#include "dxpanel.h"
#include <Arduino.h>
#include <math.h>
#include "ui_theme.h"
#include "dxcluster.h"
#include "settings.h"
#include "astro.h"
#include "cty.h"
#include "map_render.h"
#include "worldmap.h"

static constexpr int ROWS = 11;
static constexpr int ROW_H = 19;
static constexpr int MAP_SPOTS = 40;

struct Row { lv_obj_t *box, *dot, *freq, *call, *time; };
static Row rows[ROWS];
static Spot *view;               // aktuell angezeigte Spots (PSRAM)
static int n_view = 0;
static lv_obj_t *l_head, *popup;
static double qlat, qlon;
static uint32_t seen_seq = 0xFFFFFFFF;
static uint32_t last_refresh = 0;
static bool map_dirty = true, force = true;

// ---------- Spot-Details ----------
int dxpanel_spots(const Spot **out) {
  *out = view;
  return n_view;
}

void dxpanel_format_spot(const Spot &s, char *buf, size_t n) {
  struct tm u;
  gmtime_r(&s.t, &u);
  if (s.ent >= 0) {
    const CtyEntity &c = cty_entities[s.ent];
    double d = gc_distance_km(qlat, qlon, s.lat, s.lon);
    double b = gc_bearing_deg(qlat, qlon, s.lat, s.lon);
    snprintf(buf, n, "%s  %s (%s, CQ %d)  %.0f km  %.0f\xC2\xB0\n%.1f kHz %s  de %s  %02d%02dZ  %s",
             s.dx, c.name, c.cont, s.cq, d, b, s.khz, s.mode, s.spotter, u.tm_hour, u.tm_min, s.comment);
  } else {
    snprintf(buf, n, "%s  (Land unbekannt)\n%.1f kHz %s  de %s  %02d%02dZ  %s",
             s.dx, s.khz, s.mode, s.spotter, u.tm_hour, u.tm_min, s.comment);
  }
}

static void row_clicked(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= n_view) return;
  char buf[160];
  dxpanel_format_spot(view[i], buf, sizeof buf);
  worldmap_show_info(buf);
}

// ---------- Bandfilter ----------
static void band_toggled(lv_event_t *e) {
  int b = (int)(intptr_t)lv_event_get_user_data(e);
  bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  uint16_t m = g_settings.band_mask;
  m = on ? (m | (1u << b)) : (m & ~(1u << b));
  g_settings.band_mask = m;
  force = true;
}

static void popup_close(lv_event_t *) {
  settings_save_band_mask(g_settings.band_mask);
  lv_obj_del(popup);
  popup = nullptr;
  force = true;
}

static void open_filter(lv_event_t *) {
  if (popup) return;
  popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(popup, 800, 480);
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(popup, LV_OPA_60, 0);
  lv_obj_set_style_border_width(popup, 0, 0);
  lv_obj_set_style_radius(popup, 0, 0);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(popup);
  lv_obj_set_size(box, 560, 330);
  lv_obj_center(box);
  lv_obj_set_style_bg_color(box, C_PANE, 0);
  lv_obj_set_style_border_color(box, C_BORDER, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(box);
  lv_label_set_text(t, "Baender anzeigen");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t, C_ACCENT, 0);

  for (int b = 0; b < B_OTHER; b++) {
    lv_obj_t *btn = lv_btn_create(box);
    lv_obj_set_size(btn, 120, 48);
    lv_obj_set_pos(btn, (b % 4) * 132, 36 + (b / 4) * 58);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x374151), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(BAND_COLORS[b]), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, C_TEXT, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x000000), LV_STATE_CHECKED);
    if (g_settings.band_mask & (1u << b)) lv_obj_add_state(btn, LV_STATE_CHECKED);
    lv_obj_add_event_cb(btn, band_toggled, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)b);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text_fmt(l, "%s%s", BAND_NAMES[b], b == B_70CM ? "" : " m");
    lv_obj_center(l);
  }
  lv_obj_t *ok = lv_btn_create(box);
  lv_obj_set_size(ok, 252, 48);
  lv_obj_set_pos(ok, 2 * 132, 36 + 3 * 58);
  lv_obj_set_style_bg_color(ok, lv_color_hex(0x15803D), 0);
  lv_obj_add_event_cb(ok, popup_close, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(ok);
  lv_label_set_text(l, LV_SYMBOL_OK "  Fertig");
  lv_obj_center(l);
}

// ---------- Aufbau ----------
static lv_obj_t *mk_label(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, "");
  return l;
}

void dxpanel_create(lv_obj_t *p, int x, int y, int w, int h, double qth_lat, double qth_lon) {
  qlat = qth_lat;
  qlon = qth_lon;
  view = (Spot *)heap_caps_calloc(MAP_SPOTS, sizeof(Spot), MALLOC_CAP_SPIRAM);

  l_head = mk_label(p, 0, 4, &lv_font_montserrat_12, C_DIM);
  lv_obj_set_width(l_head, w - 80);
  lv_label_set_long_mode(l_head, LV_LABEL_LONG_DOT);

  lv_obj_t *fb = lv_btn_create(p);
  lv_obj_set_size(fb, 66, 24);
  lv_obj_set_pos(fb, w - 66, 0);
  lv_obj_set_style_bg_color(fb, lv_color_hex(0x1E40AF), 0);
  lv_obj_set_ext_click_area(fb, 6);
  lv_obj_add_event_cb(fb, open_filter, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *fl = lv_label_create(fb);
  lv_label_set_text(fl, LV_SYMBOL_LIST " Band");
  lv_obj_set_style_text_font(fl, &lv_font_montserrat_12, 0);
  lv_obj_center(fl);

  for (int i = 0; i < ROWS; i++) {
    Row &r = rows[i];
    r.box = lv_obj_create(p);
    lv_obj_set_pos(r.box, 0, 28 + i * ROW_H);
    lv_obj_set_size(r.box, w, ROW_H);
    lv_obj_set_style_bg_opa(r.box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(r.box, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(r.box, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(r.box, 0, 0);
    lv_obj_set_style_radius(r.box, 2, 0);
    lv_obj_set_style_pad_all(r.box, 0, 0);
    lv_obj_clear_flag(r.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r.box, row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    r.dot = lv_obj_create(r.box);
    lv_obj_set_size(r.dot, 6, 12);
    lv_obj_set_pos(r.dot, 0, 3);
    lv_obj_set_style_border_width(r.dot, 0, 0);
    lv_obj_set_style_radius(r.dot, 2, 0);
    lv_obj_clear_flag(r.dot, LV_OBJ_FLAG_CLICKABLE);
    r.freq = mk_label(r.box, 10, 1, &lv_font_montserrat_14, C_TEXT);
    r.call = mk_label(r.box, 76, 1, &lv_font_montserrat_14, C_CALL);
    r.time = mk_label(r.box, w - 36, 2, &lv_font_montserrat_12, C_DIM);
    lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN);
  }
}

static void refresh() {
  n_view = dx_snapshot(view, MAP_SPOTS, g_settings.band_mask);
  time_t now = time(nullptr);
  for (int i = 0; i < ROWS; i++) {
    Row &r = rows[i];
    if (i >= n_view) { lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN); continue; }
    const Spot &s = view[i];
    lv_obj_clear_flag(r.box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(r.dot, lv_color_hex(BAND_COLORS[s.band]), 0);
    char f[16];
    snprintf(f, sizeof f, "%.1f", s.khz);
    lv_label_set_text(r.freq, f);
    lv_label_set_text(r.call, s.dx);
    int age = (int)((now - s.t) / 60);
    if (age < 0) age = 0;
    lv_label_set_text_fmt(r.time, "%d'", age);
    lv_obj_set_style_text_color(r.call, age < 5 ? C_CALL : C_TEXT, 0);
  }
  static const char *st[] = {"offline", "verbinde ...", "online", "Fehler"};
  DxState ds = dxcluster_state();
  char head[80];
  snprintf(head, sizeof head, "%s %s  %d Spots", ds == DX_ONLINE ? LV_SYMBOL_OK : LV_SYMBOL_WARNING,
           ds == DX_ONLINE ? dxcluster_server() : st[ds], n_view);
  lv_label_set_text(l_head, head);
  lv_obj_set_style_text_color(l_head, ds == DX_ONLINE ? C_DIM : C_FAIR, 0);
}

void dxpanel_update() {
  uint32_t sq = dx_seq();
  // bei neuen Spots hoechstens alle 2 s, sonst alle 30 s (Alter aktualisieren)
  if (force || (sq != seen_seq && millis() - last_refresh > 2000) || millis() - last_refresh > 30000) {
    if (sq != seen_seq || force) map_dirty = true;
    seen_seq = sq;
    force = false;
    last_refresh = millis();
    refresh();
  }
}

bool dxpanel_map_dirty() { return map_dirty; }

void dxpanel_push_map_spots() {
  static MapSpot ms[MAP_SPOTS];
  int n = 0;
  for (int i = 0; i < n_view && n < MAP_SPOTS; i++)
    if (view[i].ent >= 0) ms[n++] = {view[i].lat, view[i].lon, BAND_COLORS[view[i].band]};
  worldmap_set_spots(ms, n);
  map_dirty = false;
}
