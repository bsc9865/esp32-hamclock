#include "spacewx.h"
#include <atomic>
#include <Arduino.h>
#include "net.h"
#include "config.h"

static SemaphoreHandle_t mtx;
static WxData *data;                       // PSRAM
static uint8_t *aurora[2], *drap[2];       // Doppelpuffer: Abruf schreibt in den inaktiven
static volatile int aurora_cur = -1, drap_cur = -1;
static std::atomic<uint32_t> seq{0};

uint32_t spacewx_seq() { return seq; }

void spacewx_get(WxData &out) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  out = *data;
  xSemaphoreGive(mtx);
}

const uint8_t *spacewx_aurora() { return aurora_cur >= 0 ? aurora[aurora_cur] : nullptr; }
const uint8_t *spacewx_drap() { return drap_cur >= 0 ? drap[drap_cur] : nullptr; }

template <typename F>
static bool fetch(const char *url, const char *name, F parse) {
  String body;
  if (!http_get(url, body)) return false;
  bool ok = parse(body.c_str());
  if (ok) seq++;
  else Serial.printf("%s: keine Daten erkannt\n", name);
  return ok;
}

bool spacewx_fetch_kp() {
  return fetch(URL_SWPC_KP, "Kp", [](const char *s) {
    static WxPoint tmp[64];
    int n = wx_parse_kp(s, tmp, 64);
    if (n < 1) return false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    memcpy(data->kp, tmp, n * sizeof(WxPoint));
    data->n_kp = n;
    xSemaphoreGive(mtx);
    Serial.printf("Kp: %d Werte, aktuell %.2f\n", n, tmp[n - 1].v);
    return true;
  });
}

bool spacewx_fetch_sfi() {
  return fetch(URL_SWPC_SFI, "SFI", [](const char *s) {
    static WxPoint tmp[40];
    int n = wx_parse_flux(s, tmp, 40);
    if (n < 1) return false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    memcpy(data->sfi, tmp, n * sizeof(WxPoint));
    data->n_sfi = n;
    xSemaphoreGive(mtx);
    Serial.printf("SFI-Verlauf: %d Werte\n", n);
    return true;
  });
}

bool spacewx_fetch_xray() {
  return fetch(URL_SWPC_XRAY, "X-Ray", [](const char *s) {
    static WxPoint *tmp = (WxPoint *)heap_caps_malloc(400 * sizeof(WxPoint), MALLOC_CAP_SPIRAM);
    int n = wx_parse_xray(s, tmp, 400);
    if (n < 1) return false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    memcpy(data->xray, tmp, n * sizeof(WxPoint));
    data->n_xray = n;
    xSemaphoreGive(mtx);
    char c[8];
    wx_xray_class(tmp[n - 1].v, c);
    Serial.printf("X-Ray: %d Werte, aktuell %s\n", n, c);
    return true;
  });
}

bool spacewx_fetch_alerts() {
  return fetch(URL_SWPC_ALERTS, "Meldungen", [](const char *s) {
    static WxAlert *tmp = (WxAlert *)heap_caps_malloc(8 * sizeof(WxAlert), MALLOC_CAP_SPIRAM);
    int n = wx_parse_alerts(s, tmp, 8);
    xSemaphoreTake(mtx, portMAX_DELAY);
    memcpy(data->alerts, tmp, n * sizeof(WxAlert));
    data->n_alerts = n;
    xSemaphoreGive(mtx);
    Serial.printf("SWPC-Meldungen: %d\n", n);
    return true;
  });
}

static void ovation_chunk(const char *d, size_t n, void *ctx) { ((OvationParser *)ctx)->feed(d, n); }

bool spacewx_fetch_aurora() {
  int next = aurora_cur == 0 ? 1 : 0;
  OvationParser p(aurora[next]);
  uint32_t t0 = millis();
  if (!http_stream(URL_SWPC_AURORA, ovation_chunk, &p) || p.count() < AURORA_W * AURORA_H / 2) {
    Serial.printf("Aurora: Abruf unvollstaendig (%d Punkte)\n", p.count());
    return false;
  }
  aurora_cur = next;
  seq++;
  Serial.printf("Aurora: %d Punkte in %lu ms\n", p.count(), millis() - t0);
  return true;
}

bool spacewx_fetch_drap() {
  return fetch(URL_SWPC_DRAP, "D-RAP", [](const char *s) {
    int next = drap_cur == 0 ? 1 : 0;
    if (!wx_parse_drap(s, drap[next])) return false;
    drap_cur = next;
    Serial.println("D-RAP: aktualisiert");
    return true;
  });
}

void spacewx_begin() {
  mtx = xSemaphoreCreateMutex();
  data = (WxData *)heap_caps_calloc(1, sizeof(WxData), MALLOC_CAP_SPIRAM);
  for (int i = 0; i < 2; i++) {
    aurora[i] = (uint8_t *)heap_caps_calloc(AURORA_W * AURORA_H, 1, MALLOC_CAP_SPIRAM);
    drap[i] = (uint8_t *)heap_caps_calloc(DRAP_W * DRAP_H, 1, MALLOC_CAP_SPIRAM);
  }
}
