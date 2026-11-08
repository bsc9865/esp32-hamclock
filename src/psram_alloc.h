#pragma once
// Speicher fuer ArduinoJson-Dokumente aus dem PSRAM (grosse Antworten belasten so nicht den internen RAM)
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};

inline PsramAllocator *psram_allocator() {
  static PsramAllocator a;
  return &a;
}
