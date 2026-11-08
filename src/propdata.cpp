#include "propdata.h"
#include <Arduino.h>
#include "net.h"
#include "config.h"

static PropModel *model[2];               // Doppelpuffer (PSRAM)
static Ionosonde *stations[2];
static int n_st[2];
static volatile int cur = -1;
static volatile uint32_t seq = 0;
static uint8_t *grid;                     // MUF-Raster fuer die Karte
static time_t grid_t = 0;
static uint32_t grid_seq = 0xFFFFFFFF;

uint32_t propdata_seq() { return seq; }

static bool stale = false;        // nur aeltere Messwerte verfuegbar
bool propdata_stale() { return stale; }
const PropModel *propdata_model() { return cur >= 0 ? model[cur] : nullptr; }

int propdata_stations(const Ionosonde **out) {
  if (cur < 0) { *out = nullptr; return 0; }
  *out = stations[cur];
  return n_st[cur];
}

bool propdata_fetch() {
  String body;
  if (!http_get(URL_KC2G_STATIONS, body)) return false;
  int next = cur == 0 ? 1 : 0;
  time_t now = time(nullptr);
  int n = prop_parse_kc2g(body.c_str(), now, 2 * 3600, stations[next], 128);
  bool old = false;
  if (n < 3) {   // KC2G/GIRO stockt gelegentlich: dann aeltere Messwerte (bis 8 h) mit Hinweis
    n = prop_parse_kc2g(body.c_str(), now, 8 * 3600, stations[next], 128);
    old = true;
  }
  body = String();
  if (n < 3) {
    Serial.printf("KC2G: zu wenige aktuelle Ionosonden (%d)\n", n);
    return false;
  }
  uint32_t t0 = millis();
  prop_build(*model[next], stations[next], n, now, [] { vTaskDelay(1); });   // Watchdog/IDLE nicht aushungern
  n_st[next] = n;
  cur = next;
  stale = old;
  seq++;
  Serial.printf("KC2G: %d Ionosonden, Modell in %lu ms berechnet\n", n, millis() - t0);
  return true;
}

const uint8_t *propdata_muf_grid(time_t t) {
  const PropModel *m = propdata_model();
  if (!m) return nullptr;
  // Tagesgang aendert sich langsam: hoechstens alle 5 min neu berechnen
  if (grid_seq != seq || t - grid_t >= 300 || t < grid_t) {
    prop_muf_grid(*m, t, grid);
    grid_t = t;
    grid_seq = seq;
  }
  return grid;
}

void propdata_begin() {
  for (int i = 0; i < 2; i++) {
    model[i] = (PropModel *)heap_caps_calloc(1, sizeof(PropModel), MALLOC_CAP_SPIRAM);
    stations[i] = (Ionosonde *)heap_caps_calloc(128, sizeof(Ionosonde), MALLOC_CAP_SPIRAM);
  }
  grid = (uint8_t *)heap_caps_calloc(PROP_GW * PROP_GH, 1, MALLOC_CAP_SPIRAM);
}
