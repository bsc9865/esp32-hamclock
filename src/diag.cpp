#include "diag.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/idf_additions.h>
#include "config.h"
#include "net.h"
#include "settings.h"
#include "datastore.h"
#include "dxcluster.h"
#include "aprs.h"
#include "ota.h"

static esp_reset_reason_t reason;
extern uint32_t mapscreen_last_fast_ms, mapscreen_last_smooth_ms;
extern float mapscreen_last_zoom;

// Protokoll gescheiterter Speicheranforderungen (Ringpuffer)
struct AllocFail { uint32_t ms; uint32_t size; uint32_t caps; char task[12]; };
static AllocFail fails[8];
static volatile uint32_t n_fails = 0;

static void IRAM_ATTR alloc_failed(size_t size, uint32_t caps, const char *func) {
  (void)func;
  AllocFail &f = fails[n_fails % 8];
  f.ms = millis();
  f.size = size;
  f.caps = caps;
  const char *t = pcTaskGetName(nullptr);
  strncpy(f.task, t ? t : "?", sizeof f.task - 1);
  f.task[sizeof f.task - 1] = 0;
  n_fails++;
}

// PSRAM-Tiefststaende mit Zeitpunkt und laufendem Abruf (Diagnose)
struct PsramLow { uint32_t ms; uint32_t free_b; char job[24]; };
static PsramLow lows[8];
static volatile uint32_t n_lows = 0;
static uint32_t psram_min_seen = 0xFFFFFFFF;
// interner RAM: Tiefststand seit setup() mit laufendem Abruf
static PsramLow ilows[8];
static volatile uint32_t n_ilows = 0;
static uint32_t int_min_seen = 0xFFFFFFFF;

static void psram_monitor(void *) {
  for (;;) {
    uint32_t f = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (f + 65536 < psram_min_seen) {   // neuer Tiefststand (mind. 64 KB tiefer)
      psram_min_seen = f;
      PsramLow &l = lows[n_lows % 8];
      l.ms = millis();
      l.free_b = f;
      strncpy(l.job, datastore_current_job(), sizeof l.job - 1);
      l.job[sizeof l.job - 1] = 0;
      n_lows++;
    }
    uint32_t fi = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (fi + 2048 < int_min_seen) {   // neuer Tiefststand (mind. 2 KB tiefer)
      int_min_seen = fi;
      PsramLow &l = ilows[n_ilows % 8];
      l.ms = millis();
      l.free_b = fi;
      strncpy(l.job, datastore_current_job(), sizeof l.job - 1);
      l.job[sizeof l.job - 1] = 0;
      n_ilows++;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void diag_begin() {
  reason = esp_reset_reason();
  heap_caps_register_failed_alloc_callback(alloc_failed);
  xTaskCreatePinnedToCoreWithCaps(psram_monitor, "psmon", 2048, nullptr, 2, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

const char *diag_reset_reason() {
  switch (reason) {
    case ESP_RST_POWERON: return "Stromversorgung";
    case ESP_RST_EXT: return "Reset-Taste";
    case ESP_RST_SW: return "Software/Update";
    case ESP_RST_PANIC: return "Absturz (Panic)";
    case ESP_RST_INT_WDT: return "Watchdog (Interrupt)";
    case ESP_RST_TASK_WDT: return "Watchdog (Task)";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Unterspannung";
    case ESP_RST_DEEPSLEEP: return "Tiefschlaf";
    default: return "unbekannt";
  }
}

bool diag_reset_was_crash() {
  return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

// ---------- Statusdaten ----------
static const char *TASKS[] = {"loopTask", "data", "dxcluster", "sats", "aprs", "web", "async_udp"};

static String json_escape(const char *s) {
  String o;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') o += '\\';
    if ((unsigned char)*s >= 0x20) o += *s;
  }
  return o;
}

static String status_json() {
  time_t now = time(nullptr);
  String j;
  j.reserve(2048);
  j += "{\"version\":\"" FW_VERSION "\"";
  j += ",\"uptime_s\":" + String(millis() / 1000);
  j += ",\"reset_reason\":\"" + json_escape(diag_reset_reason()) + "\"";
  j += ",\"heap_internal\":" + String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  j += ",\"heap_internal_min\":" + String(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  j += ",\"heap_internal_block\":" + String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  j += ",\"psram_free\":" + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  j += ",\"psram_min\":" + String(psram_min_seen);   // seit setup() (der Heap-Wert enthaelt den Systemstart)
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"dx_state\":" + String((int)dxcluster_state());
  j += ",\"aprs_online\":" + String(aprs_online() ? "true" : "false");
  j += ",\"update\":\"" + String(ota_latest_version()) + "\"";
  j += ",\"map_render\":{\"zoom\":" + String(mapscreen_last_zoom, 1) + ",\"fast_ms\":" + String(mapscreen_last_fast_ms) +
       ",\"smooth_ms\":" + String(mapscreen_last_smooth_ms) + "}";
  j += ",\"alloc_fails\":" + String(n_fails) + ",\"alloc_fail_log\":[";
  for (uint32_t i = 0; i < 8 && i < n_fails; i++) {
    const AllocFail &f = fails[(n_fails - 1 - i) % 8];
    if (i) j += ",";
    j += "{\"t_s\":" + String(f.ms / 1000) + ",\"size\":" + String(f.size) + ",\"caps\":\"0x" + String(f.caps, HEX) +
         "\",\"task\":\"" + json_escape(f.task) + "\"}";
  }
  j += "]";
  j += ",\"psram_lows\":[";
  for (uint32_t i = 0; i < 8 && i < n_lows; i++) {
    const PsramLow &l = lows[(n_lows - 1 - i) % 8];
    if (i) j += ",";
    j += "{\"t_ms\":" + String(l.ms) + ",\"free\":" + String(l.free_b) + ",\"job\":\"" + json_escape(l.job) + "\"}";
  }
  j += "]";
  j += ",\"internal_lows\":[";
  for (uint32_t i = 0; i < 8 && i < n_ilows; i++) {
    const PsramLow &l = ilows[(n_ilows - 1 - i) % 8];
    if (i) j += ",";
    j += "{\"t_ms\":" + String(l.ms) + ",\"free\":" + String(l.free_b) + ",\"job\":\"" + json_escape(l.job) + "\"}";
  }
  j += "]";
  j += ",\"tasks\":{";
  bool first = true;
  for (const char *t : TASKS) {
    TaskHandle_t h = xTaskGetHandle(t);
    if (!h) continue;
    if (!first) j += ",";
    first = false;
    j += "\"" + String(t) + "\":" + String(uxTaskGetStackHighWaterMark(h));
  }
  j += "},\"jobs\":[";
  JobInfo jobs[20];
  int n = datastore_jobs(jobs, 20);
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"name\":\"" + json_escape(jobs[i].name) + "\",\"ok\":" + String(jobs[i].ok) + ",\"fail\":" +
         String(jobs[i].fail) + ",\"age_s\":" + String(jobs[i].last_ok ? (long)(now - jobs[i].last_ok) : -1) +
         ",\"ms\":" + String(jobs[i].last_ms) + "}";
  }
  j += "]}";
  return j;
}

static String fmt_age(long s) {
  if (s < 0) return "nie";
  if (s < 120) return String(s) + " s";
  if (s < 7200) return String(s / 60) + " min";
  return String(s / 3600) + " h";
}

static String status_html() {
  time_t now = time(nullptr);
  uint32_t up = millis() / 1000;
  String h;
  h.reserve(6000);
  h += F("<!doctype html><html lang=de><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
         "<meta http-equiv=refresh content=30><title>ESP32-HamClock Status</title><style>"
         "body{font-family:system-ui,sans-serif;background:#0a0e14;color:#e6edf3;margin:16px;max-width:860px}"
         "h1{color:#38bdf8;font-size:22px}h2{color:#38bdf8;font-size:16px;margin-top:22px}"
         "table{border-collapse:collapse;width:100%}td,th{padding:4px 8px;border-bottom:1px solid #263142;text-align:left}"
         "th{color:#7d8b9c;font-weight:500}.bad{color:#ef4444}.ok{color:#22c55e}.dim{color:#7d8b9c}"
         "</style></head><body>");
  h += "<h1>ESP32-HamClock " + String(g_settings.call) + " &middot; " + String(g_settings.loc) + "</h1>";
  h += "<table>";
  h += "<tr><th>Firmware</th><td>" FW_VERSION;
  if (ota_update_available()) h += " <span class=ok>(Update " + String(ota_latest_version()) + " verf&uuml;gbar)</span>";
  h += "</td></tr>";
  h += "<tr><th>Laufzeit</th><td>" + String(up / 86400) + " d " + String((up / 3600) % 24) + " h " + String((up / 60) % 60) + " min</td></tr>";
  h += "<tr><th>Letzter Neustart</th><td" + String(diag_reset_was_crash() ? " class=bad" : "") + ">" + diag_reset_reason() + "</td></tr>";
  h += "<tr><th>Interner RAM</th><td>" + String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB frei, Tiefststand " +
       String(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB, gr&ouml;&szlig;ter Block " +
       String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024) + " KB</td></tr>";
  h += "<tr><th>PSRAM</th><td>" + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) + " KB frei, Tiefststand " +
       String(psram_min_seen / 1024) + " KB (seit Start)</td></tr>";
  h += "<tr><th>Speicherfehler</th><td" + String(n_fails ? " class=bad" : "") + ">" + String(n_fails);
  if (n_fails) {
    const AllocFail &f = fails[(n_fails - 1) % 8];
    h += " (zuletzt " + String(f.size) + " Bytes, Task " + String(f.task) + ", nach " + String(f.ms / 1000) + " s)";
  }
  h += "</td></tr>";
  h += "<tr><th>WLAN</th><td>" + net_ip() + ", " + String(WiFi.RSSI()) + " dBm</td></tr>";
  static const char *dx[] = {"aus", "verbinde", "online", "Fehler"};
  h += "<tr><th>DX-Cluster</th><td>" + String(dx[dxcluster_state()]) + " (" + String(dxcluster_server()) + ")</td></tr>";
  h += "<tr><th>APRS-IS</th><td>" + String(aprs_online() ? "online" : "aus") + "</td></tr>";
  h += "</table><h2>Datenquellen</h2><table><tr><th>Quelle</th><th>zuletzt OK</th><th>OK</th><th>Fehler</th><th>Dauer</th></tr>";
  JobInfo jobs[20];
  int n = datastore_jobs(jobs, 20);
  for (int i = 0; i < n; i++) {
    long age = jobs[i].last_ok ? (long)(now - jobs[i].last_ok) : -1;
    bool stale = age < 0 || age > (long)jobs[i].interval_s * 3 + 300;
    h += "<tr><td>" + String(jobs[i].name) + "</td><td" + (stale ? " class=bad" : "") + ">" + fmt_age(age) +
         "</td><td>" + String(jobs[i].ok) + "</td><td" + (jobs[i].fail ? " class=bad" : "") + ">" + String(jobs[i].fail) +
         "</td><td class=dim>" + String(jobs[i].last_ms) + " ms</td></tr>";
  }
  h += "</table><h2>Task-Stacks (freie Bytes, Tiefststand)</h2><table>";
  for (const char *t : TASKS) {
    TaskHandle_t th = xTaskGetHandle(t);
    if (!th) continue;
    UBaseType_t w = uxTaskGetStackHighWaterMark(th);
    h += "<tr><td>" + String(t) + "</td><td" + (w < 1024 ? " class=bad" : "") + ">" + String(w) + "</td></tr>";
  }
  h += "</table><p class=dim>Aktualisiert sich alle 30 s &middot; <a href=/status.json style='color:#38bdf8'>status.json</a></p></body></html>";
  return h;
}

// ---------- Webserver ----------
static WebServer *server;

static void web_task(void *) {
  // warten, bis das WLAN steht
  while (!net_connected()) vTaskDelay(pdMS_TO_TICKS(1000));
  server = new WebServer(80);
  server->on("/", [] { server->send(200, "text/html; charset=utf-8", status_html()); });
  server->on("/status.json", [] { server->send(200, "application/json", status_json()); });
  server->on("/heapinfo", [] {   // Heap-Bereiche auf die serielle Schnittstelle (Diagnose)
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    server->send(200, "text/plain", "Heap-Info auf Serial ausgegeben\n");
  });
  server->onNotFound([] { server->send(404, "text/plain", "nicht gefunden"); });
  server->begin();
  Serial.printf("Statusseite: http://%s.local/ bzw. http://%s/\n", HOSTNAME, net_ip().c_str());
  for (;;) {
    server->handleClient();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void web_begin() {
  if (xTaskCreatePinnedToCoreWithCaps(web_task, "web", 6144, nullptr, 1, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    xTaskCreatePinnedToCore(web_task, "web", 6144, nullptr, 1, nullptr, 0);
}
