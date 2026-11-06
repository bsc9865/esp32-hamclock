// Kartenbilder fuer Host-Tests aus assets/*.bin laden
#include "maps/maps.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static MapImage load(const char *path, std::vector<uint8_t> &buf, uint16_t *pal) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "kann %s nicht oeffnen\n", path); exit(1); }
  buf.resize(512 + MAP_SRC_W * MAP_SRC_H);
  size_t n = fread(buf.data(), 1, buf.size(), f);
  fclose(f);
  if (n != buf.size()) { fprintf(stderr, "%s: falsche Groesse\n", path); exit(1); }
  for (int i = 0; i < 256; i++) pal[i] = buf[2 * i] | (buf[2 * i + 1] << 8);
  return {pal, buf.data() + 512};
}

MapImage maps_day() {
  static std::vector<uint8_t> b; static uint16_t p[256]; static MapImage m = load("assets/map_day.bin", b, p);
  return m;
}
MapImage maps_night() {
  static std::vector<uint8_t> b; static uint16_t p[256]; static MapImage m = load("assets/map_night.bin", b, p);
  return m;
}
