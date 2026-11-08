#include "spacewx_parse.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_scan.h"
#include "timeutil.h"

static time_t str_time(const char *v) { return v && *v == '"' ? parse_iso_utc(v + 1) : 0; }

static int parse_series(const char *json, const char *vkey, WxPoint *out, int max) {
  int n = 0;
  json_for_each_object(json, [&](const char *b, const char *e) {
    if (n >= max) return;
    const char *t = json_find_key(b, e, "time_tag");
    const char *v = json_find_key(b, e, vkey);
    if (!t || !v || *v == 'n') return;   // null
    out[n].t = str_time(t);
    out[n].v = (float)atof(*v == '"' ? v + 1 : v);
    if (out[n].t) n++;
  });
  return n;
}

int wx_parse_kp(const char *json, WxPoint *out, int max) { return parse_series(json, "Kp", out, max); }
int wx_parse_flux(const char *json, WxPoint *out, int max) { return parse_series(json, "flux", out, max); }

int wx_parse_xray(const char *json, WxPoint *out, int max) {
  int n = 0;
  json_for_each_object(json, [&](const char *b, const char *e) {
    if (n >= max) return;
    const char *en = json_find_key(b, e, "energy");
    if (!en || strncmp(en, "\"0.1-0.8nm\"", 11) != 0) return;
    const char *t = json_find_key(b, e, "time_tag");
    const char *v = json_find_key(b, e, "flux");
    if (!t || !v || *v == 'n') return;
    out[n].t = str_time(t);
    out[n].v = (float)atof(v);
    if (out[n].t && out[n].v > 0) n++;
  });
  return n;
}

void wx_xray_class(float f, char out[8]) {
  static const char cls[] = "ABCMX";
  int i = 0;
  float base = 1e-8f;
  while (i < 4 && f >= base * 10) { base *= 10; i++; }
  float m = f / base;
  if (m < 1) m = 1;
  snprintf(out, 8, "%c%.1f", cls[i], m);
}

// JSON-String ab '"' dekodieren: \n, \r, \/ , \" ; nur ASCII
static void json_unescape(const char *v, char *out, size_t n) {
  size_t o = 0;
  if (*v == '"') v++;
  for (; *v && *v != '"' && o < n - 1; v++) {
    if (*v == '\\' && v[1]) {
      v++;
      char c = *v;
      if (c == 'n') out[o++] = '\n';
      else if (c == 'r') continue;
      else if (c == 't') out[o++] = ' ';
      else if (c == 'u') { v += 4; out[o++] = '?'; }
      else out[o++] = c;
    } else if ((unsigned char)*v < 0x80) {
      out[o++] = *v;
    }
  }
  out[o] = 0;
}

int wx_parse_alerts(const char *json, WxAlert *out, int max) {
  static const char *heads[] = {"ALERT:", "WARNING:", "WATCH:", "SUMMARY:", "EXTENDED WARNING:",
                                "CONTINUED ALERT:", "CANCEL WARNING:", "CANCEL WATCH:", "CANCEL SUMMARY:"};
  int n = 0;
  json_for_each_object(json, [&](const char *b, const char *e) {
    if (n >= max) return;
    const char *t = json_find_key(b, e, "issue_datetime");
    const char *m = json_find_key(b, e, "message");
    if (!t || !m) return;
    WxAlert &a = out[n];
    a.t = str_time(t);
    json_unescape(m, a.text, sizeof a.text);
    a.title[0] = 0;
    // Titelzeile suchen
    for (const char *line = a.text; line && *line; ) {
      const char *nl = strchr(line, '\n');
      size_t len = nl ? (size_t)(nl - line) : strlen(line);
      for (auto h : heads)
        if (strncmp(line, h, strlen(h)) == 0) {
          size_t l = len < sizeof a.title - 1 ? len : sizeof a.title - 1;
          memcpy(a.title, line, l);
          a.title[l] = 0;
          while (l > 0 && a.title[l - 1] == ' ') a.title[--l] = 0;
          break;
        }
      if (a.title[0]) break;
      line = nl ? nl + 1 : nullptr;
    }
    if (!a.title[0]) snprintf(a.title, sizeof a.title, "Meldung");
    if (a.t) n++;
  });
  // neueste zuerst
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (out[j].t > out[i].t) { WxAlert tmp = out[i]; out[i] = out[j]; out[j] = tmp; }
  return n;
}

// ---------- OVATION (Stream) ----------
OvationParser::OvationParser(uint8_t *grid) : grid_(grid) {
  memset(grid_, 0, AURORA_W * AURORA_H);
  key_[0] = 0;
}

void OvationParser::feed(const char *d, size_t len) {
  for (size_t i = 0; i < len; i++) {
    char c = d[i];
    if (!in_coords_) {
      // nach dem Schluessel "coordinates" suchen
      if (c == '"') {
        key_[key_len_] = 0;
        if (key_len_ > 0 && strcmp(key_, "coordinates") == 0) in_coords_ = true;
        key_len_ = 0;
      } else if (key_len_ < (int)sizeof key_ - 1 && isalpha((unsigned char)c)) {
        key_[key_len_++] = c;
      } else {
        key_len_ = 0;
      }
      continue;
    }
    if (c == '-' && !in_num_) { sign_ = -1; in_num_ = true; num_ = 0; frac_ = false; continue; }
    if (isdigit((unsigned char)c)) {
      if (!in_num_) { in_num_ = true; num_ = 0; sign_ = 1; frac_ = false; }
      if (!frac_) num_ = num_ * 10 + (c - '0');
      continue;
    }
    if (c == '.' && in_num_) { frac_ = true; continue; }
    if (in_num_) {
      trip_[idx_++] = sign_ * num_;
      in_num_ = false;
      if (idx_ == 3) {
        int lon = trip_[0], lat = trip_[1], v = trip_[2];
        if (lon >= 0 && lon < AURORA_W && lat >= -90 && lat <= 90)
          grid_[(lat + 90) * AURORA_W + lon] = (uint8_t)(v > 100 ? 100 : (v < 0 ? 0 : v));
        idx_ = 0;
        n_++;
      }
    }
  }
}

// ---------- D-RAP ----------
bool wx_parse_drap(const char *text, uint8_t *grid) {
  memset(grid, 0, DRAP_W * DRAP_H);
  int rows = 0;
  for (const char *line = text; line && *line; ) {
    const char *nl = strchr(line, '\n');
    // Datenzeilen: " 89 |  0.0  0.0 ..."
    const char *bar = strchr(line, '|');
    if (bar && (!nl || bar < nl) && line[0] != '#') {
      int lat = atoi(line);
      int row = (89 - lat) / 2;
      if (row >= 0 && row < DRAP_H) {
        const char *p = bar + 1;
        for (int col = 0; col < DRAP_W; col++) {
          char *end;
          double v = strtod(p, &end);
          if (end == p || (nl && end > nl)) break;
          int q = (int)lround(v * 5);
          grid[row * DRAP_W + col] = (uint8_t)(q > 255 ? 255 : (q < 0 ? 0 : q));
          p = end;
        }
        rows++;
      }
    }
    line = nl ? nl + 1 : nullptr;
  }
  return rows >= DRAP_H - 2;   // alle 90 Breitenzeilen
}
