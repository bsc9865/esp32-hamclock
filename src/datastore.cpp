#include "datastore.h"
#include <Arduino.h>
#include "net.h"
#include "config.h"
#include "activations.h"
#include "spacewx.h"
#include "sats.h"
#include "satfreq.h"
#include "propdata.h"
#include "ota.h"
#include "weather.h"

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
  const char *name;
  time_t last_ok;      // letzter erfolgreicher Abruf
  uint32_t ok, fail;   // Zaehler
  uint32_t last_ms;    // Dauer des letzten Abrufs
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
  {job_solar, SOLAR_REFRESH_S, 0, "Solar (hamqsl)"},
  {activations_fetch_pota, ACT_REFRESH_S, 0, "POTA"},
  {activations_fetch_wwff, ACT_REFRESH_S, 0, "WWFF"},
  {spacewx_fetch_kp, 15 * 60, 0, "Kp (SWPC)"},
  {spacewx_fetch_xray, 5 * 60, 0, "X-Ray (SWPC)"},
  {spacewx_fetch_alerts, 15 * 60, 0, "Meldungen (SWPC)"},
  {spacewx_fetch_drap, 15 * 60, 0, "D-RAP (SWPC)"},
  {spacewx_fetch_aurora, 30 * 60, 0, "Aurora (SWPC)"},
  {spacewx_fetch_sfi, 3 * 3600, 0, "SFI-Verlauf (SWPC)"},
  {sats_fetch_tle, 3600, 0, "TLE (CelesTrak)"},   // prueft selbst, ob die TLEs aelter als 12 h sind
  {satfreq_fetch, 3600, 0, "Sat-Frequenzen (SatNOGS)"},   // prueft selbst, was fehlt oder veraltet ist
  {propdata_fetch, 15 * 60, 0, "Ionosonden (KC2G)"},
  {ota_check_github, 6 * 3600, 0, "Update (GitHub)"},
  {weather_fetch, 15 * 60, 0, "Wetter (Open-Meteo)"},
};
static constexpr int N_JOBS = sizeof jobs / sizeof jobs[0];
static volatile const char *current_job = "-";
const char *datastore_current_job() { return (const char *)current_job; }

int datastore_jobs(JobInfo *out, int max) {
  int n = 0;
  for (int i = 0; i < N_JOBS && n < max; i++)
    out[n++] = {jobs[i].name, jobs[i].last_ok, jobs[i].ok, jobs[i].fail, jobs[i].last_ms, jobs[i].interval_s};
  return n;
}

static void data_task(void *) {
  for (;;) {
    net_poll();
    ota_poll_requests();
    if (net_connected() && time_valid()) {   // Abrufe erst mit gueltiger Uhrzeit
      uint32_t now = millis();
      for (auto &j : jobs) {
        if ((int32_t)(now - j.next_ms) >= 0) {
          uint32_t t0 = millis();
          current_job = j.name;
          bool ok = j.run();
          current_job = "-";
          j.last_ms = millis() - t0;
          if (ok) { j.ok++; j.last_ok = time(nullptr); } else j.fail++;
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
