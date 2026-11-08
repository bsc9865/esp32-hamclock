// Frequenzen der Favoriten-Satelliten aus der SatNOGS DB (CC BY-SA 4.0)
#include "satfreq.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "activations.h"   // utf8_to_ascii
#include "config.h"
#include "net.h"
#include "sats.h"

static constexpr int MAX_TX = SAT_MAX_FAV * SATFREQ_PER_SAT;
static const char *CACHE_FILE = "/sattx.bin";
static const uint32_t CACHE_MAGIC = 0x31585453 ^ sizeof(SatTx);   // "STX1", Layout-Pruefung

struct Fetched { int32_t norad; int64_t when; };

static SemaphoreHandle_t mtx;
static SatTx *table;                       // PSRAM
static int n_tx = 0;
static Fetched fetched[SAT_MAX_FAV * 2];   // wann welcher Satellit geholt wurde
static int n_fetched = 0;
static volatile uint32_t seq = 0;

uint32_t satfreq_seq() { return seq; }

struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static PsramAllocator psram_alloc;

static void init_once() {
  if (mtx) return;
  mtx = xSemaphoreCreateMutex();
  table = (SatTx *)heap_caps_calloc(MAX_TX, sizeof(SatTx), MALLOC_CAP_SPIRAM);
}

static void load_cache();

void satfreq_begin() {
  init_once();
  load_cache();
}

int satfreq_get(int norad, SatTx *out, int max) {
  if (!mtx) return 0;
  int k = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < n_tx && k < max; i++)
    if (table[i].norad == norad) out[k++] = table[i];
  xSemaphoreGive(mtx);
  return k;
}

// ---------- Zwischenspeicher ----------
static void save_cache() {
  File f = LittleFS.open(CACHE_FILE, "w");
  if (!f) return;
  uint32_t hdr[3] = {CACHE_MAGIC, (uint32_t)n_fetched, (uint32_t)n_tx};
  f.write((const uint8_t *)hdr, sizeof hdr);
  f.write((const uint8_t *)fetched, n_fetched * sizeof(Fetched));
  f.write((const uint8_t *)table, n_tx * sizeof(SatTx));
  f.close();
}

static void load_cache() {
  if (!LittleFS.exists(CACHE_FILE)) return;
  File f = LittleFS.open(CACHE_FILE, "r");
  uint32_t hdr[3];
  if (f.read((uint8_t *)hdr, sizeof hdr) == sizeof hdr && hdr[0] == CACHE_MAGIC &&
      hdr[1] <= (uint32_t)(SAT_MAX_FAV * 2) && hdr[2] <= (uint32_t)MAX_TX) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    n_fetched = f.read((uint8_t *)fetched, hdr[1] * sizeof(Fetched)) / sizeof(Fetched);
    n_tx = f.read((uint8_t *)table, hdr[2] * sizeof(SatTx)) / sizeof(SatTx);
    xSemaphoreGive(mtx);
    seq++;
    Serial.printf("SatNOGS: %d Frequenzen fuer %d Satelliten aus dem Speicher\n", n_tx, n_fetched);
  }
  f.close();
}

// ---------- Abruf ----------
static uint64_t hz(JsonVariantConst v) { return v.isNull() ? 0 : v.as<uint64_t>(); }

// Holt die Frequenzen eines Satelliten; out: bis zu SATFREQ_PER_SAT Eintraege
static int fetch_one(int norad, SatTx *out) {
  char url[120];
  snprintf(url, sizeof url, URL_SATNOGS_TX, norad);
  String body;
  if (!http_get(url, body)) return -1;
  JsonDocument filter;
  JsonObject f = filter[0].to<JsonObject>();
  for (const char *k : {"description", "status", "type", "uplink_low", "uplink_high", "downlink_low", "downlink_high",
                        "mode", "invert"})
    f[k] = true;
  JsonDocument doc(&psram_alloc);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err) { Serial.printf("SatNOGS %d: JSON-Fehler %s\n", norad, err.c_str()); return -1; }

  // Transponder und Transceiver zuerst, dann Baken/Telemetrie
  int n = 0;
  for (int pass = 0; pass < 2; pass++)
    for (JsonObject o : doc.as<JsonArray>()) {
      if (n >= SATFREQ_PER_SAT) break;
      if (strcmp(o["status"] | "", "active") != 0) continue;
      const char *type = o["type"] | "";
      uint8_t kind = !strcmp(type, "Transponder") ? TX_TRANSPONDER : (!strcmp(type, "Transceiver") ? TX_TRANSCEIVER : TX_TRANSMITTER);
      if ((kind == TX_TRANSMITTER) != (pass == 1)) continue;
      uint64_t dl = hz(o["downlink_low"]), ul = hz(o["uplink_low"]);
      if (!(dl && satfreq_amateur_hz(dl)) && !(ul && satfreq_amateur_hz(ul))) continue;
      SatTx t{};
      t.norad = norad;
      t.kind = kind;
      utf8_to_ascii(o["description"] | "", t.desc, sizeof t.desc);
      utf8_to_ascii(o["mode"] | "", t.mode, sizeof t.mode);
      t.invert = o["invert"] | false;
      t.up_lo = ul;
      t.up_hi = hz(o["uplink_high"]);
      t.dn_lo = dl;
      t.dn_hi = hz(o["downlink_high"]);
      bool dup = false;   // SatNOGS fuehrt manche Sender doppelt
      for (int i = 0; i < n; i++)
        if (out[i].dn_lo == t.dn_lo && out[i].up_lo == t.up_lo && !strcmp(out[i].mode, t.mode)) dup = true;
      if (!dup) out[n++] = t;
    }
  return n;
}

bool satfreq_fetch() {
  if (!mtx) return false;
  int fav[SAT_MAX_FAV];
  int nf = sats_favorites(fav, SAT_MAX_FAV);
  time_t now = time(nullptr);
  bool ok = true, changed = false;
  static SatTx *got = (SatTx *)heap_caps_calloc(SATFREQ_PER_SAT, sizeof(SatTx), MALLOC_CAP_SPIRAM);
  if (!got || !table) return false;
  for (int i = 0; i < nf; i++) {
    int64_t when = 0;
    for (int j = 0; j < n_fetched; j++) if (fetched[j].norad == fav[i]) when = fetched[j].when;
    if (when && now - when < SATFREQ_REFRESH_S) continue;
    int n = fetch_one(fav[i], got);
    if (n < 0) { ok = false; continue; }
    xSemaphoreTake(mtx, portMAX_DELAY);
    // alte Eintraege dieses Satelliten entfernen, neue anhaengen
    int k = 0;
    for (int j = 0; j < n_tx; j++) if (table[j].norad != fav[i]) table[k++] = table[j];
    for (int j = 0; j < n && k < MAX_TX; j++) table[k++] = got[j];
    n_tx = k;
    // Abrufzeit merken; Satelliten, die keine Favoriten mehr sind, vergessen
    int m = 0;
    for (int j = 0; j < n_fetched; j++)
      if (fetched[j].norad != fav[i] && sats_is_favorite(fetched[j].norad)) fetched[m++] = fetched[j];
    if (m < SAT_MAX_FAV * 2) fetched[m++] = {fav[i], (int64_t)now};
    n_fetched = m;
    xSemaphoreGive(mtx);
    changed = true;
    Serial.printf("SatNOGS: %d Frequenzen fuer %d\n", n, fav[i]);
  }
  if (changed) {
    // Eintraege von Satelliten entfernen, die keine Favoriten mehr sind
    xSemaphoreTake(mtx, portMAX_DELAY);
    int k = 0;
    for (int j = 0; j < n_tx; j++) if (sats_is_favorite(table[j].norad)) table[k++] = table[j];
    n_tx = k;
    xSemaphoreGive(mtx);
    save_cache();
    seq++;
  }
  return ok;
}
