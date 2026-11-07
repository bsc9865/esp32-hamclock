// Host-Test fuer die SWPC-Parser mit echten Beispieldateien
//   c++ -std=c++17 -Isrc test/host/test_spacewx.cpp src/spacewx_parse.cpp src/timeutil.cpp -o /tmp/tw
//   /tmp/tw <verzeichnis mit den SWPC-Dateien>
// Ohne Verzeichnis werden nur die eingebauten Mini-Beispiele getestet.
#include "spacewx_parse.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FEHLER: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static std::string slurp(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  if (!f) return "";
  std::string s;
  char b[65536];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
  fclose(f);
  return s;
}

int main(int argc, char **argv) {
  WxPoint pts[400];
  // Mini-Beispiele
  int n = wx_parse_kp(R"([{"time_tag":"2026-10-31T00:00:00","Kp":3.00,"a_running":15},{"time_tag":"2026-10-31T03:00:00","Kp":5.33}])", pts, 64);
  CHECK(n == 2 && pts[1].v > 5.3f && pts[1].t - pts[0].t == 3 * 3600, "Kp mini n=%d", n);
  n = wx_parse_xray(R"([{"time_tag": "2026-11-07T01:17:00Z", "flux": 2.2e-08, "energy": "0.05-0.4nm"}, {"time_tag": "2026-11-07T01:17:00Z", "flux": 1.17e-06, "energy": "0.1-0.8nm"}])", pts, 400);
  CHECK(n == 1 && pts[0].v > 1e-6f, "Xray mini n=%d", n);
  char c[8];
  wx_xray_class(1.17e-6f, c);  CHECK(!strcmp(c, "C1.2"), "Klasse %s", c);
  wx_xray_class(3.4e-5f, c);   CHECK(!strcmp(c, "M3.4"), "Klasse %s", c);
  wx_xray_class(2.0e-4f, c);   CHECK(!strcmp(c, "X2.0"), "Klasse %s", c);
  wx_xray_class(5.0e-9f, c);   CHECK(!strcmp(c, "A1.0"), "Klasse %s", c);

  std::vector<uint8_t> grid(AURORA_W * AURORA_H);
  OvationParser op(grid.data());
  const char *ov = R"({"Observation Time": "x", "coordinates": [[0, -90, 3], [359, 67, 42], [10, 90, 7]]})";
  size_t ovlen = strlen(ov);
  for (size_t i = 0; i < ovlen; i += 5) op.feed(ov + i, std::min<size_t>(5, ovlen - i));   // in kleinen Stuecken
  CHECK(op.count() == 3 && grid[0] == 3 && grid[(67 + 90) * AURORA_W + 359] == 42 && grid[180 * AURORA_W + 10] == 7,
        "Ovation mini count=%d", op.count());

  OvationParser op3(grid.data());
  const char *ovf = R"({"coordinates": [[5, 50, 12.75], [6.0, 51, 3]]})";
  op3.feed(ovf, strlen(ovf));
  CHECK(op3.count() == 2 && grid[140 * AURORA_W + 5] == 12 && grid[141 * AURORA_W + 6] == 3, "Ovation Dezimal count=%d", op3.count());

  if (argc > 1) {
    std::string d = argv[1];
    std::string s = slurp(d + "/noaa-planetary-k-index.json");
    n = wx_parse_kp(s.c_str(), pts, 64);
    printf("Kp: %d Werte, letzter %.2f\n", n, n ? pts[n - 1].v : -1);
    CHECK(n >= 40, "Kp echt n=%d", n);
    s = slurp(d + "/10cm-flux-30-day.json");
    n = wx_parse_flux(s.c_str(), pts, 64);
    printf("SFI: %d Werte, letzter %.0f\n", n, n ? pts[n - 1].v : -1);
    CHECK(n >= 20, "SFI echt n=%d", n);
    s = slurp(d + "/xrays-6-hour.json");
    n = wx_parse_xray(s.c_str(), pts, 400);
    wx_xray_class(n ? pts[n - 1].v : 0, c);
    printf("X-Ray: %d Werte, aktuell %s\n", n, c);
    CHECK(n >= 300, "Xray echt n=%d", n);
    s = slurp(d + "/alerts.json");
    static WxAlert al[8];
    n = wx_parse_alerts(s.c_str(), al, 8);
    printf("Meldungen: %d\n", n);
    for (int i = 0; i < n && i < 4; i++) printf("  %ld  %s\n", (long)al[i].t, al[i].title);
    CHECK(n >= 1, "Alerts n=%d", n);
    s = slurp(d + "/ovation_aurora_latest.json");
    OvationParser op2(grid.data());
    for (size_t i = 0; i < s.size(); i += 1460) op2.feed(s.data() + i, std::min<size_t>(1460, s.size() - i));
    int maxv = 0, cnt = 0;
    for (auto v : grid) { if (v > maxv) maxv = v; if (v >= 10) cnt++; }
    printf("Aurora: %d Punkte, Maximum %d %%, %d Zellen >= 10 %%\n", op2.count(), maxv, cnt);
    CHECK(op2.count() == AURORA_W * AURORA_H, "Ovation echt count=%d", op2.count());
    s = slurp(d + "/drap_global_frequencies.txt");
    std::vector<uint8_t> dg(DRAP_W * DRAP_H);
    bool ok = wx_parse_drap(s.c_str(), dg.data());
    int dmax = 0, south = 0;
    for (auto v : dg) if (v > dmax) dmax = v;
    for (int i = DRAP_W * DRAP_H / 2; i < DRAP_W * DRAP_H; i++) south += dg[i];
    CHECK(dg[(DRAP_H - 1) * DRAP_W] > 0 || south > 0, "DRAP Suedhalbkugel leer");
    printf("D-RAP: %s, Maximum %.1f MHz\n", ok ? "ok" : "FEHLER", dmax / 5.0);
    CHECK(ok, "DRAP");
  }
  printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
