// Host-Test fuer den DX-Spot-Parser
//   cc -c src/data/cty_data.c -o /tmp/cty.o
//   c++ -std=c++17 -Isrc test/host/test_spots.cpp src/spots.cpp src/cty.cpp /tmp/cty.o -o /tmp/ts && /tmp/ts
#include "spots.h"
#include "cty.h"
#include <cstdio>
#include <cstring>
#include <cmath>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FEHLER: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
  const time_t now = 1794333600;  // 2026-11-06 18:00:00 UTC
  Spot s;

  CHECK(spot_parse("DX de UA4FKT:     1862.5  RA4FUR                                      1758Z", now, s), "Zeile 1");
  CHECK(!strcmp(s.spotter, "UA4FKT") && !strcmp(s.dx, "RA4FUR"), "Rufzeichen %s %s", s.spotter, s.dx);
  CHECK(fabs(s.khz - 1862.5) < 0.01 && s.band == B_160, "Frequenz/Band %.1f %d", s.khz, s.band);
  CHECK(s.t == now - 120, "Zeit %ld", (long)(s.t - now));
  CHECK(s.ent >= 0 && !strcmp(cty_entities[s.ent].prefix, "UA"), "Land");
  CHECK(!strcmp(s.mode, "SSB"), "Mode %s", s.mode);

  CHECK(spot_parse("DX de OH6BG-#:    7015.0  JA1XYZ       CW 22 dB 20 WPM CQ             1759Z", now, s), "RBN");
  CHECK(!strcmp(s.dx, "JA1XYZ") && s.band == B_40 && !strcmp(s.mode, "CW"), "RBN %s %d %s", s.dx, s.band, s.mode);
  CHECK(!strcmp(s.comment, "CW 22 dB 20 WPM CQ"), "Kommentar '%s'", s.comment);

  CHECK(spot_parse("DX de DL1ABC:    14074.0  VK9XY        FT8 -12dB from QF56          1801Z JO62", now, s), "mit Locator");
  CHECK(!strcmp(s.mode, "FT8") && s.band == B_20 && s.t == now + 60, "FT8 %s %ld", s.mode, (long)(s.t - now));
  CHECK(!strcmp(cty_entities[s.ent].name, "Christmas Island"), "VK9X");

  // Spot von kurz vor Mitternacht, empfangen kurz danach
  const time_t after_midnight = 1794355500;  // 2026-11-07 00:05 UTC
  CHECK(spot_parse("DX de K1TTT:     28495.0  ZS6ABC       up 5                           2358Z", after_midnight, s), "Mitternacht");
  CHECK(s.t == after_midnight - 7 * 60, "Mitternacht Zeit %ld", (long)(s.t - after_midnight));
  CHECK(!strcmp(s.mode, "SSB") && s.band == B_10, "10m SSB %s", s.mode);

  CHECK(!spot_parse("DJ0SB-8 de G6NHU-2  6-Nov-2026 1758Z dxspider >", now, s), "Prompt ist kein Spot");
  CHECK(!spot_parse("DX de :", now, s), "kaputte Zeile");

  CHECK(band_from_khz(50313) == B_6 && band_from_khz(144300) == B_2 && band_from_khz(9000) == B_OTHER, "Baender");

  printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
