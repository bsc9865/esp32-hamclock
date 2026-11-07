// Host-Test fuer die Satellitenberechnung
//   c++ -std=c++17 -Isrc -Ilib/sgp4core/src test/host/test_satellite.cpp src/satellite.cpp \
//       lib/sgp4core/src/*.cpp -o /tmp/tsat && /tmp/tsat [amateur.tle]
#include "satellite.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FEHLER: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

// ISS-TLE vom 06.11.2026 (CelesTrak, Gruppe "amateur")
static const char *ISS0 = "ISS (ZARYA)";
static const char *ISS1 = "1 25544U 98067A   26310.88530624  .00012224  00000+0  22778-3 0  9997";
static const char *ISS2 = "2 25544  51.6322 313.9479 0007239 356.4073   3.6863 15.49223765593878";

int main(int argc, char **argv) {
  char sn[16];
  Satellite::short_name("OSCAR 7 (AO-7)          ", sn, sizeof sn); CHECK(!strcmp(sn, "AO-7"), "Kurzname %s", sn);
  Satellite::short_name("ISS (ZARYA)", sn, sizeof sn);               CHECK(!strcmp(sn, "ISS"), "Kurzname %s", sn);
  Satellite::short_name("RS-44 & BREEZE-KM R/B", sn, sizeof sn);     CHECK(!strcmp(sn, "RS-44"), "Kurzname %s", sn);
  Satellite::short_name("ES'HAIL 2", sn, sizeof sn);                 CHECK(!strcmp(sn, "QO-100"), "Kurzname %s", sn);
  Satellite::short_name("XW-3 (CAS-9)", sn, sizeof sn);              CHECK(!strcmp(sn, "CAS-9"), "Kurzname %s", sn);
  Satellite::short_name("LILACSAT-2", sn, sizeof sn);                CHECK(!strcmp(sn, "LILACSAT-2"), "Kurzname %s", sn);
  Satellite::short_name("UMKA 1 (RS40S)", sn, sizeof sn);            CHECK(!strcmp(sn, "UMKA 1"), "Kurzname %s", sn);

  Satellite iss;
  CHECK(iss.init(ISS0, ISS1, ISS2), "ISS init");
  CHECK(iss.norad() == 25544, "NORAD %d", iss.norad());
  CHECK(fabs(iss.period_min() - 92.9) < 0.5, "Umlaufzeit %.2f", iss.period_min());

  Observer qth{50.3125, 11.7083, 0.4};
  const time_t t0 = 1794052800;   // 2026-11-07 12:00 UTC (kurz nach der TLE-Epoche)
  SatState s;
  CHECK(iss.state(t0, qth, s), "state");
  CHECK(s.alt_km > 380 && s.alt_km < 440, "Hoehe %.1f", s.alt_km);
  CHECK(fabs(s.lat) <= 51.7, "Breite %.2f liegt ausserhalb der Inklination", s.lat);
  printf("ISS %ld: %.2f %.2f %.0f km, Az %.0f El %.1f, %.0f km\n", (long)t0, s.lat, s.lon, s.alt_km, s.az, s.el, s.range_km);

  // Ueber 24 h: Breiten bleiben innerhalb der Inklination, Hoehe stabil
  double minlat = 90, maxlat = -90;
  for (time_t t = t0; t < t0 + 86400; t += 120) {
    iss.state(t, qth, s);
    minlat = fmin(minlat, s.lat);
    maxlat = fmax(maxlat, s.lat);
  }
  CHECK(maxlat > 51 && maxlat < 52 && minlat < -51 && minlat > -52, "Breitenbereich %.2f..%.2f", minlat, maxlat);

  SatPass p[16];
  int n = iss.passes(qth, t0, t0 + 86400, 0, p, 16);
  printf("ISS-Ueberfluege in 24 h: %d\n", n);
  CHECK(n >= 3 && n <= 8, "Anzahl Ueberfluege %d", n);
  for (int i = 0; i < n; i++) {
    struct tm a; gmtime_r(&p[i].aos, &a);
    printf("  AOS %02d:%02d:%02d  Az %3.0f  max %4.1f Grad  Az %3.0f  LOS +%3ld s  Az %3.0f\n", a.tm_hour, a.tm_min,
           a.tm_sec, p[i].az_aos, p[i].max_el, p[i].az_max, (long)(p[i].los - p[i].aos), p[i].az_los);
    CHECK(p[i].los > p[i].aos && p[i].los - p[i].aos < 15 * 60, "Dauer %ld", (long)(p[i].los - p[i].aos));
    CHECK(p[i].tmax > p[i].aos && p[i].tmax < p[i].los, "tmax ausserhalb");
    CHECK(p[i].max_el > 0 && p[i].max_el <= 90, "max_el %.1f", p[i].max_el);
    // Elevation am AOS ~ 0
    Satellite c = iss;
    c.state(p[i].aos, qth, s);
    CHECK(fabs(s.el) < 0.2, "El am AOS %.3f", s.el);
  }

  // Referenz: Skyfield 1.x mit derselben TLE (find_events, Horizont 0 Grad)
  //   AOS 19:05:46 Az 199.1, MAX 19:10:27 El 14.9 ; 4. Ueberflug MAX 00:00:28 El 84.2
  if (n >= 4) {
    struct tm a; gmtime_r(&p[0].aos, &a);
    int aos_s = a.tm_hour * 3600 + a.tm_min * 60 + a.tm_sec;
    CHECK(abs(aos_s - (19 * 3600 + 5 * 60 + 46)) <= 3, "AOS 1 weicht von Skyfield ab (%d s)", aos_s);
    CHECK(fabs(p[0].max_el - 14.9) < 0.3 && fabs(p[0].az_aos - 199.1) < 0.5, "Pass 1 max %.1f az %.1f", p[0].max_el, p[0].az_aos);
    CHECK(fabs(p[3].max_el - 84.2) < 0.3, "Pass 4 max %.1f", p[3].max_el);
  }

  float fl[72], fo[72];
  Satellite::footprint(50, 10, 420, 72, fl, fo);
  CHECK(fabs(fl[0] - (50 + 20.6)) < 0.5 && fabs(fl[36] - (50 - 20.6)) < 0.5, "Footprint %.2f %.2f", fl[0], fl[36]);

  // Optional: komplette TLE-Datei
  if (argc > 1) {
    FILE *f = fopen(argv[1], "r");
    char a[100], b[100], c[100];
    int ok = 0, bad = 0;
    while (f && fgets(a, sizeof a, f) && fgets(b, sizeof b, f) && fgets(c, sizeof c, f)) {
      Satellite x;
      if (x.init(a, b, c) && x.state(t0, qth, s)) ok++; else { bad++; printf("  fehlerhaft: %s", a); }
    }
    if (f) fclose(f);
    printf("TLE-Datei: %d ok, %d fehlerhaft\n", ok, bad);
    CHECK(ok > 50, "zu wenige gueltige TLEs");
  }
  printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
