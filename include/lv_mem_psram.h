#pragma once
// LVGL-Speicher (Widgets, Styles, Texte) aus dem PSRAM holen, damit der knappe
// interne RAM fuer Display-DMA, WLAN und Stacks frei bleibt.
#include <stddef.h>
#include <esp_heap_caps.h>

static inline void *lv_psram_malloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(n, MALLOC_CAP_8BIT);
}
static inline void *lv_psram_realloc(void *p, size_t n) {
  void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return q ? q : heap_caps_realloc(p, n, MALLOC_CAP_8BIT);
}
static inline void lv_psram_free(void *p) { heap_caps_free(p); }
