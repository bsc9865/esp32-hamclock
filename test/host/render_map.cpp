#include <cstring>
#include <cmath>
// Rendert die Weltkarte auf dem PC (gleicher Code wie auf dem ESP32) als PPM-Bild.
//   c++ -O2 -std=c++17 -Isrc test/host/render_map.cpp test/host/maps_file.cpp src/map_render.cpp src/astro.cpp -o /tmp/rm
//   /tmp/rm <unix-zeit> <breite> <hoehe> <zoom> <lat> <lon> karte.ppm   (aus dem Projektordner starten)
#include "map_render.h"
#include "astro.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv) {
  if (argc < 8) { fprintf(stderr, "Aufruf: rm zeit w h zoom lat lon out.ppm\n"); return 1; }
  time_t t = atol(argv[1]);
  int w = atoi(argv[2]), h = atoi(argv[3]);
  double zoom = atof(argv[4]), lat = atof(argv[5]), lon = atof(argv[6]);
  LatLon q;
  maidenhead_to_latlon("JO50uh", q);
  MapSpot sp[] = {{36, 138, 0x38BDF8}, {40, -100, 0x22C55E}, {-25, 134, 0xFACC15}, {-29, 24, 0xF97316},
                  {-10, -55, 0xE879F9}, {61, -150, 0xDC2626}, {21, -157, 0x2DD4BF}, {42.0f, 12.5f, 0xA3E635},
                  {64, -18, 0x818CF8}, {40.4f, -3.7f, 0xF472B6}};
  std::vector<uint16_t> buf(w * h);
  // optional 8. Argument: Ebenen fuer Vorschauen (a = Aurora-Testoval, c = CQ-Zonen, i = ITU-Zonen,
  // d = Detailansicht Deutschland mit lat/lon als Mitte)
  const char *layers = argc > 8 ? argv[8] : "";
  MapView v = strchr(layers, 'd') ? map_view_detail(w, h, lat, lon, zoom)
              : zoom <= 1.0 && w == 2 * h ? map_view_world(w, h) : map_view_zoom(w, h, lat, lon, zoom);
  MapOverlays ov;
  ov.borders = true;
  static uint8_t aur[360 * 181];
  if (strchr(layers, 'a')) {
    for (int r = 0; r < 181; r++)
      for (int c = 0; c < 360; c++) {
        double d = fabs(fabs(r - 90.0) - 66.0);
        aur[r * 360 + c] = d < 6 ? (uint8_t)(80 - d * 12) : 0;
      }
    ov.aurora = aur;
  }
  ov.cq_zones = strchr(layers, 'c') != nullptr;
  ov.itu_zones = strchr(layers, 'i') != nullptr;
  map_render(buf.data(), v, t, q.lat, q.lon, sp, 10, &ov);
  FILE *f = fopen(argv[7], "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (uint16_t c : buf) {
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                            (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return 0;
}
