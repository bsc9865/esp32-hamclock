// Zugriff auf die per board_build.embed_files eingebetteten Kartenbilder (nur ESP32)
#ifdef ARDUINO
#include "maps.h"
#include <string.h>

extern const uint8_t map_start[] asm("_binary_assets_map_bin_start");
extern const uint8_t borders_start[] asm("_binary_assets_borders_bin_start");

extern const uint8_t zones_start[] asm("_binary_assets_zones_bin_start");

const uint8_t *maps_borders() { return borders_start; }
const uint8_t *maps_zones() { return zones_start; }

// Paletten in ausgerichteten RAM kopieren (eingebettete Daten sind evtl. nicht 2-Byte-ausgerichtet)
static uint16_t pal[32];

MapImage maps_image() {
  static bool init = false;
  if (!init) { memcpy(pal, map_start + 4, 64); init = true; }
  return {pal, pal + 16, map_start + 68};
}
#endif
