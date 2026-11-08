#pragma once
// Kartenbild: schlichte Land/Wasser-Karte, 16 Farben (4 Bit pro Pixel), equirektangulaer
// Erzeugt von scripts/gen_map_simple.py -> assets/map.bin
#include <stdint.h>

#define MAP_SRC_W 1600
#define MAP_SRC_H 800

struct MapImage {
  const uint16_t *pal_day;     // 16 Eintraege (im RAM, ausgerichtet)
  const uint16_t *pal_night;   // 16 Eintraege, gleiche Indizes
  const uint8_t *px;           // MAP_SRC_W * MAP_SRC_H / 2 Bytes, gerades x im unteren Nibble
};

static inline uint8_t map_px(const uint8_t *row, int u) {
  uint8_t b = row[u >> 1];
  return (u & 1) ? (b >> 4) : (b & 15);
}

// Auf dem ESP32: eingebettete Datei; auf dem PC: aus Datei geladen
MapImage maps_image();

// Landgrenzen (assets/borders.bin, siehe scripts/gen_borders.py); nullptr wenn nicht vorhanden
const uint8_t *maps_borders();
// CQ-/ITU-Zonen (assets/zones.bin, siehe scripts/gen_zones.py)
const uint8_t *maps_zones();
