// Seite "Wetter am QTH" (800x480)
//
//  [< Zurueck]  Wetter JO50uh                                         Stand 10:15
//  +-- Jetzt --------------------+  +-- Naechste 12 Stunden -------------------------+
//  |  (Symbol)  1.3 °C            |  |  11   12   13  ...   (Uhrzeit)                 |
//  |            bedeckt           |  |  (Symbole)                                     |
//  |  Gefuehlt   Luftfeuchte      |  |  Temperaturkurve mit Werten                    |
//  |  Wind       Luftdruck        |  |  Balken Regenwahrscheinlichkeit                |
//  +------------------------------+  +------------------------------------------------+
//  +-- 3 Tage: Karten mit Symbol, Hoechst-/Tiefstwert, Regenwahrscheinlichkeit ---------+
//  Daten: Open-Meteo.com (CC BY 4.0)
#include "weather_ui.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "astro.h"
#include "weather.h"
#include "settings.h"
#include "ui_theme.h"

static constexpr int NH = 12, ND = 3;
static constexpr int COL_W = 38;                    // Spaltenbreite im 12-Stunden-Panel
static constexpr int CH_Y = 86, CH_H = 58;          // Temperaturkurve
static constexpr int BAR_BASE = 200, BAR_H = 34;    // Regenbalken
static constexpr uint32_t COL_SUN = 0xFACC15, COL_MOON = 0xE2E8F0, COL_CLOUD = 0xCBD5E1, COL_CLOUD_DARK = 0x8C99AB,
                          COL_RAIN = 0x60A5FA, COL_MAX = 0xFB923C, COL_MIN = 0x60A5FA,
                          COL_PANE = 0x131A24, COL_NIGHT = 0x0B1119;

static lv_obj_t *scr, *scr_prev, *l_stand, *now_icon, *l_now_t, *l_now_d, *l_warn;
static lv_obj_t *l_det_v[6];
static lv_obj_t *h_night[NH], *h_time[NH], *h_icon[NH], *h_temp[NH], *h_bar[NH], *h_pop[NH], *chart;
static lv_chart_series_t *ser;
static lv_obj_t *d_name[ND], *d_icon[ND], *d_max[ND], *d_min[ND], *d_text[ND], *d_pop[ND];
static lv_timer_t *timer;
static uint32_t seen = 0xFFFFFFFF;
static double qlat, qlon;
static const char *WD[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

// ---------- Wettersymbole aus einfachen Formen ----------
static lv_obj_t *shape(lv_obj_t *p, int x, int y, int w, int h, uint32_t col, bool round = true) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, round ? LV_RADIUS_CIRCLE : 1, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void sun(lv_obj_t *p, int x, int y, int d) {
  lv_obj_t *o = shape(p, x, y, d, d, COL_SUN);
  lv_obj_set_style_shadow_color(o, lv_color_hex(0xF59E0B), 0);
  lv_obj_set_style_shadow_width(o, d / 2, 0);
  lv_obj_set_style_shadow_opa(o, LV_OPA_50, 0);
}

static void moon(lv_obj_t *p, int x, int y, int d, uint32_t bg) {
  shape(p, x, y, d, d, COL_MOON);
  shape(p, x + d / 3, y - d / 8, d, d, bg);   // Sichel: Hintergrundfarbe darueber
}

// Wolke im Rechteck (x, y, w, w*0.6)
static void cloud(lv_obj_t *p, int x, int y, int w, uint32_t col) {
  int h = w * 6 / 10;
  shape(p, x, y + h * 45 / 100, w, h * 55 / 100, col);
  shape(p, x + w * 12 / 100, y + h * 8 / 100, h * 75 / 100, h * 75 / 100, col);
  shape(p, x + w * 48 / 100, y + h * 28 / 100, h * 58 / 100, h * 58 / 100, col);
}

// Blitz als Linienzug; lv_line braucht dauerhafte Punkte -> je Symbolgroesse ein Satz
static lv_point_t *bolt_points(int s) {
  static int sizes[4] = {0};
  static lv_point_t pts[4][4];
  for (int i = 0; i < 4; i++) {
    if (sizes[i] == s) return pts[i];
    if (!sizes[i]) {
      sizes[i] = s;
      pts[i][0] = {(lv_coord_t)(s * 56 / 100), (lv_coord_t)(s * 50 / 100)};
      pts[i][1] = {(lv_coord_t)(s * 40 / 100), (lv_coord_t)(s * 76 / 100)};
      pts[i][2] = {(lv_coord_t)(s * 56 / 100), (lv_coord_t)(s * 76 / 100)};
      pts[i][3] = {(lv_coord_t)(s * 42 / 100), (lv_coord_t)(s * 100 / 100)};
      return pts[i];
    }
  }
  return pts[3];
}

// Zeichnet das Symbol fuer einen WMO-Wettercode in box (Groesse s x s)
static void draw_icon(lv_obj_t *box, int s, uint8_t code, bool day, uint32_t bg = COL_PANE) {
  lv_obj_clean(box);
  auto light = [&](int x, int y, int d) { if (day) sun(box, x, y, d); else moon(box, x, y, d, bg); };
  int cw = s * 92 / 100, cx = (s - cw) / 2, cy = s * 22 / 100;
  if (code == 0) {
    light(s * 18 / 100, s * 18 / 100, s * 64 / 100);
  } else if (code <= 2) {
    light(s * 8 / 100, s * 4 / 100, s * 50 / 100);
    cloud(box, s * 22 / 100, s * 34 / 100, s * 74 / 100, COL_CLOUD);
  } else if (code == 3) {
    cloud(box, s * 2 / 100, s * 10 / 100, s * 70 / 100, COL_CLOUD_DARK);
    cloud(box, s * 22 / 100, s * 30 / 100, s * 76 / 100, COL_CLOUD);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 3; i++)
      shape(box, s * (10 + 8 * (i % 2)) / 100, s * (28 + 20 * i) / 100, s * 72 / 100, s / 10 + 1, COL_CLOUD);
  } else {
    bool thunder = code >= 95;
    bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    bool drizzle = code >= 51 && code <= 57;
    cloud(box, cx, s * 4 / 100, cw, thunder || !drizzle ? COL_CLOUD_DARK : COL_CLOUD);
    (void)cy;
    if (thunder) {
      lv_obj_t *l = lv_line_create(box);
      lv_line_set_points(l, bolt_points(s), 4);
      lv_obj_set_style_line_color(l, lv_color_hex(COL_SUN), 0);
      lv_obj_set_style_line_width(l, s >= 40 ? 4 : 2, 0);
      lv_obj_set_style_line_rounded(l, true, 0);
    } else {
      for (int i = 0; i < 3; i++) {
        int x = s * (24 + 24 * i) / 100, y = s * (66 + 8 * (i % 2)) / 100;
        if (snow) { int d = s / 8 + 1; shape(box, x - d / 2, y, d, d, 0xFFFFFF); }
        else if (drizzle) { int d = s / 11 + 1; shape(box, x - d / 2, y + 2, d, d, COL_RAIN); }
        else shape(box, x - 1, y, s >= 40 ? 3 : 2, s * 22 / 100, COL_RAIN, false);
      }
    }
  }
}

static lv_obj_t *icon_box(lv_obj_t *p, int x, int y, int s) {
  lv_obj_t *b = lv_obj_create(p);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, s, s);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

// ganze Grad ohne "-0"
static void fmt_deg(char *b, size_t n, float v) {
  long r = lroundf(v);
  snprintf(b, n, "%ld\xC2\xB0", r == 0 ? 0L : r);
}

static bool is_day(time_t t) {
  double alt, az;
  sun_alt_az(t, qlat, qlon, alt, az);
  return alt > -0.833;
}

// ---------- Aufbau ----------
static lv_obj_t *panel(int x, int y, int w, int h, const char *title) {
  lv_obj_t *p = lv_obj_create(scr);
  lv_obj_set_pos(p, x, y);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_bg_color(p, C_PANE, 0);
  lv_obj_set_style_border_color(p, C_BORDER, 0);
  lv_obj_set_style_pad_all(p, 8, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *t = lv_label_create(p);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, C_ACCENT, 0);
  return p;
}

static lv_obj_t *mk_label(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c, const char *txt = "") {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, txt);
  return l;
}

// Label mittig ueber x setzen (Breite fest, Text zentriert)
static lv_obj_t *mk_center(lv_obj_t *p, int cx, int y, int w, const lv_font_t *f, lv_color_t c) {
  lv_obj_t *l = mk_label(p, cx - w / 2, y, f, c);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

static void build_now(lv_obj_t *p) {
  now_icon = icon_box(p, 0, 26, 72);
  l_now_t = mk_label(p, 84, 24, &lv_font_montserrat_48, C_TEXT, "--");
  l_now_d = mk_label(p, 86, 80, &lv_font_montserrat_16, C_CALL);
  static const char *names[6] = {"Gefuehlt", "Luftfeuchte", "Wind", "Luftdruck", "Boeen", "Niederschlag"};
  for (int i = 0; i < 6; i++) {
    int x = (i % 2) * 142, y = 112 + (i / 2) * 38;
    mk_label(p, x, y, &lv_font_montserrat_12, C_DIM, names[i]);
    l_det_v[i] = mk_label(p, x, y + 14, &lv_font_montserrat_16, C_TEXT);
  }
  l_warn = mk_label(p, 0, 224, &lv_font_montserrat_14, C_POOR, LV_SYMBOL_WARNING "  Gewitter - Antennen trennen!");
  lv_obj_add_flag(l_warn, LV_OBJ_FLAG_HIDDEN);
}

static void build_hours(lv_obj_t *p) {
  // Nachtstunden dunkler hinterlegt (vor allem anderen, damit dahinter)
  for (int i = 0; i < NH; i++) {
    h_night[i] = shape(p, i * COL_W, 18, COL_W, BAR_BASE, COL_NIGHT, false);
    lv_obj_add_flag(h_night[i], LV_OBJ_FLAG_HIDDEN);
  }
  // Grundlinie der Regenbalken
  shape(p, 0, BAR_BASE, NH * COL_W, 1, 0x263142, false);
  chart = lv_chart_create(p);
  lv_obj_remove_style_all(chart);
  lv_obj_set_pos(chart, COL_W / 2, CH_Y);
  lv_obj_set_size(chart, (NH - 1) * COL_W, CH_H);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, NH);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 5, LV_PART_INDICATOR);
  ser = lv_chart_add_series(chart, lv_color_hex(COL_MAX), LV_CHART_AXIS_PRIMARY_Y);
  lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < NH; i++) {
    int cx = i * COL_W + COL_W / 2;
    h_time[i] = mk_center(p, cx, 20, COL_W, &lv_font_montserrat_12, C_DIM);
    h_icon[i] = icon_box(p, cx - 16, 36, 32);
    h_temp[i] = mk_center(p, cx, CH_Y, COL_W + 4, &lv_font_montserrat_12, C_TEXT);
    h_bar[i] = shape(p, cx - 7, BAR_BASE, 14, 0, COL_RAIN, false);
    lv_obj_set_style_radius(h_bar[i], 3, 0);
    lv_obj_set_style_bg_opa(h_bar[i], LV_OPA_80, 0);
    h_pop[i] = mk_center(p, cx, BAR_BASE + 3, COL_W, &lv_font_montserrat_12, C_DIM);
  }
  mk_label(p, NH * COL_W - 120, 0, &lv_font_montserrat_12, C_DIM, "Regenwahrsch. %");
}

static void build_days(lv_obj_t *p) {
  for (int i = 0; i < ND; i++) {
    int x = i * 258;
    if (i) shape(p, x - 8, 22, 1, 96, 0x263142, false);
    d_name[i] = mk_label(p, x, 22, &lv_font_montserrat_16, C_TEXT);
    d_icon[i] = icon_box(p, x, 46, 56);
    d_max[i] = mk_label(p, x + 70, 44, &lv_font_montserrat_28, lv_color_hex(COL_MAX));
    d_min[i] = mk_label(p, x + 150, 52, &lv_font_montserrat_20, lv_color_hex(COL_MIN));
    d_text[i] = mk_label(p, x + 70, 80, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_long_mode(d_text[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(d_text[i], 176);
    d_pop[i] = mk_label(p, x + 70, 100, &lv_font_montserrat_14, lv_color_hex(COL_RAIN));
  }
}

// ---------- Aktualisieren ----------
static void refresh() {
  Weather w = weather_get();
  if (!w.valid) {
    lv_label_set_text(l_now_d, "Noch keine Wetterdaten ...");
    return;
  }
  char b[64];
  struct tm l;
  localtime_r(&w.fetched, &l);
  snprintf(b, sizeof b, "Stand %02d:%02d", l.tm_hour, l.tm_min);
  lv_label_set_text(l_stand, b);

  // Jetzt
  draw_icon(now_icon, 72, w.code, is_day(time(nullptr)));
  snprintf(b, sizeof b, "%.1f\xC2\xB0", w.temp);
  lv_label_set_text(l_now_t, b);
  lv_label_set_text(l_now_d, wx_code_text(w.code));
  snprintf(b, sizeof b, "%.1f \xC2\xB0""C", w.feels);
  lv_label_set_text(l_det_v[0], b);
  snprintf(b, sizeof b, "%d %%", w.rh);
  lv_label_set_text(l_det_v[1], b);
  snprintf(b, sizeof b, "%s %.0f km/h", wx_wind_dir(w.wind_dir), w.wind);
  lv_label_set_text(l_det_v[2], b);
  snprintf(b, sizeof b, "%.0f hPa", w.pressure);
  lv_label_set_text(l_det_v[3], b);
  snprintf(b, sizeof b, "%.0f km/h", w.gust);
  lv_label_set_text(l_det_v[4], b);
  snprintf(b, sizeof b, "%.1f mm", w.precip);
  lv_label_set_text(l_det_v[5], b);
  if (w.thunder) lv_obj_clear_flag(l_warn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(l_warn, LV_OBJ_FLAG_HIDDEN);

  // 12 Stunden: Temperaturbereich fuer die Kurve
  float tmin = 1e9f, tmax = -1e9f;
  for (int i = 0; i < w.n_hours && i < NH; i++) {
    tmin = fminf(tmin, w.hours[i].temp);
    tmax = fmaxf(tmax, w.hours[i].temp);
  }
  if (tmax - tmin < 4) { float m = (tmax + tmin) / 2; tmin = m - 2; tmax = m + 2; }
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, (lv_coord_t)floorf(tmin * 10), (lv_coord_t)ceilf(tmax * 10));
  for (int i = 0; i < NH; i++) {
    bool have = i < w.n_hours;
    if (!have) {
      for (lv_obj_t *o : {h_time[i], h_temp[i], h_pop[i]}) lv_label_set_text(o, "");
      lv_obj_clean(h_icon[i]);
      lv_obj_add_flag(h_night[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_height(h_bar[i], 0);
      lv_chart_set_value_by_id(chart, ser, i, LV_CHART_POINT_NONE);
      continue;
    }
    const WxHour &h = w.hours[i];
    localtime_r(&h.t, &l);
    snprintf(b, sizeof b, "%02d", l.tm_hour);
    lv_label_set_text(h_time[i], b);
    bool day = is_day(h.t + 1800);
    draw_icon(h_icon[i], 32, h.code, day, day ? COL_PANE : COL_NIGHT);
    if (day) lv_obj_add_flag(h_night[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(h_night[i], LV_OBJ_FLAG_HIDDEN);
    lv_chart_set_value_by_id(chart, ser, i, (lv_coord_t)lroundf(h.temp * 10));
    // Wert ueber dem Kurvenpunkt
    float f = (h.temp - floorf(tmin * 10) / 10) / (ceilf(tmax * 10) / 10 - floorf(tmin * 10) / 10);
    int py = CH_Y + CH_H - (int)lroundf(f * CH_H);
    fmt_deg(b, sizeof b, h.temp);
    lv_label_set_text(h_temp[i], b);
    lv_obj_set_y(h_temp[i], py - 18);
    int bh = h.pop * BAR_H / 100;
    lv_obj_set_y(h_bar[i], BAR_BASE - bh);
    lv_obj_set_height(h_bar[i], bh);
    if (h.pop > 0) snprintf(b, sizeof b, "%d", h.pop); else b[0] = 0;   // Nullen weglassen (ruhiger)
    lv_label_set_text(h_pop[i], b);
    lv_obj_set_style_text_color(h_pop[i], h.pop >= 50 ? lv_color_hex(COL_RAIN) : C_DIM, 0);
  }
  lv_chart_refresh(chart);

  // 3 Tage
  for (int i = 0; i < ND; i++) {
    if (i >= w.n_days) {
      for (lv_obj_t *o : {d_name[i], d_max[i], d_min[i], d_text[i], d_pop[i]}) lv_label_set_text(o, "");
      lv_obj_clean(d_icon[i]);
      continue;
    }
    const WxDay &d = w.days[i];
    localtime_r(&d.t, &l);
    snprintf(b, sizeof b, "%s  %02d.%02d.", i == 0 ? "Heute" : WD[l.tm_wday], l.tm_mday, l.tm_mon + 1);
    lv_label_set_text(d_name[i], b);
    draw_icon(d_icon[i], 56, d.code, true);
    fmt_deg(b, sizeof b, d.tmax);
    lv_label_set_text(d_max[i], b);
    fmt_deg(b, sizeof b, d.tmin);
    lv_label_set_text(d_min[i], b);
    lv_label_set_text(d_text[i], wx_code_text(d.code));
    lv_obj_set_style_text_color(d_text[i], d.code >= 95 ? C_POOR : C_TEXT, 0);
    snprintf(b, sizeof b, "Regen %d %%", d.pop);
    lv_label_set_text(d_pop[i], b);
  }
}

static void tick(lv_timer_t *) {
  if (weather_seq() != seen) {
    seen = weather_seq();
    refresh();
  }
}

static void on_back(lv_event_t *) {
  lv_scr_load(scr_prev);
  lv_obj_del_async(scr);
}

static void on_delete(lv_event_t *) {
  lv_timer_del(timer);
  scr = nullptr;
}

void weather_ui_open() {
  if (scr) return;
  seen = 0xFFFFFFFF;
  LatLon q;
  maidenhead_to_latlon(g_settings.loc, q);
  qlat = q.lat;
  qlon = q.lon;
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
  lv_obj_t *t = mk_label(scr, 142, 10, &lv_font_montserrat_28, C_ACCENT);
  lv_label_set_text_fmt(t, "Wetter %s", g_settings.loc);
  l_stand = mk_label(scr, 0, 18, &lv_font_montserrat_14, C_DIM);
  lv_obj_align(l_stand, LV_ALIGN_TOP_RIGHT, -12, 18);

  build_now(panel(8, 52, 300, 250, "Jetzt"));
  build_hours(panel(316, 52, 476, 250, "Naechste 12 Stunden"));
  build_days(panel(8, 308, 784, 142, "3 Tage"));

  mk_label(scr, 12, 456, &lv_font_montserrat_12, C_DIM, "Wetterdaten: Open-Meteo.com (CC BY 4.0)");

  lv_scr_load(scr);
  refresh();
  timer = lv_timer_create(tick, 2000, nullptr);
}
