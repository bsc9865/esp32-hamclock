#pragma once
// Task auf Core 0 mit Stack im PSRAM starten (spart internen RAM); Rueckfall auf internen Stack.
// Nur fuer Tasks, die waehrend Flash-Schreibzugriffen nicht laufen muessen.
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

inline bool task_start_psram(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio) {
  if (xTaskCreatePinnedToCoreWithCaps(fn, name, stack, nullptr, prio, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS)
    return true;
  return xTaskCreatePinnedToCore(fn, name, stack, nullptr, prio, nullptr, 0) == pdPASS;
}
