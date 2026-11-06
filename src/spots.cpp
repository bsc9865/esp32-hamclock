#include "spots.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cty.h"

const char *const BAND_NAMES[BAND_COUNT] = {
  "160", "80", "60", "40", "30", "20", "17", "15", "12", "10", "6", "4", "2", "70cm", "?"};

const uint32_t BAND_COLORS[BAND_COUNT] = {
  0xDC2626, 0xF97316, 0xFB923C, 0xFACC15, 0xA3E635, 0x22C55E, 0x2DD4BF,
  0x38BDF8, 0x818CF8, 0xE879F9, 0xF472B6, 0xFDA4AF, 0xFFFFFF, 0xD1D5DB, 0x9CA3AF};

struct BandRange { double lo, hi, cw_top, digi_top; };
// Grenzen in kHz; cw_top/digi_top: grobe Bandplan-Einteilung fuer die Mode-Schaetzung
static const BandRange RANGES[] = {
  {1800, 2000, 1838, 1843},     {3500, 4000, 3570, 3600},     {5250, 5450, 5354, 5366},
  {7000, 7300, 7040, 7060},     {10100, 10150, 10130, 10150}, {14000, 14350, 14070, 14100},
  {18068, 18168, 18095, 18110}, {21000, 21450, 21070, 21150}, {24890, 24990, 24915, 24940},
  {28000, 29700, 28070, 28300}, {50000, 54000, 50100, 50320}, {70000, 71000, 70100, 70200},
  {144000, 148000, 144150, 144400}, {420000, 450000, 432150, 432500},
};

Band band_from_khz(double khz) {
  for (int i = 0; i < (int)(sizeof RANGES / sizeof RANGES[0]); i++)
    if (khz >= RANGES[i].lo && khz <= RANGES[i].hi) return (Band)i;
  return B_OTHER;
}

static bool has_word(const char *s, const char *w) {
  size_t n = strlen(w);
  for (const char *p = s; (p = strstr(p, w)); p++) {
    bool start = p == s || !isalnum((unsigned char)p[-1]);
    bool end = !isalnum((unsigned char)p[n]);
    if (start && end) return true;
  }
  return false;
}

const char *mode_guess(double khz, const char *comment) {
  char up[40];
  size_t i = 0;
  for (; comment[i] && i < sizeof up - 1; i++) up[i] = toupper((unsigned char)comment[i]);
  up[i] = 0;
  static const char *modes[] = {"FT8", "FT4", "CW", "SSB", "USB", "LSB", "RTTY", "PSK31", "PSK", "JS8", "FM", "AM", "SSTV"};
  for (auto m : modes)
    if (has_word(up, m)) return (!strcmp(m, "USB") || !strcmp(m, "LSB")) ? "SSB" : (!strcmp(m, "PSK31") ? "PSK" : m);
  if (strstr(up, "WPM")) return "CW";        // RBN/Skimmer
  Band b = band_from_khz(khz);
  if (b == B_OTHER) return "";
  const BandRange &r = RANGES[b];
  if (khz < r.cw_top) return "CW";
  if (khz < r.digi_top) return "DIGI";
  return "SSB";
}

static void copy_tok(char *dst, size_t n, const char *s, size_t len) {
  if (len >= n) len = n - 1;
  memcpy(dst, s, len);
  dst[len] = 0;
}

bool spot_parse(const char *line, time_t now, Spot &out) {
  if (strncmp(line, "DX de ", 6) != 0) return false;
  const char *p = line + 6;
  const char *colon = strchr(p, ':');
  if (!colon || colon - p > 15) return false;
  Spot s;
  copy_tok(s.spotter, sizeof s.spotter, p, colon - p);
  p = colon + 1;

  // Frequenz
  char *end;
  s.khz = strtod(p, &end);
  if (end == p || s.khz < 100) return false;
  p = end;
  while (*p == ' ') p++;

  // DX-Rufzeichen
  const char *c = p;
  while (*p && *p != ' ') p++;
  if (p == c) return false;
  copy_tok(s.dx, sizeof s.dx, c, p - c);
  while (*p == ' ') p++;

  // Zeit: letztes Token der Form HHMMZ (danach evtl. noch ein Locator)
  const char *tz = nullptr;
  for (const char *q = p; *q; q++)
    if (isdigit((unsigned char)q[0]) && isdigit((unsigned char)q[1]) && isdigit((unsigned char)q[2]) &&
        isdigit((unsigned char)q[3]) && q[4] == 'Z' && (q == p || q[-1] == ' '))
      tz = q;
  const char *cend = tz ? tz : p + strlen(p);
  while (cend > p && cend[-1] == ' ') cend--;
  copy_tok(s.comment, sizeof s.comment, p, cend - p);

  struct tm u;
  gmtime_r(&now, &u);
  if (tz) {
    int hh = (tz[0] - '0') * 10 + (tz[1] - '0');
    int mm = (tz[2] - '0') * 10 + (tz[3] - '0');
    time_t day0 = now - (u.tm_hour * 3600 + u.tm_min * 60 + u.tm_sec);
    s.t = day0 + hh * 3600 + mm * 60;
    if (s.t > now + 3600) s.t -= 86400;   // Spot von gestern (kurz nach Mitternacht)
  } else {
    s.t = now;
  }

  s.band = band_from_khz(s.khz);
  strncpy(s.mode, mode_guess(s.khz, s.comment), sizeof s.mode - 1);
  CtyResult r = cty_lookup(s.dx);
  if (r.ent) {
    s.ent = (int16_t)(r.ent - cty_entities);
    s.cq = r.cq;
    s.lat = r.ent->lat;
    s.lon = r.ent->lon;
  }
  out = s;
  return true;
}
