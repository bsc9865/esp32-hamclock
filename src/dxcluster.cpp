#include "dxcluster.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/idf_additions.h>
#include "config.h"
#include "net.h"
#include "settings.h"

static SemaphoreHandle_t mtx;
static Spot *spots;               // Ringpuffer im PSRAM
static int head = 0, count = 0;   // head = Index des neuesten Spots
static volatile uint32_t seq = 0;
static volatile DxState state = DX_OFF;
static char server[48];

DxState dxcluster_state() { return state; }
const char *dxcluster_server() { return server; }
uint32_t dx_seq() { return seq; }

// Neuer Spot: gleiche Station auf gleichem Band innerhalb 10 min -> ersetzen
static void store(const Spot &s) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < count; i++) {
    Spot &o = spots[(head - i + DX_MAX_SPOTS) % DX_MAX_SPOTS];
    if (o.band == s.band && strcmp(o.dx, s.dx) == 0 && s.t - o.t < 600) {
      // aelteren Eintrag entfernen: nach vorne ruecken
      for (int j = i; j > 0; j--)
        spots[(head - j + DX_MAX_SPOTS) % DX_MAX_SPOTS] = spots[(head - j + 1 + DX_MAX_SPOTS) % DX_MAX_SPOTS];
      spots[head] = s;
      seq++;
      xSemaphoreGive(mtx);
      return;
    }
  }
  head = (head + 1) % DX_MAX_SPOTS;
  spots[head] = s;
  if (count < DX_MAX_SPOTS) count++;
  seq++;
  xSemaphoreGive(mtx);
}

int dx_snapshot(Spot *out, int max, uint16_t band_mask) {
  time_t now = time(nullptr);
  int n = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < count && n < max; i++) {
    const Spot &s = spots[(head - i + DX_MAX_SPOTS) % DX_MAX_SPOTS];
    if (now - s.t > DX_RETENTION_S) continue;
    if (s.band < 16 && !(band_mask & (1u << s.band))) continue;
    out[n++] = s;
  }
  xSemaphoreGive(mtx);
  return n;
}

// ---------- Telnet ----------
static bool read_line(WiFiClient &c, char *buf, size_t n, size_t &len, uint32_t &last_rx) {
  while (c.available()) {
    int ch = c.read();
    last_rx = millis();
    if (ch == '\r') continue;
    if (ch == '\n') { buf[len] = 0; len = 0; return true; }
    if (ch == 0xFF) continue;            // Telnet-Steuerzeichen ignorieren
    if (len < n - 1) buf[len++] = (char)ch;
  }
  // Login-Prompt kommt ohne Zeilenende
  buf[len] = 0;
  return false;
}

static void session(const char *host, uint16_t port) {
  WiFiClient c;
  state = DX_CONNECTING;
  Serial.printf("DX-Cluster: verbinde %s:%u\n", host, port);
  if (!c.connect(host, port, 10000)) {
    Serial.println("DX-Cluster: Verbindung fehlgeschlagen");
    state = DX_ERROR;
    return;
  }
  char login[24];
  snprintf(login, sizeof login, "%s%s", g_settings.call, DX_LOGIN_SSID);

  char line[256];
  size_t len = 0;
  uint32_t last_rx = millis(), t0 = millis();
  bool logged_in = false;
  int n_spots = 0;

  while (c.connected() && net_connected()) {
    bool got = read_line(c, line, sizeof line, len, last_rx);
    if (!logged_in && (strstr(line, "login:") || strstr(line, "call:") ||
                       (got && strstr(line, "Please enter your call")) || millis() - t0 > 8000)) {
      c.printf("%s\r\n", login);
      logged_in = true;
      state = DX_ONLINE;
      Serial.printf("DX-Cluster: angemeldet als %s\n", login);
      len = 0;
      continue;
    }
    if (got) {
      Spot s;
      if (spot_parse(line, time(nullptr), s)) {
        store(s);
        if (++n_spots == 1) Serial.printf("DX-Cluster: erster Spot %s %.1f\n", s.dx, s.khz);
      }
      continue;
    }
    if (millis() - last_rx > 10 * 60 * 1000UL) {   // 10 min Stille -> neu verbinden
      Serial.println("DX-Cluster: keine Daten, neue Verbindung");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  c.stop();
  state = DX_ERROR;
  Serial.printf("DX-Cluster: getrennt (%d Spots empfangen)\n", n_spots);
}

static void dx_task(void *) {
  uint32_t backoff = 10;
  for (;;) {
    if (!net_connected() || !time_valid()) {
      state = DX_OFF;
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    char host[48];
    uint16_t port;
    if (!settings_cluster_hostport(host, sizeof host, port)) {
      state = DX_ERROR;
      vTaskDelay(pdMS_TO_TICKS(30000));
      continue;
    }
    snprintf(server, sizeof server, "%s:%u", host, port);
    uint32_t start = millis();
    session(host, port);
    if (millis() - start > 5 * 60 * 1000UL) backoff = 10;   // lief laenger: schnell neu verbinden
    vTaskDelay(pdMS_TO_TICKS(backoff * 1000));
    backoff = backoff < 300 ? backoff * 2 : 300;
  }
}

void dxcluster_begin() {
  mtx = xSemaphoreCreateMutex();
  spots = (Spot *)heap_caps_calloc(DX_MAX_SPOTS, sizeof(Spot), MALLOC_CAP_SPIRAM);
  // Stack im PSRAM (der Task nutzt nur Sockets, keinen Flash) - spart internen RAM
  if (xTaskCreatePinnedToCoreWithCaps(dx_task, "dxcluster", 6144, nullptr, 1, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    xTaskCreatePinnedToCore(dx_task, "dxcluster", 6144, nullptr, 1, nullptr, 0);
}
