#pragma once
// Minimaler JSON-Scanner fuer flache Objekt-Arrays (ohne Speicher-Allokation, auch auf dem PC testbar)
#include <string.h>
#include <type_traits>

// Sucht im Bereich [p, end) das naechste "key": und liefert den Zeiger hinter den Doppelpunkt
// (Leerzeichen uebersprungen) oder nullptr
static inline const char *json_find_key(const char *p, const char *end, const char *key) {
  size_t kl = strlen(key);
  for (const char *q = p; q && q < end; q++) {
    q = strchr(q, '"');
    if (!q || q >= end) return nullptr;
    if (strncmp(q + 1, key, kl) == 0 && q[1 + kl] == '"') {
      const char *v = q + 2 + kl;
      while (*v == ' ' || *v == ':') v++;
      return v;
    }
  }
  return nullptr;
}

// Ruft f(begin, end) fuer jedes Top-Level-Objekt {...} auf (verschachtelte Objekte und Strings werden
// beachtet); liefert f ein bool, bricht false die Schleife ab
template <typename F>
static inline void json_for_each_object(const char *json, F f) {
  const char *p = json;
  while ((p = strchr(p, '{'))) {
    const char *e = p + 1;
    bool in_str = false;
    int depth = 1;
    for (; *e && depth > 0; e++) {
      if (*e == '\\' && in_str) { if (e[1]) e++; continue; }
      if (*e == '"') in_str = !in_str;
      else if (!in_str && *e == '{') depth++;
      else if (!in_str && *e == '}') depth--;
    }
    if (depth != 0) return;
    if constexpr (std::is_same_v<decltype(f(p, e)), bool>) {
      if (!f(p, e)) return;
    } else {
      f(p, e);
    }
    p = e;
  }
}
