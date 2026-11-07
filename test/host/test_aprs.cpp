// Host-Test fuer den APRS-Parser
//   c++ -std=c++17 -Isrc test/host/test_aprs.cpp src/aprs_parse.cpp src/astro.cpp -o /tmp/ta && /tmp/ta [mitschnitt.txt]
#include "aprs_parse.h"
#include "astro.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FEHLER: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(int argc, char **argv) {
  AprsPos p;
  // unkomprimiert
  CHECK(aprs_parse("DK7JP-2>APYSNR,TCPIP*,qAS,DK7JP:!4908.13N/01108.05E- QTH Heideck", p), "unkomprimiert");
  CHECK(!strcmp(p.call, "DK7JP-2") && fabs(p.lat - 49.13550) < 1e-4 && fabs(p.lon - 11.13417) < 1e-4, "Pos %f %f", p.lat, p.lon);
  CHECK(p.sym_code == '-' && aprs_category(p.sym_table, p.sym_code) == APRS_FIXED && !strcmp(p.comment, "QTH Heideck"), "Symbol/Kommentar '%s'", p.comment);
  // mit Zeitstempel
  CHECK(aprs_parse("DL4FNM>APWW11,TCPIP*,qAC,T2ERFURT:@092808h5056.68N/01105.17E-www.DR2Q.de", p) &&
        fabs(p.lat - 50.9447) < 1e-3 && fabs(p.lon - 11.0862) < 1e-3, "Zeitstempel %f %f", p.lat, p.lon);
  // Mic-E: DO2WT-11>UQ2R16 -> 51 22.16 N, 12 44.18 E
  CHECK(aprs_parse("DO2WT-11>UQ2R16-2,WIDE1-1,WIDE2-2,qAO,DN9SKW-10:`(H.l!-/\"5/}", p), "Mic-E");
  CHECK(fabs(p.lat - (51 + 22.16 / 60)) < 1e-3 && fabs(p.lon - (12 + 44.18 / 60)) < 1e-3, "Mic-E Pos %f %f", p.lat, p.lon);
  // Symbol laut Spezifikation: Byte 7 = Code, Byte 8 = Tabelle
  CHECK(p.sym_code == '/' && p.sym_table == '"', "Mic-E Symbol %c%c", p.sym_table, p.sym_code);
  // komprimiert
  CHECK(aprs_parse("DO1VHT-10>APLRG1,TCPIP*,qAC,T2ROMANIA:=L4eNRQGiV& !GLoRa APRS", p), "komprimiert");
  CHECK(p.lat > 49 && p.lat < 52 && p.lon > 9 && p.lon < 14 && p.sym_code == '&', "komprimiert Pos %f %f %c", p.lat, p.lon, p.sym_code);
  // keine Position
  CHECK(!aprs_parse("DB0HAS-10>APMI06,TCPIP*,qAC,T2FRA:> Infos on http://www.qsl.net/db0has", p), "Status");
  CHECK(!aprs_parse("# aprsc 2.1.21", p), "Kommentar");
  CHECK(!aprs_parse("DB0ESA>APRX26,TCPIP*,qAC,T2SPAIN:T#247,2.5,0.2,10.0,0.0,0.0,00000000", p), "Telemetrie");

  if (argc > 1) {
    FILE *f = fopen(argv[1], "rb");
    char line[600];
    int ok = 0, total = 0, far = 0;
    while (f && fgets(line, sizeof line, f)) {
      if (line[0] == '#') continue;
      total++;
      if (!aprs_parse(line, p)) continue;
      ok++;
      double d = gc_distance_km(50.3125, 11.7083, p.lat, p.lon);
      if (d > 200) { far++; printf("  weit weg (%.0f km): %s", d, line); }
    }
    if (f) fclose(f);
    printf("Mitschnitt: %d von %d Paketen mit Position, %d ausserhalb 200 km\n", ok, total, far);
    CHECK(far == 0, "Positionen ausserhalb des Filterradius");
    CHECK(ok > total / 2, "zu wenige Positionen erkannt");
  }
  printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
