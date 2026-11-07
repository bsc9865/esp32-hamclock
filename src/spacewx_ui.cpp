// Seite "Weltraumwetter" (800x480)
//
//  [< Zurueck]  Weltraumwetter                     Kp 2.3  SFI 125  X B9.4
//  +-- Kp-Index, 7 Tage -------------+  +-- Roentgenfluss GOES, 6 h -------+
//  |  Balken, gruen/gelb/rot          |  |  Linie, log. Skala A..X          |
//  +----------------------------------+  +----------------------------------+
//  +-- Solarflux SFI, 30 Tage --------+  +-- SWPC-Meldungen ----------------+
//  |  Linie                           |  |  Liste (antippen = Volltext)     |
//  +----------------------------------+  +----------------------------------+
#include "spacewx_ui.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "spacewx.h"
#include "ui_theme.h"

static constexpr int KP_N = 56, XR_N = 360, SFI_N = 30, AL_ROWS = 7;

static lv_obj_t *scr, *scr_prev, *l_summary, *popup;
static lv_obj_t *kp_chart, *xr_chart, *sfi_chart, *l_kp_now, *l_xr_now, *l_sfi_now, *l_sfi_range;
static lv_chart_series_t *kp_ser, *xr_ser, *sfi_ser;
static lv_obj_t *al_rows[AL_ROWS];
static lv_timer_t *timer;
static WxData *wx;                 // Kopie fuer die Anzeige (PSRAM)
static uint32_t seen_seq = 0xFFFFFFFF;

// ---------- Helfer ----------
static lv_obj_t *panel(int x, int y, int w, int h, const char *title) {
  lv_obj_t *p = lv_obj_create(scr);
  lv_obj_set_pos(p, x, y);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_bg_color(p, C_PANE, 0);
  lv_obj_set_style_border_color(p, C_BORDER, 0);
  lv_obj_set_style_radius(p, 6, 0);
  lv_obj_set_style_pad_all(p, 6, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *t = lv_label_create(p);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, C_ACCENT, 0);
  return p;
}

static lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t *mk_chart(lv_obj_t *p, lv_chart_type_t type, int points, int y0, int y1, int hdiv) {
  lv_obj_t *c = lv_chart_create(p);
  lv_obj_set_pos(c, 0, 24);
  lv_obj_set_size(c, 352, 150);
  lv_chart_set_type(c, type);
  lv_chart_set_point_count(c, points);
  lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, y0, y1);
  lv_chart_set_div_line_count(c, hdiv, 0);
  lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_bg_color(c, lv_color_hex(0x0B1018), 0);
  lv_obj_set_style_border_color(c, C_BORDER, 0);
  lv_obj_set_style_line_color(c, lv_color_hex(0x2A3647), LV_PART_MAIN);
  lv_obj_set_style_pad_all(c, 2, 0);
  lv_obj_set_style_size(c, 0, LV_PART_INDICATOR);   // keine Punkte auf der Linie
  lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
  lv_obj_set_style_pad_column(c, 1, 0);             // Abstand zwischen Balken
  lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static lv_color_t kp_color(float kp) {
  return kp >= 5 ? C_POOR : (kp >= 4 ? C_FAIR : C_GOOD);
}

// Kp-Balken nach Sturmstufe einfaerben
static void kp_draw(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *d = lv_event_get_draw_part_dsc(e);
  if (d->part != LV_PART_ITEMS || d->type != LV_CHART_DRAW_PART_BAR || !d->rect_dsc) return;
  lv_coord_t v = kp_ser->y_points[d->id];
  if (v == LV_CHART_POINT_NONE) return;
  d->rect_dsc->bg_color = kp_color(v / 10.0f);
}

// ---------- Meldungen ----------
static void close_popup(lv_event_t *) {
  if (popup) { lv_obj_del(popup); popup = nullptr; }
}

static void alert_clicked(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= wx->n_alerts || popup) return;
  popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(popup, 800, 480);
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(popup, LV_OPA_60, 0);
  lv_obj_set_style_border_width(popup, 0, 0);
  lv_obj_set_style_radius(popup, 0, 0);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(popup, close_popup, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *box = lv_obj_create(popup);
  lv_obj_set_size(box, 720, 420);
  lv_obj_center(box);
  lv_obj_set_style_bg_color(box, C_PANE, 0);
  lv_obj_set_style_border_color(box, C_BORDER, 0);
  lv_obj_t *l = lv_label_create(box);
  lv_obj_set_width(l, 690);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, C_TEXT, 0);
  lv_label_set_text(l, wx->alerts[i].text);
  lv_obj_t *hint = lv_label_create(popup);
  lv_label_set_text(hint, "Antippen zum Schliessen");
  lv_obj_set_style_text_color(hint, C_DIM, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// ---------- Aktualisierung ----------
static void refresh() {
  spacewx_get(*wx);
  char buf[96];

  // Kp: die letzten 56 Werte (7 Tage)
  int start = wx->n_kp > KP_N ? wx->n_kp - KP_N : 0;
  lv_chart_set_all_value(kp_chart, kp_ser, LV_CHART_POINT_NONE);
  for (int i = start, k = KP_N - (wx->n_kp - start); i < wx->n_kp; i++, k++)
    kp_ser->y_points[k] = (lv_coord_t)lroundf(wx->kp[i].v * 10);
  lv_chart_refresh(kp_chart);
  float kp_now = wx->n_kp ? wx->kp[wx->n_kp - 1].v : NAN;
  if (!isnan(kp_now)) {
    snprintf(buf, sizeof buf, "aktuell %.1f", kp_now);
    lv_label_set_text(l_kp_now, buf);
    lv_obj_set_style_text_color(l_kp_now, kp_color(kp_now), 0);
  }

  // Roentgen: log10(Fluss) -> 100 (A, 1e-8) .. 600 (X10, 1e-3)
  start = wx->n_xray > XR_N ? wx->n_xray - XR_N : 0;
  lv_chart_set_all_value(xr_chart, xr_ser, LV_CHART_POINT_NONE);
  for (int i = start, k = XR_N - (wx->n_xray - start); i < wx->n_xray; i++, k++) {
    float v = (log10f(wx->xray[i].v) + 9.0f) * 100.0f;
    xr_ser->y_points[k] = (lv_coord_t)fminf(600, fmaxf(100, v));
  }
  lv_chart_refresh(xr_chart);
  char cls[8] = "--";
  if (wx->n_xray) {
    wx_xray_class(wx->xray[wx->n_xray - 1].v, cls);
    lv_label_set_text_fmt(l_xr_now, "aktuell %s", cls);
    lv_obj_set_style_text_color(l_xr_now, cls[0] == 'X' ? C_POOR : (cls[0] == 'M' ? C_FAIR : C_GOOD), 0);
  }

  // SFI
  int lo = 1000, hi = 0;
  for (int i = 0; i < wx->n_sfi; i++) {
    lo = min(lo, (int)wx->sfi[i].v);
    hi = max(hi, (int)wx->sfi[i].v);
  }
  if (wx->n_sfi) {
    lv_chart_set_range(sfi_chart, LV_CHART_AXIS_PRIMARY_Y, lo - 10, hi + 10);
    start = wx->n_sfi > SFI_N ? wx->n_sfi - SFI_N : 0;
    lv_chart_set_all_value(sfi_chart, sfi_ser, LV_CHART_POINT_NONE);
    for (int i = start, k = SFI_N - (wx->n_sfi - start); i < wx->n_sfi; i++, k++)
      sfi_ser->y_points[k] = (lv_coord_t)wx->sfi[i].v;
    lv_chart_refresh(sfi_chart);
    lv_label_set_text_fmt(l_sfi_now, "aktuell %d", (int)wx->sfi[wx->n_sfi - 1].v);
    lv_label_set_text_fmt(l_sfi_range, "min %d / max %d", lo, hi);
  }

  // Meldungen
  for (int i = 0; i < AL_ROWS; i++) {
    lv_obj_t *r = al_rows[i];
    if (i >= wx->n_alerts) { lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN); continue; }
    struct tm u;
    gmtime_r(&wx->alerts[i].t, &u);
    snprintf(buf, sizeof buf, "%02d.%02d. %02d:%02dZ  %s", u.tm_mday, u.tm_mon + 1, u.tm_hour, u.tm_min,
             wx->alerts[i].title);
    lv_label_set_text(lv_obj_get_child(r, 0), buf);
    const char *t = wx->alerts[i].title;
    lv_color_t c = !strncmp(t, "ALERT", 5) || !strncmp(t, "CONTINUED", 9) ? C_POOR
                   : !strncmp(t, "WARNING", 7) || !strncmp(t, "EXTENDED", 8) ? C_FAIR
                   : !strncmp(t, "WATCH", 5) ? C_CALL : C_TEXT;
    lv_obj_set_style_text_color(lv_obj_get_child(r, 0), c, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_HIDDEN);
  }

  snprintf(buf, sizeof buf, "Kp %.1f   SFI %d   X %s", isnan(kp_now) ? 0.0f : kp_now,
           wx->n_sfi ? (int)wx->sfi[wx->n_sfi - 1].v : 0, cls);
  lv_label_set_text(l_summary, buf);
}

static void tick(lv_timer_t *) {
  if (spacewx_seq() != seen_seq) {
    seen_seq = spacewx_seq();
    refresh();
  }
}

static void on_back(lv_event_t *) {
  close_popup(nullptr);
  lv_scr_load(scr_prev);
  lv_obj_del_async(scr);
}

static void on_delete(lv_event_t *) {
  lv_timer_del(timer);
  scr = nullptr;
}

void spacewx_ui_open() {
  if (scr) return;
  if (!wx) wx = (WxData *)heap_caps_calloc(1, sizeof(WxData), MALLOC_CAP_SPIRAM);
  seen_seq = 0xFFFFFFFF;
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
  lv_obj_t *t = mk_label(scr, &lv_font_montserrat_28, C_ACCENT);
  lv_label_set_text(t, "Weltraumwetter");
  lv_obj_set_pos(t, 142, 10);
  l_summary = mk_label(scr, &lv_font_montserrat_16, C_TEXT);
  lv_obj_align(l_summary, LV_ALIGN_TOP_RIGHT, -12, 18);

  // Kp
  lv_obj_t *p = panel(8, 52, 388, 210, "Kp-Index, 7 Tage");
  l_kp_now = mk_label(p, &lv_font_montserrat_14, C_TEXT);
  lv_obj_align(l_kp_now, LV_ALIGN_TOP_RIGHT, 0, 0);
  kp_chart = mk_chart(p, LV_CHART_TYPE_BAR, KP_N, 0, 90, 8);
  kp_ser = lv_chart_add_series(kp_chart, C_GOOD, LV_CHART_AXIS_PRIMARY_Y);
  lv_obj_add_event_cb(kp_chart, kp_draw, LV_EVENT_DRAW_PART_BEGIN, nullptr);
  for (int k = 0; k <= 9; k += 3) {   // Skala 0, 3, 6, 9
    lv_obj_t *s = mk_label(p, &lv_font_montserrat_12, C_DIM);
    lv_label_set_text_fmt(s, "%d", k);
    lv_obj_set_pos(s, 360, 24 + 2 + (146 * (9 - k)) / 9 - 7);
  }
  lv_obj_t *s = mk_label(p, &lv_font_montserrat_12, C_DIM);
  lv_label_set_text(s, "vor 7 Tagen                                                          jetzt");
  lv_obj_set_pos(s, 0, 178);

  // Roentgenfluss
  p = panel(404, 52, 388, 210, "Roentgenfluss (GOES), 6 h");
  l_xr_now = mk_label(p, &lv_font_montserrat_14, C_TEXT);
  lv_obj_align(l_xr_now, LV_ALIGN_TOP_RIGHT, 0, 0);
  xr_chart = mk_chart(p, LV_CHART_TYPE_LINE, XR_N, 100, 600, 4);
  xr_ser = lv_chart_add_series(xr_chart, lv_color_hex(0xFBBF24), LV_CHART_AXIS_PRIMARY_Y);
  static const char *classes[] = {"A", "B", "C", "M", "X"};
  for (int k = 0; k < 5; k++) {
    s = mk_label(p, &lv_font_montserrat_12, k >= 3 ? (k == 4 ? C_POOR : C_FAIR) : C_DIM);
    lv_label_set_text(s, classes[k]);
    lv_obj_set_pos(s, 362, 24 + 2 + 146 * (4 - k) / 5 + 146 / 10 - 7);
  }
  s = mk_label(p, &lv_font_montserrat_12, C_DIM);
  lv_label_set_text(s, "vor 6 Stunden                                                       jetzt");
  lv_obj_set_pos(s, 0, 178);

  // SFI
  p = panel(8, 268, 388, 206, "Solarflux SFI, 30 Tage");
  l_sfi_now = mk_label(p, &lv_font_montserrat_14, C_TEXT);
  lv_obj_align(l_sfi_now, LV_ALIGN_TOP_RIGHT, 0, 0);
  sfi_chart = mk_chart(p, LV_CHART_TYPE_LINE, SFI_N, 60, 200, 4);
  lv_obj_set_size(sfi_chart, 352, 146);
  sfi_ser = lv_chart_add_series(sfi_chart, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
  l_sfi_range = mk_label(p, &lv_font_montserrat_12, C_DIM);
  lv_obj_set_pos(l_sfi_range, 0, 174);

  // Meldungen
  p = panel(404, 268, 388, 206, "SWPC-Meldungen (antippen)");
  for (int i = 0; i < AL_ROWS; i++) {
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_set_pos(r, 0, 22 + i * 24);
    lv_obj_set_size(r, 374, 22);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(r, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r, alert_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = mk_label(r, &lv_font_montserrat_12, C_TEXT);
    lv_obj_set_width(l, 374);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, 0, 4);
    lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    al_rows[i] = r;
  }

  lv_scr_load(scr);
  timer = lv_timer_create(tick, 2000, nullptr);
  tick(nullptr);
}
