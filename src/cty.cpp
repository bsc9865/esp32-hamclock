#include "cty.h"
#include <string.h>
#include <ctype.h>

static const CtyKey *find(const CtyKey *tab, int n, const char *key) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(tab[mid].key, key);
    if (c == 0) return &tab[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

// Suffixe, die das Land nicht aendern
static bool is_modifier(const char *s) {
  static const char *mods[] = {"P", "M", "MM", "AM", "QRP", "A", "B", "LH", "J", "R", "T"};
  for (auto m : mods) if (strcmp(s, m) == 0) return true;
  return strlen(s) == 1 && isdigit((unsigned char)s[0]);
}

CtyResult cty_lookup(const char *call_in) {
  char call[24];
  size_t n = 0;
  for (; call_in[n] && n < sizeof call - 1; n++) call[n] = toupper((unsigned char)call_in[n]);
  call[n] = 0;
  // Skimmer-Endungen wie "-#" oder "-2" entfernen
  if (char *d = strchr(call, '-')) *d = 0;

  // 1. Exakte Rufzeichen (Sonderfaelle aus cty.dat)
  if (const CtyKey *k = find(cty_exact, cty_exact_count, call))
    return {&cty_entities[k->ent], k->cq};

  // 2. Rufzeichen mit "/": Praefix-Teil bestimmen
  char base[24];
  strcpy(base, call);
  if (char *slash = strchr(call, '/')) {
    *slash = 0;
    const char *a = call, *b = slash + 1;
    if (char *s2 = strchr((char *)b, '/')) *s2 = 0;  // DL/DJ0SB/P -> nur die ersten beiden Teile
    if (is_modifier(b)) strcpy(base, a);
    else if (is_modifier(a)) strcpy(base, b);
    else strcpy(base, strlen(a) <= strlen(b) ? a : b);   // kuerzerer Teil = Praefix
  }

  // 3. Laengster passender Praefix
  char key[24];
  for (size_t len = strlen(base); len > 0; len--) {
    memcpy(key, base, len);
    key[len] = 0;
    if (const CtyKey *k = find(cty_prefixes, cty_prefix_count, key))
      return {&cty_entities[k->ent], k->cq};
  }
  return {nullptr, 0};
}
