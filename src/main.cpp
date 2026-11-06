// ESP32-HamClock – Amateurfunk-Dashboard fuer Waveshare ESP32-S3-Touch-LCD-7
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "display.h"
#include "net.h"
#include "datastore.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32-HamClock " FW_VERSION " ===");
  Serial.printf("PSRAM: %lu KB\n", ESP.getPsramSize() / 1024);

  display_init();
  net_begin();
  datastore_begin();
  ui_build();
  Serial.println("Setup fertig.");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
