#include "datastore.h"
#include <Arduino.h>
#include "net.h"
#include "config.h"
#include "activations.h"
#include "spacewx.h"
#include "sats.h"
#include "propdata.h"

static SemaphoreHandle_t mtx;
static SolarData g_solar;
static volatile uint32_t g_solar_seq = 0;

uint32_t solar_seq() { return g_solar_seq; }

SolarData solar_get() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  SolarData c = g_solar;
  xSemaphoreGive(mtx);
  return c;
}

// Plant einen Abruf: faellig, wenn next_ms erreicht ist
struct Job {
  bool (*run)();
  uint32_t interval_s;
  uint32_t next_ms;
};

static bool job_solar() {
  SolarData d;
  if (!solar_fetch(d)) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  g_solar = d;
  g_solar_seq++;
  xSemaphoreGive(mtx);
  return true;
}

static Job jobs[] = {
  {job_solar, SOLAR_REFRESH_S, 0},
  {activations_fetch_pota, ACT_REFRESH_S, 0},
  {activations_fetch_wwff, ACT_REFRESH_S, 0},
  {spacewx_fetch_kp, 15 * 60, 0},
  {spacewx_fetch_xray, 5 * 60, 0},
  {spacewx_fetch_alerts, 15 * 60, 0},
  {spacewx_fetch_drap, 15 * 60, 0},
  {spacewx_fetch_aurora, 30 * 60, 0},
  {spacewx_fetch_sfi, 3 * 3600, 0},
  {sats_fetch_tle, 3600, 0},          // prueft selbst, ob die TLEs aelter als 12 h sind
  {propdata_fetch, 15 * 60, 0},
};

static void data_task(void *) {
  for (;;) {
    net_poll();
    if (net_connected() && time_valid()) {   // Abrufe erst mit gueltiger Uhrzeit
      uint32_t now = millis();
      for (auto &j : jobs) {
        if ((int32_t)(now - j.next_ms) >= 0) {
          bool ok = j.run();
          j.next_ms = millis() + (ok ? j.interval_s : RETRY_S) * 1000UL;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void datastore_begin() {
  mtx = xSemaphoreCreateMutex();
  activations_begin();
  spacewx_begin();
  propdata_begin();
  xTaskCreatePinnedToCore(data_task, "data", 12288, nullptr, 1, nullptr, 0);
}
