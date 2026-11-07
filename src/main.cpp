// ESP32-HamClock – Amateurfunk-Dashboard fuer Waveshare ESP32-S3-Touch-LCD-7
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "display.h"
#include "net.h"
#include "datastore.h"
#include "ui.h"
#include "settings.h"
#include "dxcluster.h"
#include "sats.h"
#include "astro.h"
#include "ota.h"
#include "weather.h"
#include "aprs.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32-HamClock " FW_VERSION " ===");
  Serial.printf("PSRAM: %lu KB\n", ESP.getPsramSize() / 1024);

  settings_load();
  display_init();
  net_begin();
  datastore_begin();
  dxcluster_begin();
  LatLon q;
  maidenhead_to_latlon(g_settings.loc, q);
  sats_begin(q.lat, q.lon);
  weather_begin(q.lat, q.lon);
  aprs_begin(q.lat, q.lon);
  ota_begin();
  ui_build();
  Serial.printf("Setup fertig. Freier Speicher: intern %u KB (groesster Block %u KB), PSRAM %u KB\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024,
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
}

void loop() {
  ota_loop();
  lv_timer_handler();
  delay(5);
}
