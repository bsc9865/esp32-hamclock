// Zugriff auf die per board_build.embed_files eingebetteten Kartenbilder (nur ESP32)
#ifdef ARDUINO
#include "maps.h"
#include <string.h>

extern const uint8_t day_start[] asm("_binary_assets_map_day_bin_start");
extern const uint8_t night_start[] asm("_binary_assets_map_night_bin_start");

// Palette in ausgerichteten RAM kopieren (eingebettete Daten sind evtl. nicht 2-Byte-ausgerichtet)
static uint16_t pal_day[256], pal_night[256];

MapImage maps_day() {
  static bool init = false;
  if (!init) { memcpy(pal_day, day_start, 512); init = true; }
  return {pal_day, day_start + 512};
}

MapImage maps_night() {
  static bool init = false;
  if (!init) { memcpy(pal_night, night_start, 512); init = true; }
  return {pal_night, night_start + 512};
}
#endif
