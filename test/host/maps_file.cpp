// Kartenbilder fuer Host-Tests aus assets/*.bin laden
#include "maps/maps.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

MapImage maps_image() {
  static std::vector<uint8_t> buf;
  static uint16_t pal[32];
  if (buf.empty()) {
    FILE *f = fopen("assets/map.bin", "rb");
    if (!f) { fprintf(stderr, "kann assets/map.bin nicht oeffnen\n"); exit(1); }
    buf.resize(68 + MAP_SRC_W * MAP_SRC_H / 2);
    size_t n = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (n != buf.size() || buf[0] != 'M' || buf[3] != '4') { fprintf(stderr, "map.bin: falsches Format\n"); exit(1); }
    for (int i = 0; i < 32; i++) pal[i] = buf[4 + 2 * i] | (buf[5 + 2 * i] << 8);
  }
  return {pal, pal + 16, buf.data() + 68};
}

// Ganze Datei einmalig laden; nullptr, wenn sie fehlt
static const uint8_t *load_blob(const char *path, std::vector<uint8_t> &b) {
  if (b.empty()) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
  }
  return b.data();
}

const uint8_t *maps_borders() { static std::vector<uint8_t> b; return load_blob("assets/borders.bin", b); }
const uint8_t *maps_zones() { static std::vector<uint8_t> b; return load_blob("assets/zones.bin", b); }
const uint8_t *maps_detail() { static std::vector<uint8_t> b; return load_blob("assets/detail.bin", b); }
