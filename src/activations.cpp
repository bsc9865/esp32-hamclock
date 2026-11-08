#include "activations.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "net.h"
#include "config.h"
#include "timeutil.h"

const char *const ACT_NAMES[ACT_TYPES] = {"POTA", "WWFF"};
const uint32_t ACT_COLORS[ACT_TYPES] = {0x4ADE80, 0x2DD4BF};

static constexpr int MAX_PER_TYPE = 100;
static SemaphoreHandle_t mtx;
static Activation *store[ACT_TYPES];     // je Typ ein Feld im PSRAM
static int count[ACT_TYPES];
static volatile uint32_t seq = 0;

uint32_t act_seq() { return seq; }

// ---------- Hilfsfunktionen ----------
void utf8_to_ascii(const char *in, char *out, size_t n) {
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)in;
  auto put = [&](const char *s) { while (*s && o < n - 1) out[o++] = *s++; };
  while (*p && o < n - 1) {
    if (*p < 0x80) { out[o++] = *p++; continue; }
    uint32_t cp = 0;
    int len = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
    cp = *p & (0xFF >> (len + 1));
    for (int i = 1; i < len && p[i]; i++) cp = (cp << 6) | (p[i] & 0x3F);
    p += len;
    switch (cp) {
      case 0xE4: put("ae"); break;  case 0xF6: put("oe"); break;  case 0xFC: put("ue"); break;
      case 0xC4: put("Ae"); break;  case 0xD6: put("Oe"); break;  case 0xDC: put("Ue"); break;
      case 0xDF: put("ss"); break;
      default:
        if (cp >= 0xC0 && cp <= 0xC5) put("A");
        else if (cp >= 0xE0 && cp <= 0xE5) put("a");
        else if (cp >= 0xC8 && cp <= 0xCB) put("E");
        else if (cp >= 0xE8 && cp <= 0xEB) put("e");
        else if (cp >= 0xCC && cp <= 0xCF) put("I");
        else if (cp >= 0xEC && cp <= 0xEF) put("i");
        else if ((cp >= 0xD2 && cp <= 0xD5) || cp == 0xD8) put("O");
        else if ((cp >= 0xF2 && cp <= 0xF5) || cp == 0xF8) put("o");
        else if (cp >= 0xD9 && cp <= 0xDB) put("U");
        else if (cp >= 0xF9 && cp <= 0xFB) put("u");
        else if (cp == 0xD1) put("N");
        else if (cp == 0xF1) put("n");
        else if (cp == 0xC7) put("C");
        else if (cp == 0xE7) put("c");
        else if (cp == 0x2013 || cp == 0x2014) put("-");
        else if (cp == 0x2018 || cp == 0x2019) put("'");
        else put("?");
    }
  }
  out[o] = 0;
}

static void cpy(char *dst, size_t n, const char *src) {
  if (!src) { dst[0] = 0; return; }
  utf8_to_ascii(src, dst, n);
}

static bool valid_pos(const Activation &a) {
  return a.lat >= -90 && a.lat <= 90 && a.lon >= -180 && a.lon <= 180;   // auch NaN faellt heraus
}

// JSON-Speicher aus dem PSRAM
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static PsramAllocator psram_alloc;

static void commit(ActType type, Activation *tmp, int n) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  memcpy(store[type], tmp, n * sizeof(Activation));
  count[type] = n;
  seq++;
  xSemaphoreGive(mtx);
}

// ---------- POTA ----------
bool activations_fetch_pota() {
  String body;
  if (!http_get(URL_POTA_SPOTS, body)) return false;
  JsonDocument filter;
  JsonObject f = filter[0].to<JsonObject>();
  for (const char *k : {"activator", "frequency", "mode", "reference", "name", "locationDesc",
                        "latitude", "longitude", "spotTime", "comments"})
    f[k] = true;
  JsonDocument doc(&psram_alloc);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err) { Serial.printf("POTA: JSON-Fehler %s\n", err.c_str()); return false; }

  static Activation *tmp = (Activation *)heap_caps_malloc(MAX_PER_TYPE * sizeof(Activation), MALLOC_CAP_SPIRAM);
  int n = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (n >= MAX_PER_TYPE) break;
    Activation &a = tmp[n];
    a.type = ACT_POTA;
    cpy(a.call, sizeof a.call, o["activator"]);
    a.khz = atof(o["frequency"] | "0");
    cpy(a.mode, sizeof a.mode, o["mode"]);
    cpy(a.ref, sizeof a.ref, o["reference"]);
    cpy(a.name, sizeof a.name, o["name"]);
    cpy(a.loc, sizeof a.loc, o["locationDesc"]);
    a.lat = o["latitude"] | 0.0f;
    a.lon = o["longitude"] | 0.0f;
    a.t = parse_iso_utc(o["spotTime"]);
    cpy(a.comment, sizeof a.comment, o["comments"]);
    a.band = band_from_khz(a.khz);
    if (a.call[0] && a.khz > 0 && valid_pos(a)) n++;
  }
  // neueste zuerst
  std::sort(tmp, tmp + n, [](const Activation &x, const Activation &y) { return x.t > y.t; });
  commit(ACT_POTA, tmp, n);
  Serial.printf("POTA: %d Aktivierungen\n", n);
  return true;
}

// ---------- WWFF ----------
bool activations_fetch_wwff() {
  String body;
  if (!http_get(URL_WWFF_SPOTS, body)) return false;
  JsonDocument filter;
  JsonObject f = filter[0].to<JsonObject>();
  for (const char *k : {"activator", "frequency_khz", "mode", "reference", "reference_name",
                        "latitude", "longitude", "spot_time", "remarks"})
    f[k] = true;
  JsonDocument doc(&psram_alloc);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err) { Serial.printf("WWFF: JSON-Fehler %s\n", err.c_str()); return false; }

  static Activation *tmp = (Activation *)heap_caps_malloc(MAX_PER_TYPE * sizeof(Activation), MALLOC_CAP_SPIRAM);
  int n = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (n >= MAX_PER_TYPE) break;
    Activation &a = tmp[n];
    a.type = ACT_WWFF;
    cpy(a.call, sizeof a.call, o["activator"]);
    a.khz = o["frequency_khz"] | 0.0;
    cpy(a.mode, sizeof a.mode, o["mode"]);
    cpy(a.ref, sizeof a.ref, o["reference"]);
    cpy(a.name, sizeof a.name, o["reference_name"]);
    a.loc[0] = 0;
    a.lat = o["latitude"] | 0.0f;
    a.lon = o["longitude"] | 0.0f;
    a.t = (time_t)(o["spot_time"] | 0LL);
    cpy(a.comment, sizeof a.comment, o["remarks"]);
    a.band = band_from_khz(a.khz);
    if (a.call[0] && a.khz > 0 && valid_pos(a)) n++;
  }
  std::sort(tmp, tmp + n, [](const Activation &x, const Activation &y) { return x.t > y.t; });
  commit(ACT_WWFF, tmp, n);
  Serial.printf("WWFF: %d Aktivierungen\n", n);
  return true;
}

int act_snapshot(Activation *out, int max, uint8_t type_mask, uint16_t band_mask) {
  time_t now = time(nullptr);
  int n = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  // beide Listen nach Zeit zusammenfuehren (jeweils schon sortiert)
  int i[ACT_TYPES] = {0, 0};
  while (n < max) {
    int best = -1;
    for (int t = 0; t < ACT_TYPES; t++) {
      if (!(type_mask & (1 << t)) || i[t] >= count[t]) continue;
      if (best < 0 || store[t][i[t]].t > store[best][i[best]].t) best = t;
    }
    if (best < 0) break;
    const Activation &a = store[best][i[best]++];
    if (now - a.t > ACT_RETENTION_S) continue;
    if (a.band < 16 && !(band_mask & (1u << a.band))) continue;
    out[n++] = a;
  }
  xSemaphoreGive(mtx);
  return n;
}

void activations_begin() {
  mtx = xSemaphoreCreateMutex();
  for (int t = 0; t < ACT_TYPES; t++)
    store[t] = (Activation *)heap_caps_calloc(MAX_PER_TYPE, sizeof(Activation), MALLOC_CAP_SPIRAM);
}
