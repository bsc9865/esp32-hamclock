// Host-Test fuer die Praefix-Suche
//   cc -c src/data/cty_data.c -o /tmp/cty.o && c++ -std=c++17 -Isrc test/host/test_cty.cpp src/cty.cpp /tmp/cty.o -o /tmp/tc && /tmp/tc
#include "cty.h"
#include <cstdio>
#include <cstring>

static int fails = 0;

static void expect(const char *call, const char *prefix) {
  CtyResult r = cty_lookup(call);
  const char *got = r.ent ? r.ent->prefix : "(keins)";
  bool ok = strcmp(got, prefix) == 0;
  printf("%-14s -> %-6s %-28s CQ %2d %s\n", call, got, r.ent ? r.ent->name : "", r.cq, ok ? "" : "  <== FEHLER");
  if (!ok) fails++;
}

int main() {
  expect("DJ0SB", "DL");
  expect("dj0sb", "DL");
  expect("DJ0SB/P", "DL");
  expect("EA8/DJ0SB", "EA8");
  expect("DJ0SB/EA8", "EA8");
  expect("W1AW", "K");
  expect("KH6XYZ", "KH6");
  expect("JA1ABC", "JA");
  expect("VK9XY", "VK9X");
  expect("UA9ABC", "UA9");
  expect("UA3ABC", "UA");
  expect("3Y0X", "3Y/p");   // exakter Eintrag (Expedition)
  expect("3Y5ABC", "CE9");  // reiner 3Y-Praefix -> Antarktis laut cty.dat
  expect("OH6BG-#", "OH");
  expect("ZS6ABC", "ZS");
  expect("VP8XYZ", "VP8");
  printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
