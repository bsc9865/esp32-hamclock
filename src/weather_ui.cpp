// Seite "Wetter am QTH" (800x480)
//
//  [< Zurueck]  Wetter JO50uh                                    Stand 10:15
//  +-- Jetzt ----------------------+  +-- Naechste 12 Stunden ------------------+
//  |  1.3 °C  bedeckt               |  | 11:00  1.8°  bedeckt         0 %       |
//  |  gefuehlt, Wind, Boeen, ...    |  | ...                                     |
//  +--------------------------------+  +-----------------------------------------+
//  +-- 3 Tage ----------------------------------------------------------------+
//  Daten: Open-Meteo.com (CC BY 4.0)
#include "weather_ui.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "weather.h"
#include "settings.h"
#include "ui_theme.h"

static lv_obj_t *scr, *scr_prev, *l_stand, *l_now_t, *l_now_d, *l_now_x, *l_warn;
static lv_obj_t *l_hour[12], *l_day[3];
static lv_timer_t *timer;
static uint32_t seen = 0xFFFFFFFF;
static const char *WD[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

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

static lv_obj_t *mk_label(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, "");
  return l;
}

static void refresh() {
  Weather w = weather_get();
  if (!w.valid) {
    lv_label_set_text(l_now_t, "--");
    lv_label_set_text(l_now_d, "Noch keine Wetterdaten ...");
    return;
  }
  char b[160];
  struct tm l;
  localtime_r(&w.fetched, &l);
  snprintf(b, sizeof b, "Stand %02d:%02d", l.tm_hour, l.tm_min);
  lv_label_set_text(l_stand, b);
  snprintf(b, sizeof b, "%.1f \xC2\xB0""C", w.temp);
  lv_label_set_text(l_now_t, b);
  lv_label_set_text(l_now_d, wx_code_text(w.code));
  snprintf(b, sizeof b,
           "gefuehlt %.1f \xC2\xB0""C\nWind %s %.0f km/h, Boeen %.0f km/h\nLuftfeuchte %d %%   Wolken %d %%\n"
           "Luftdruck %.0f hPa   Niederschlag %.1f mm",
           w.feels, wx_wind_dir(w.wind_dir), w.wind, w.gust, w.rh, w.clouds, w.pressure, w.precip);
  lv_label_set_text(l_now_x, b);
  if (w.thunder) lv_obj_clear_flag(l_warn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(l_warn, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < 12; i++) {
    if (i >= w.n_hours) { lv_label_set_text(l_hour[i], ""); continue; }
    const WxHour &h = w.hours[i];
    localtime_r(&h.t, &l);
    snprintf(b, sizeof b, "%02d:00   %5.1f\xC2\xB0   %-16s %3d %%", l.tm_hour, h.temp, wx_code_text(h.code), h.pop);
    lv_label_set_text(l_hour[i], b);
    lv_obj_set_style_text_color(l_hour[i], h.code >= 95 ? C_POOR : (h.pop >= 50 ? C_ACCENT : C_TEXT), 0);
  }
  for (int i = 0; i < 3; i++) {
    if (i >= w.n_days) { lv_label_set_text(l_day[i], ""); continue; }
    const WxDay &d = w.days[i];
    localtime_r(&d.t, &l);
    snprintf(b, sizeof b, "%s %02d.%02d.\n%.0f\xC2\xB0 / %.0f\xC2\xB0\n%s\nRegen %d %%", i == 0 ? "Heute" : WD[l.tm_wday],
             l.tm_mday, l.tm_mon + 1, d.tmax, d.tmin, wx_code_text(d.code), d.pop);
    lv_label_set_text(l_day[i], b);
    lv_obj_set_style_text_color(l_day[i], d.code >= 95 ? C_POOR : C_TEXT, 0);
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

  lv_obj_t *p = panel(8, 52, 330, 250, "Jetzt");
  l_now_t = mk_label(p, 0, 24, &lv_font_montserrat_48, C_TEXT);
  l_now_d = mk_label(p, 0, 80, &lv_font_montserrat_20, C_CALL);
  l_now_x = mk_label(p, 0, 112, &lv_font_montserrat_16, C_TEXT);
  l_warn = mk_label(p, 0, 202, &lv_font_montserrat_16, C_POOR);
  lv_label_set_text(l_warn, LV_SYMBOL_WARNING "  Gewittergefahr - Antennen trennen!");
  lv_obj_add_flag(l_warn, LV_OBJ_FLAG_HIDDEN);

  p = panel(346, 52, 446, 250, "Naechste 12 Stunden");
  for (int i = 0; i < 12; i++) l_hour[i] = mk_label(p, 0, 22 + i * 18, &lv_font_montserrat_14, C_TEXT);

  p = panel(8, 308, 784, 142, "3 Tage");
  for (int i = 0; i < 3; i++) l_day[i] = mk_label(p, i * 260, 22, &lv_font_montserrat_16, C_TEXT);

  lv_obj_t *src = mk_label(scr, 12, 456, &lv_font_montserrat_12, C_DIM);
  lv_label_set_text(src, "Wetterdaten: Open-Meteo.com (CC BY 4.0)");

  lv_scr_load(scr);
  refresh();
  timer = lv_timer_create(tick, 2000, nullptr);
}
