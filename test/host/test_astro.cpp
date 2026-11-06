// Host-Tests fuer Astronomie/Geo-Rechnungen (laufen auf dem PC, nicht auf dem ESP32)
//   c++ -std=c++17 -Isrc test/host/test_astro.cpp src/astro.cpp -o /tmp/t && /tmp/t
#include "astro.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FEHLER: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static int local_hm(time_t t) { struct tm l; localtime_r(&t, &l); return l.tm_hour * 60 + l.tm_min; }

int main() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  LatLon q;
  CHECK(maidenhead_to_latlon("JO50uh", q), "Locator ungueltig");
  CHECK(fabs(q.lat - 50.3125) < 1e-3 && fabs(q.lon - 11.7083) < 1e-3, "JO50uh -> %.4f %.4f", q.lat, q.lon);
  char loc[7];
  latlon_to_maidenhead(q.lat, q.lon, loc);
  CHECK(strcmp(loc, "JO50uh") == 0, "Rueckumrechnung %s", loc);
  latlon_to_maidenhead(-33.87, 151.21, loc);   // Sydney
  CHECK(strncmp(loc, "QF56", 4) == 0, "Sydney %s", loc);

  // Sonnenauf-/untergang JO50uh am 4.11.2026: ca. 07:08 / 16:44 MEZ
  time_t r, s;
  CHECK(sun_rise_set(1793808000, q.lat, q.lon, r, s), "kein Auf-/Untergang");
  CHECK(abs(local_hm(r) - (7 * 60 + 8)) <= 3, "Aufgang %d min", local_hm(r));
  CHECK(abs(local_hm(s) - (16 * 60 + 44)) <= 3, "Untergang %d min", local_hm(s));

  // Sommersonnenwende: Deklination ~ +23,44
  SunPos sp = sun_position(1781964000);
  CHECK(fabs(sp.decl - 23.44) < 0.1, "Deklination %.2f", sp.decl);
  // Mittag UTC am Aequinoktium: Subsolarpunkt nahe Laenge 0 (+- Zeitgleichung)
  sp = sun_position(1773921600);  // 2026-03-19 12:00 UTC
  CHECK(fabs(sp.subsolar_lon + sp.eot_min / 4.0) < 0.5, "Subsolar-Laenge %.2f", sp.subsolar_lon);

  // Neumond 19.03.2026 ~01:23 UTC
  double age, il;
  moon_phase(1773883380, age, il);
  CHECK(il < 0.02, "Neumond-Beleuchtung %.3f", il);

  // Grosskreis JO50uh -> W1AW
  double d = gc_distance_km(q.lat, q.lon, 41.71, -72.73);
  double b = gc_bearing_deg(q.lat, q.lon, 41.71, -72.73);
  CHECK(fabs(d - 6234) < 20, "Distanz %.0f", d);
  CHECK(fabs(b - 296) < 2, "Richtung %.0f", b);

  printf(fails ? "%d Test(s) fehlgeschlagen\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
