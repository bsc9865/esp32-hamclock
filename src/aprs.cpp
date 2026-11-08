#include "task_util.h"
#include "aprs.h"
#include <atomic>
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/idf_additions.h>
#include "config.h"
#include "net.h"
#include "settings.h"

static SemaphoreHandle_t mtx;
static AprsStation *st;            // PSRAM
static int n_st = 0;
static std::atomic<uint32_t> seq{0};
static volatile bool online = false;
static double qlat, qlon;

uint32_t aprs_seq() { return seq; }
bool aprs_online() { return online; }

static void store(const AprsPos &p) {
  time_t now = time(nullptr);
  xSemaphoreTake(mtx, portMAX_DELAY);
  int idx = -1, oldest = 0;
  for (int i = 0; i < n_st; i++) {
    if (strcmp(st[i].p.call, p.call) == 0) { idx = i; break; }
    if (st[i].t < st[oldest].t) oldest = i;
  }
  if (idx < 0) idx = n_st < APRS_MAX_STATIONS ? n_st++ : oldest;
  st[idx].p = p;
  st[idx].t = now;
  seq++;
  xSemaphoreGive(mtx);
}

int aprs_snapshot(AprsStation *out, int max) {
  time_t now = time(nullptr);
  xSemaphoreTake(mtx, portMAX_DELAY);
  int n = 0;
  for (int i = 0; i < n_st && n < max; i++)
    if (now - st[i].t <= APRS_RETENTION_S) out[n++] = st[i];
  xSemaphoreGive(mtx);
  std::sort(out, out + n, [](const AprsStation &a, const AprsStation &b) { return a.t > b.t; });
  return n;
}

static void session() {
  WiFiClient c;
  Serial.printf("APRS-IS: verbinde %s:%d\n", APRS_SERVER, APRS_PORT);
  if (!c.connect(APRS_SERVER, APRS_PORT, 10000)) {
    Serial.println("APRS-IS: Verbindung fehlgeschlagen");
    return;
  }
  c.printf("user %s%s pass -1 vers ESP32-HamClock %s filter r/%.2f/%.2f/%d\r\n", g_settings.call, APRS_LOGIN_SSID,
           FW_VERSION, qlat, qlon, APRS_RADIUS_KM);
  online = true;
  Serial.printf("APRS-IS: verbunden mit %s, Radius %d km\n", APRS_SERVER, APRS_RADIUS_KM);
  static char line[512];
  size_t len = 0;
  uint32_t last_rx = millis();
  int n_pos = 0;
  while (c.connected() && net_connected() && (g_settings.layers & LAYER_APRS)) {
    while (c.available()) {
      int ch = c.read();
      last_rx = millis();
      if (ch == '\n') {
        line[len] = 0;
        len = 0;
        AprsPos p;
        if (aprs_parse(line, p)) { store(p); n_pos++; }
      } else if (ch != '\r' && len < sizeof line - 1) {
        line[len++] = (char)ch;
      }
    }
    if (millis() - last_rx > 3 * 60 * 1000UL) {   // Server sendet sonst alle 20 s einen Kommentar
      Serial.println("APRS-IS: keine Daten, neue Verbindung");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  c.stop();
  online = false;
  Serial.printf("APRS-IS: getrennt (%d Positionen)\n", n_pos);
}

static void aprs_task(void *) {
  uint32_t backoff = 10;
  for (;;) {
    // nur verbinden, wenn die Kartenebene eingeschaltet ist
    if (!net_connected() || !time_valid() || !(g_settings.layers & LAYER_APRS)) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    uint32_t start = millis();
    session();
    if (millis() - start > 5 * 60 * 1000UL) backoff = 10;
    if (!(g_settings.layers & LAYER_APRS)) continue;   // abgeschaltet: kein Warten
    vTaskDelay(pdMS_TO_TICKS(backoff * 1000));
    backoff = backoff < 300 ? backoff * 2 : 300;
  }
}

void aprs_begin(double lat, double lon) {
  qlat = lat;
  qlon = lon;
  mtx = xSemaphoreCreateMutex();
  st = (AprsStation *)heap_caps_calloc(APRS_MAX_STATIONS, sizeof(AprsStation), MALLOC_CAP_SPIRAM);
  task_start_psram(aprs_task, "aprs", 5120, 1);
}
