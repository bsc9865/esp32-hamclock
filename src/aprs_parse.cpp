#include "aprs_parse.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

const uint32_t APRS_COLORS[APRS_CATS] = {0xF97316, 0x60A5FA, 0x22C55E, 0x22D3EE, 0xA1A1AA};
const char *const APRS_CAT_NAMES[APRS_CATS] = {"mobil", "fest", "Digi/Gateway", "Wetter", "sonstige"};

AprsCat aprs_category(char table, char code) {
  (void)table;
  switch (code) {
    case '>': case 'k': case 'b': case '[': case 'v': case 'j': case 'U': case 'u': case '<': case 'R':
    case 's': case 'Y': case 'X': case '^': case '\'': case 'O': case 'f': case 'a': case 'p':
      return APRS_MOBILE;
    case '-': case 'y': case 'h': case 'K':
      return APRS_FIXED;
    case '#': case '&': case 'r': case 'D':
      return APRS_DIGI;
    case '_': case 'W': case '@':
      return APRS_WX;
    default:
      return APRS_OTHER;
  }
}

static void copy_ascii(char *dst, size_t n, const char *s) {
  size_t o = 0;
  while (*s == ' ') s++;
  for (; *s && o < n - 1; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '\r' || c == '\n') break;
    if (c >= 0x20 && c < 0x7F) dst[o++] = (char)c;
  }
  while (o > 0 && dst[o - 1] == ' ') o--;
  dst[o] = 0;
}

// "DDMM.hhN" bzw. "DDDMM.hhE" (Leerzeichen = Positionsunschaerfe -> als 0 behandeln)
static bool parse_coord(const char *s, int deg_digits, char pos_ch, char neg_ch, float &out) {
  char buf[12];
  int len = deg_digits + 5;
  for (int i = 0; i < len; i++) {
    char c = s[i];
    if (c == ' ') c = '0';
    if (i == deg_digits + 2) { if (c != '.') return false; }
    else if (!isdigit((unsigned char)c)) return false;
    buf[i] = c;
  }
  buf[len] = 0;
  char h = s[len];
  if (h != pos_ch && h != neg_ch) return false;
  char d[4] = {0};
  memcpy(d, buf, deg_digits);
  float v = atoi(d) + (float)atof(buf + deg_digits) / 60.0f;
  out = h == neg_ch ? -v : v;
  return true;
}

// Position ab p (nach Typzeichen bzw. Zeitstempel); liefert Zeiger auf den Rest oder nullptr
static const char *parse_position(const char *p, AprsPos &o) {
  if (isdigit((unsigned char)p[0]) || p[0] == ' ') {
    // unkomprimiert: 4903.50N/07201.75W-
    if (strlen(p) < 19) return nullptr;
    if (!parse_coord(p, 2, 'N', 'S', o.lat)) return nullptr;
    o.sym_table = p[8];
    if (!parse_coord(p + 9, 3, 'E', 'W', o.lon)) return nullptr;
    o.sym_code = p[18];
    return p + 19;
  }
  // komprimiert: Tabelle + 4 Zeichen Breite + 4 Zeichen Laenge (Base91) + Symbol + 3 Zeichen
  if (strlen(p) < 10) return nullptr;
  long y = 0, x = 0;
  for (int i = 1; i <= 4; i++) {
    int a = (unsigned char)p[i] - 33, b = (unsigned char)p[i + 4] - 33;
    if (a < 0 || a > 90 || b < 0 || b > 90) return nullptr;
    y = y * 91 + a;
    x = x * 91 + b;
  }
  o.sym_table = p[0];
  o.lat = 90.0f - y / 380926.0f;
  o.lon = -180.0f + x / 190463.0f;
  o.sym_code = p[9];
  return strlen(p) >= 13 ? p + 13 : p + 10;
}

// Mic-E: Breite aus dem Zielrufzeichen, Laenge aus den ersten Info-Bytes
static bool parse_mice(const char *dest, const char *info, AprsPos &o) {
  if (strlen(dest) < 6 || strlen(info) < 9) return false;
  int dig[6];
  bool north = false, lon100 = false, west = false;
  for (int i = 0; i < 6; i++) {
    char c = dest[i];
    int d;
    bool flag;   // "P..Z"-Bereich
    if (c >= '0' && c <= '9') { d = c - '0'; flag = false; }
    else if (c >= 'A' && c <= 'J') { d = c - 'A'; flag = false; }
    else if (c >= 'P' && c <= 'Y') { d = c - 'P'; flag = true; }
    else if (c == 'K' || c == 'L' || c == 'Z') { d = 0; flag = (c == 'Z'); }
    else return false;
    dig[i] = d;
    if (i == 3) north = flag;
    if (i == 4) lon100 = flag;
    if (i == 5) west = flag;
  }
  float lat = dig[0] * 10 + dig[1] + (dig[2] * 10 + dig[3] + (dig[4] * 10 + dig[5]) / 100.0f) / 60.0f;
  int d = (unsigned char)info[1] - 28;
  if (lon100) d += 100;
  if (d >= 180 && d <= 189) d -= 80;
  else if (d >= 190 && d <= 199) d -= 190;
  int m = (unsigned char)info[2] - 28;
  if (m >= 60) m -= 60;
  int h = (unsigned char)info[3] - 28;
  if (d < 0 || d > 180 || m < 0 || m > 59 || h < 0 || h > 99) return false;
  float lon = d + (m + h / 100.0f) / 60.0f;
  o.lat = north ? lat : -lat;
  o.lon = west ? -lon : lon;
  o.sym_code = info[7];
  o.sym_table = info[8];
  copy_ascii(o.comment, sizeof o.comment, info + 9);
  return true;
}

bool aprs_parse(const char *line, AprsPos &o) {
  if (!line || line[0] == '#') return false;
  const char *gt = strchr(line, '>');
  const char *colon = strchr(line, ':');
  if (!gt || !colon || gt > colon || gt - line > 11 || gt == line) return false;
  memset(&o, 0, sizeof o);
  memcpy(o.call, line, gt - line);
  o.call[gt - line] = 0;
  // Zielrufzeichen (bis ',' oder ':')
  char dest[10] = {0};
  const char *de = gt + 1;
  size_t dl = strcspn(de, ",:");
  if (dl >= sizeof dest) dl = sizeof dest - 1;
  memcpy(dest, de, dl);

  const char *p = colon + 1;
  const char *rest = nullptr;
  switch (p[0]) {
    case '!': case '=':
      rest = parse_position(p + 1, o);
      break;
    case '/': case '@':
      if (strlen(p) < 8) return false;
      rest = parse_position(p + 8, o);   // 7 Zeichen Zeitstempel
      break;
    case '`': case '\'':
      if (!parse_mice(dest, p, o)) return false;
      return fabsf(o.lat) <= 90 && fabsf(o.lon) <= 180;
    case ';': {
      // Objekt: ;NAME_____*DDHHMMzPOSITION
      if (strlen(p) < 18 + 19 - 1 || (p[10] != '*' && p[10] != '_')) return false;
      if (p[10] == '_') return false;   // geloeschtes Objekt
      copy_ascii(o.call, sizeof o.call, p + 1);
      o.call[9 < sizeof o.call ? 9 : sizeof o.call - 1] = 0;
      for (int i = (int)strlen(o.call) - 1; i >= 0 && o.call[i] == ' '; i--) o.call[i] = 0;
      o.object = true;
      rest = parse_position(p + 18, o);
      break;
    }
    default:
      return false;
  }
  if (!rest) return false;
  if (fabsf(o.lat) > 90 || fabsf(o.lon) > 180 || (o.lat == 0 && o.lon == 0)) return false;
  copy_ascii(o.comment, sizeof o.comment, rest);
  return true;
}
