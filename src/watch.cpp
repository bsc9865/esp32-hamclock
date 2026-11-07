#include "watch.h"
#include <ctype.h>
#include <string.h>

static bool ieq_prefix(const char *s, const char *p, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (!s[i] || toupper((unsigned char)s[i]) != toupper((unsigned char)p[i])) return false;
  return true;
}

static bool icontains(const char *hay, const char *needle, size_t n) {
  if (!n) return false;
  for (const char *h = hay; *h; h++)
    if (ieq_prefix(h, needle, n)) return true;
  return false;
}

// Grundrufzeichen ohne Zusaetze: "EA8/DJ0SB/P" -> laengster Teil "DJ0SB"
static void base_call(const char *call, char *out, size_t n) {
  const char *best = call;
  size_t best_len = 0;
  const char *p = call;
  while (*p) {
    size_t len = strcspn(p, "/");
    if (len > best_len) { best = p; best_len = len; }
    p += len;
    if (*p == '/') p++;
  }
  if (best_len >= n) best_len = n - 1;
  for (size_t i = 0; i < best_len; i++) out[i] = (char)toupper((unsigned char)best[i]);
  out[best_len] = 0;
}

bool watch_match(const char *list, const char *call, const CtyEntity *ent) {
  if (!list || !*list || !call || !*call) return false;
  char base[16], full[24];
  base_call(call, base, sizeof base);
  size_t fl = strlen(call) < sizeof full - 1 ? strlen(call) : sizeof full - 1;
  for (size_t i = 0; i < fl; i++) full[i] = (char)toupper((unsigned char)call[i]);
  full[fl] = 0;
  if (char *d = strchr(full, '-')) *d = 0;   // Skimmer-Endung

  const char *p = list;
  while (*p) {
    while (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r' || *p == ';') p++;
    if (!*p) break;
    const char *e = p;
    // Eintrag bis zum naechsten Trenner (bei '@' sind Leerzeichen im Namen erlaubt, Ende bei , ; \n)
    if (*p == '@') while (*e && *e != ',' && *e != ';' && *e != '\n' && *e != '\r') e++;
    else while (*e && *e != ',' && *e != ' ' && *e != ';' && *e != '\n' && *e != '\r') e++;
    size_t n = (size_t)(e - p);
    while (n > 0 && p[n - 1] == ' ') n--;

    if (*p == '@') {
      if (ent && n > 1) {
        const char *q = p + 1;
        size_t qn = n - 1;
        if ((strlen(ent->prefix) == qn && ieq_prefix(ent->prefix, q, qn)) || icontains(ent->name, q, qn)) return true;
      }
    } else if (n > 1 && p[n - 1] == '*') {
      if (ieq_prefix(full, p, n - 1) || ieq_prefix(base, p, n - 1)) return true;
    } else if (n > 0) {
      if ((strlen(base) == n && ieq_prefix(base, p, n)) || (strlen(full) == n && ieq_prefix(full, p, n))) return true;
    }
    p = e;
  }
  return false;
}

uint8_t mode_class(const char *m) {
  if (!m || !*m) return MODE_OTHER;
  if (!strcmp(m, "CW")) return MODE_CW;
  if (!strcmp(m, "SSB") || !strcmp(m, "AM") || !strcmp(m, "FM")) return MODE_SSB;
  if (!strcmp(m, "FT8") || !strcmp(m, "FT4") || !strcmp(m, "RTTY") || !strcmp(m, "PSK") || !strcmp(m, "JS8") ||
      !strcmp(m, "DIGI") || !strcmp(m, "SSTV") || !strcmp(m, "DATA") || !strcmp(m, "MFSK") || !strcmp(m, "OLIVIA"))
    return MODE_DIGI;
  return MODE_OTHER;
}
