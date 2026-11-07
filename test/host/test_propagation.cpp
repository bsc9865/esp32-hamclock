// Host-Test fuer das Ausbreitungsmodell
//   c++ -std=c++17 -Isrc test/host/test_propagation.cpp src/propagation.cpp src/astro.cpp src/timeutil.cpp -o /tmp/tp
//   /tmp/tp [stations.json unix-zeit]
#include "propagation.h"
#include "astro.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FEHLER: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static PropModel model;   // gross -> statisch

int main(int argc, char **argv) {
  // Tagesgang: 1 h nach Mittag am Subsolarpunkt = 1, tiefe Nacht = 0.45
  time_t t = 1794052800;   // 2026-11-07 12:00 UTC
  SunPos sp = sun_position(t - 3600);
  CHECK(fabs(prop_daylight_factor(sp.decl, sp.subsolar_lon, t) - 1.0f) < 0.01, "Mittag");
  CHECK(fabs(prop_daylight_factor(-sp.decl, sp.subsolar_lon + 180, t) - 0.45f) < 0.01, "Mitternacht");

  // Mini-JSON: eine Station veraltet, eine mit zu geringem Vertrauen
  const char *js = R"([
    {"fof2": 8.0, "station": {"name": "A", "latitude": "50.0", "longitude": "10.0"}, "cs": 90.0, "mufd": 25.0, "time": "2026-11-07T11:50:00"},
    {"fof2": 5.0, "station": {"name": "Alt", "latitude": "40.0", "longitude": "350.0"}, "cs": 90.0, "mufd": 15.0, "time": "2026-11-07T06:00:00"},
    {"fof2": 5.0, "station": {"name": "Unsicher", "latitude": "60.0", "longitude": "20.0"}, "cs": 10.0, "mufd": 9.0, "time": "2026-11-07T11:55:00"},
    {"fof2": 6.0, "station": {"name": "Sued", "latitude": "-30.0", "longitude": "300.0"}, "cs": -1.0, "mufd": 20.0, "time": "2026-11-07T11:45:00"}
  ])";
  Ionosonde st[64];
  int n = prop_parse_kc2g(js, t, 2 * 3600, st, 64);
  CHECK(n == 2 && !strcmp(st[0].name, "A") && fabs(st[1].lon - (-60)) < 0.01, "Parser n=%d", n);

  prop_build(model, st, n, t);
  // An der Station wird (fast) der Messwert reproduziert
  float m0 = prop_muf(model, 50.0, 10.0, st[0].t);
  CHECK(fabs(m0 - 25.0f) < 1.5f, "MUF an Station A %.1f", m0);
  // Nachts niedriger als tags (gleicher Ort, 12 h spaeter)
  float night = prop_muf(model, 50.0, 10.0, t + 12 * 3600);
  CHECK(night < m0 * 0.7f, "Nacht %.1f vs Tag %.1f", night, m0);

  PathForecast pf;
  prop_path(model, nullptr, 125, 50.31, 11.71, 41.71, -72.73, t, pf);   // JO50uh -> W1AW
  printf("JO50uh -> W1AW: %.0f km, %.0f Grad, %d Spruenge, MUF %.1f, LUF %.1f :", pf.dist_km, pf.bearing, pf.hops, pf.muf, pf.luf);
  for (int b = 0; b < PROP_BANDS; b++) printf(" %s=%c", PROP_BAND_NAMES[b], "-~+"[pf.band[b]]);
  printf("\n");
  CHECK(fabs(pf.dist_km - 6234) < 20 && pf.hops == 2, "Pfad %.0f km %d hops", pf.dist_km, pf.hops);
  CHECK(pf.luf < pf.muf, "LUF %.1f >= MUF %.1f", pf.luf, pf.muf);

  if (argc > 2) {
    FILE *f = fopen(argv[1], "rb");
    std::string s;
    char b[65536];
    size_t k;
    while (f && (k = fread(b, 1, sizeof b, f)) > 0) s.append(b, k);
    if (f) fclose(f);
    time_t now = atol(argv[2]);
    static Ionosonde all[128];
    n = prop_parse_kc2g(s.c_str(), now, 2 * 3600, all, 128);
    printf("Echte Daten: %d aktuelle Ionosonden\n", n);
    prop_build(model, all, n, now);
    // Kreuzvalidierung: jede Station weglassen und vorhersagen
    double err = 0;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
      static Ionosonde rest[128];
      int r = 0;
      for (int j = 0; j < n; j++) if (j != i) rest[r++] = all[j];
      static PropModel cv;
      prop_build(cv, rest, r, now);
      float p = prop_muf(cv, all[i].lat, all[i].lon, all[i].t);
      err += fabs(p - all[i].mufd);
      cnt++;
      printf("  %-28s gemessen %5.1f  vorhergesagt %5.1f\n", all[i].name, all[i].mufd, p);
    }
    printf("Mittlerer Fehler (Kreuzvalidierung): %.1f MHz\n", cnt ? err / cnt : 0);
    static const struct { const char *name; double lat, lon; } T[] = {
        {"G London", 51.5, -0.1}, {"W1 USA Ost", 41.7, -72.7}, {"W6 USA West", 37.8, -122.4},
        {"PY Brasilien", -23.5, -46.6}, {"ZS Suedafrika", -26.2, 28.0}, {"JA Japan", 35.7, 139.7},
        {"VU Indien", 28.6, 77.2}, {"VK Australien", -33.9, 151.2}};
    for (auto &d : T) {
      prop_path(model, nullptr, 125, 50.31, 11.71, d.lat, d.lon, now, pf);
      printf("  %-14s %6.0f km MUF %5.1f LUF %4.1f :", d.name, pf.dist_km, pf.muf, pf.luf);
      for (int b = 0; b < PROP_BANDS; b++) printf(" %3s%c", PROP_BAND_NAMES[b], "-~+"[pf.band[b]]);
      printf("\n");
    }
  }
  printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
