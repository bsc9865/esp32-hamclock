#pragma once
// Kartenbilder: 256-Farben-Palette (RGB565) + 1 Byte pro Pixel, equirektangulaer
// Erzeugt von scripts/gen_maps.py -> assets/map_day.bin, assets/map_night.bin
#include <stdint.h>

#define MAP_SRC_W 1600
#define MAP_SRC_H 800

struct MapImage {
  const uint16_t *pal;   // 256 Eintraege (im RAM, ausgerichtet)
  const uint8_t *px;     // MAP_SRC_W * MAP_SRC_H Indizes
};

// Auf dem ESP32: eingebettete Dateien; auf dem PC: aus Datei geladen
MapImage maps_day();
MapImage maps_night();

// Landgrenzen (assets/borders.bin, siehe scripts/gen_borders.py); nullptr wenn nicht vorhanden
const uint8_t *maps_borders();
// CQ-/ITU-Zonen (assets/zones.bin, siehe scripts/gen_zones.py)
const uint8_t *maps_zones();
