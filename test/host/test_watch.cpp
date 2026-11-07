// Host-Test fuer Watchlist und Mode-Klassen
//   cc -c src/data/cty_data.c -o /tmp/cty.o
//   c++ -std=c++17 -Isrc test/host/test_watch.cpp src/watch.cpp src/cty.cpp /tmp/cty.o -o /tmp/tw && /tmp/tw
#include "watch.h"
#include "cty.h"
#include <cstdio>
#include <cstring>

static int fails = 0;
static void expect(const char *list, const char *call, bool want) {
  CtyResult r = cty_lookup(call);
  bool got = watch_match(list, call, r.ent);
  if (got != want) { printf("FEHLER: '%s' gegen '%s' -> %d, erwartet %d\n", call, list, got, want); fails++; }
}

int main() {
  const char *w = "3Y0J, VP8*  @Bouvet\n@KH6;dl1abc";
  expect(w, "3Y0J", true);
  expect(w, "3y0j", true);
  expect(w, "3Y0J/P", true);
  expect(w, "3Y0K", false);
  expect(w, "VP8LP", true);
  expect(w, "VP2V/W1AW", false);
  expect(w, "KH6ABC", true);        // @KH6 = Hauptpraefix Hawaii
  expect(w, "W1AW", false);
  expect(w, "DL1ABC", true);        // Gross-/Kleinschreibung in der Liste egal
  expect(w, "DL1ABC-#", true);      // Skimmer-Endung
  expect("@bouvet", "3Y0X", false); // 3Y0X ist Peter I, nicht Bouvet
  expect("@peter", "3Y0X", true);
  expect("@Fed. Rep. of Germany", "DJ0SB", true);   // Name mit Leerzeichen
  expect("", "DJ0SB", false);
  expect("*", "DJ0SB", false);      // nur '*' ist kein sinnvoller Eintrag

  struct { const char *m; uint8_t c; } modes[] = {{"CW", MODE_CW}, {"SSB", MODE_SSB}, {"FT8", MODE_DIGI}, {"DIGI", MODE_DIGI},
                                                   {"RTTY", MODE_DIGI}, {"", MODE_OTHER}, {"XYZ", MODE_OTHER}};
  for (auto &x : modes)
    if (mode_class(x.m) != x.c) { printf("FEHLER: Mode %s\n", x.m); fails++; }

  printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
