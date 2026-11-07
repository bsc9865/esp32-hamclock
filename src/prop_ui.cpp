// Seite "Ausbreitung" (800x480)
//
//  [< Zurueck]  Ausbreitung (Schaetzung)        [jetzt][+2h][+4h][+8h][+12h]
//  Ziel            km   Ri.  MUF  LUF | 80 40 30 20 17 15 12 10
//  W1 USA Ost    6233  296   17   2   | ## ## ## ## .. .. .. ..
//  ...
//  MUF ueber JO50uh ... | naechste Ionosonden ... | Hinweis
#include "prop_ui.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "propdata.h"
#include "spacewx.h"
#include "datastore.h"
#include "settings.h"
#include "astro.h"
#include "ui_theme.h"

struct Target { const char *name; double lat, lon; };
static const Target TARGETS[] = {
  {"G   England", 51.5, -0.1},     {"W1  USA Ost", 41.7, -72.7},   {"W6  USA West", 37.8, -122.4},
  {"PY  Brasilien", -23.5, -46.6}, {"ZS  Suedafrika", -26.2, 28.0}, {"VU  Indien", 28.6, 77.2},
  {"JA  Japan", 35.7, 139.7},      {"VK  Australien", -33.9, 151.2}};
static constexpr int NT = sizeof TARGETS / sizeof TARGETS[0];
static const int OFFSETS_H[] = {0, 2, 4, 8, 12};
static constexpr int NO = sizeof OFFSETS_H / sizeof OFFSETS_H[0];

static lv_obj_t *scr, *scr_prev, *btn_off[NO], *l_info, *l_near, *l_time;
static lv_obj_t *l_dist[NT], *l_brg[NT], *l_muf[NT], *l_luf[NT], *cell[NT][PROP_BANDS];
static lv_timer_t *timer;
static int offset = 0;
static double qlat, qlon;
static uint32_t seen = 0xFFFFFFFF;

static lv_obj_t *mk_label(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c, const char *t = "") {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, t);
  return l;
}

static void refresh() {
  const PropModel *m = propdata_model();
  time_t t = time(nullptr) + OFFSETS_H[offset] * 3600;
  struct tm l;
  localtime_r(&t, &l);
  lv_label_set_text_fmt(l_time, "Prognose fuer %02d:%02d Uhr", l.tm_hour, l.tm_min);
  for (int i = 0; i < NO; i++) {
    if (i == offset) lv_obj_add_state(btn_off[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(btn_off[i], LV_STATE_CHECKED);
  }
  if (!m) {
    lv_label_set_text(l_info, "Noch keine Ionosonden-Daten (KC2G) ...");
    return;
  }
  SolarData sd = solar_get();
  float sfi = sd.valid ? sd.sfi : 120;
  const uint8_t *drap = offset == 0 ? spacewx_drap() : nullptr;   // D-RAP nur fuer "jetzt"
  static const lv_color_t st_col[3] = {lv_color_hex(0x2A3341), lv_color_hex(0xEAB308), lv_color_hex(0x22C55E)};
  for (int i = 0; i < NT; i++) {
    PathForecast pf;
    prop_path(*m, drap, sfi, qlat, qlon, TARGETS[i].lat, TARGETS[i].lon, t, pf);
    lv_label_set_text_fmt(l_dist[i], "%d", (int)pf.dist_km);
    lv_label_set_text_fmt(l_brg[i], "%d\xC2\xB0", (int)pf.bearing);
    lv_label_set_text_fmt(l_muf[i], "%d", (int)lroundf(pf.muf));
    lv_label_set_text_fmt(l_luf[i], "%d", (int)lroundf(pf.luf));
    for (int b = 0; b < PROP_BANDS; b++) lv_obj_set_style_bg_color(cell[i][b], st_col[pf.band[b]], 0);
  }
  float here = prop_muf(*m, qlat, qlon, t);
  const Ionosonde *st;
  int n = propdata_stations(&st);
  char info[128];
  snprintf(info, sizeof info, "MUF(3000) ueber %s: %.0f MHz   |   %d aktuelle Ionosonden (KC2G/GIRO)%s",
           g_settings.loc, here, n, offset == 0 ? "   |   inkl. D-RAP" : "");
  lv_label_set_text(l_info, info);
  // die drei naechsten Ionosonden
  int best[3] = {-1, -1, -1};
  double bd[3] = {1e9, 1e9, 1e9};
  for (int i = 0; i < n; i++) {
    double d = gc_distance_km(qlat, qlon, st[i].lat, st[i].lon);
    for (int k = 0; k < 3; k++)
      if (d < bd[k]) {
        for (int j = 2; j > k; j--) { bd[j] = bd[j - 1]; best[j] = best[j - 1]; }
        bd[k] = d; best[k] = i;
        break;
      }
  }
  char buf[200] = "Naechste Ionosonden: ";
  for (int k = 0; k < 3 && best[k] >= 0; k++) {
    const Ionosonde &s = st[best[k]];
    struct tm u;
    gmtime_r(&s.t, &u);
    char one[64];
    snprintf(one, sizeof one, "%s%.18s %.1f MHz (%.0f km, %02d:%02dZ)", k ? ",  " : "", s.name, s.mufd, bd[k], u.tm_hour, u.tm_min);
    strncat(buf, one, sizeof buf - strlen(buf) - 1);
  }
  lv_label_set_text(l_near, buf);
}

static void tick(lv_timer_t *) {
  uint32_t s = propdata_seq() + spacewx_seq();
  static time_t last = 0;
  time_t now = time(nullptr);
  if (s != seen || now - last >= 60) {
    seen = s;
    last = now;
    refresh();
  }
}

static void on_offset(lv_event_t *e) {
  offset = (int)(intptr_t)lv_event_get_user_data(e);
  refresh();
}

static void on_back(lv_event_t *) {
  lv_scr_load(scr_prev);
  lv_obj_del_async(scr);
}

static void on_delete(lv_event_t *) {
  lv_timer_del(timer);
  scr = nullptr;
}

void prop_ui_open() {
  if (scr) return;
  LatLon q;
  maidenhead_to_latlon(g_settings.loc, q);
  qlat = q.lat;
  qlon = q.lon;
  seen = 0xFFFFFFFF;
  scr_prev = lv_scr_act();
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, on_delete, LV_EVENT_DELETE, nullptr);

  lv_obj_t *b = lv_btn_create(scr);
  lv_obj_set_pos(b, 8, 6);
  lv_obj_set_size(b, 120, 40);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x374151), 0);
  lv_obj_add_event_cb(b, on_back, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = lv_label_create(b);
  lv_label_set_text(bl, LV_SYMBOL_LEFT "  Zurueck");
  lv_obj_center(bl);
  mk_label(scr, 142, 10, &lv_font_montserrat_28, C_ACCENT, "Ausbreitung");
  l_time = mk_label(scr, 142, 44, &lv_font_montserrat_14, C_DIM);

  static const char *names[NO] = {"jetzt", "+2 h", "+4 h", "+8 h", "+12 h"};
  for (int i = 0; i < NO; i++) {
    lv_obj_t *o = lv_btn_create(scr);
    lv_obj_set_pos(o, 432 + i * 72, 8);
    lv_obj_set_size(o, 66, 40);
    lv_obj_set_style_bg_color(o, C_PANE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x1E40AF), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(o, C_BORDER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_add_event_cb(o, on_offset, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *ol = lv_label_create(o);
    lv_label_set_text(ol, names[i]);
    lv_obj_center(ol);
    btn_off[i] = o;
  }

  // Tabelle
  lv_obj_t *p = lv_obj_create(scr);
  lv_obj_set_pos(p, 8, 68);
  lv_obj_set_size(p, 784, 340);
  lv_obj_set_style_bg_color(p, C_PANE, 0);
  lv_obj_set_style_border_color(p, C_BORDER, 0);
  lv_obj_set_style_pad_all(p, 8, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  const int X_DIST = 190, X_BRG = 260, X_MUF = 318, X_LUF = 370, X_BAND = 430, CW = 41;
  mk_label(p, 0, 0, &lv_font_montserrat_14, C_DIM, "Ziel");
  mk_label(p, X_DIST, 0, &lv_font_montserrat_14, C_DIM, "km");
  mk_label(p, X_BRG, 0, &lv_font_montserrat_14, C_DIM, "Ri.");
  mk_label(p, X_MUF, 0, &lv_font_montserrat_14, C_DIM, "MUF");
  mk_label(p, X_LUF, 0, &lv_font_montserrat_14, C_DIM, "LUF");
  for (int bnd = 0; bnd < PROP_BANDS; bnd++) {
    lv_obj_t *h = mk_label(p, X_BAND + bnd * CW, 0, &lv_font_montserrat_14, C_DIM, PROP_BAND_NAMES[bnd]);
    lv_obj_set_width(h, CW - 4);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  }
  for (int i = 0; i < NT; i++) {
    int y = 26 + i * 37;
    mk_label(p, 0, y + 6, &lv_font_montserrat_16, C_CALL, TARGETS[i].name);
    l_dist[i] = mk_label(p, X_DIST, y + 7, &lv_font_montserrat_14, C_TEXT);
    l_brg[i] = mk_label(p, X_BRG, y + 7, &lv_font_montserrat_14, C_TEXT);
    l_muf[i] = mk_label(p, X_MUF, y + 6, &lv_font_montserrat_16, C_TEXT);
    l_luf[i] = mk_label(p, X_LUF, y + 7, &lv_font_montserrat_14, C_DIM);
    for (int bnd = 0; bnd < PROP_BANDS; bnd++) {
      lv_obj_t *c = lv_obj_create(p);
      lv_obj_set_pos(c, X_BAND + bnd * CW, y + 2);
      lv_obj_set_size(c, CW - 4, 31);
      lv_obj_set_style_radius(c, 4, 0);
      lv_obj_set_style_border_width(c, 0, 0);
      lv_obj_set_style_bg_color(c, lv_color_hex(0x2A3341), 0);
      lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      cell[i][bnd] = c;
    }
  }

  l_info = mk_label(scr, 12, 414, &lv_font_montserrat_14, C_TEXT);
  l_near = mk_label(scr, 12, 434, &lv_font_montserrat_12, C_DIM);
  lv_obj_set_width(l_near, 776);
  lv_label_set_long_mode(l_near, LV_LABEL_LONG_DOT);
  lv_obj_t *hint = mk_label(scr, 12, 454, &lv_font_montserrat_12, C_DIM,
                            "Gruen = offen, Gelb = knapp, Grau = zu.  Grobe Schaetzung aus Ionosonden + Tagesgang "
                            "(mittl. Fehler ca. 4 MHz), kein VOACAP.");
  lv_obj_set_width(hint, 776);

  lv_scr_load(scr);
  refresh();
  timer = lv_timer_create(tick, 2000, nullptr);
}
