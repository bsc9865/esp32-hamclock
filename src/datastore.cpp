#include "datastore.h"
#include <Arduino.h>
#include "net.h"
#include "config.h"

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
};

static void data_task(void *) {
  for (;;) {
    if (net_connected()) {
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
  xTaskCreatePinnedToCore(data_task, "data", 12288, nullptr, 1, nullptr, 0);
}
