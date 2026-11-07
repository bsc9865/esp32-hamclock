#include "actpanel.h"
#include <Arduino.h>
#include "ui_theme.h"
#include "activations.h"
#include "settings.h"
#include "astro.h"
#include "map_render.h"
#include "worldmap.h"
#include "watch.h"

static constexpr int ROWS = 11;
static constexpr int ROW_H = 19;
static constexpr int MAX_ITEMS = 40;

struct Row { lv_obj_t *box, *badge, *freq, *call, *ref; };
static Row rows[ROWS];
static Activation *items;
static int n_items = 0;
static lv_obj_t *l_head, *btn_type[ACT_TYPES];
static uint8_t type_mask = (1 << ACT_POTA) | (1 << ACT_WWFF);
static double qlat, qlon;
static uint32_t seen_seq = 0xFFFFFFFF, last_refresh = 0;
static uint16_t seen_bands = 0;
static bool map_dirty = true, force = true;

int actpanel_items(const Activation **out) {
  *out = items;
  return n_items;
}

void actpanel_format(const Activation &a, char *buf, size_t n) {
  struct tm u;
  gmtime_r(&a.t, &u);
  double d = gc_distance_km(qlat, qlon, a.lat, a.lon);
  double b = gc_bearing_deg(qlat, qlon, a.lat, a.lon);
  snprintf(buf, n, "%s  %s %s  %s\n%.1f kHz %s  %s%s%02d%02dZ  %.0f km  %.0f\xC2\xB0  %s",
           a.call, ACT_NAMES[a.type], a.ref, a.name, a.khz, a.mode, a.loc, a.loc[0] ? "  " : "",
           u.tm_hour, u.tm_min, d, b, a.comment);
}

static void row_clicked(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= n_items) return;
  char buf[200];
  actpanel_format(items[i], buf, sizeof buf);
  worldmap_show_info(buf);
}

static void type_toggled(lv_event_t *e) {
  int t = (int)(intptr_t)lv_event_get_user_data(e);
  bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  type_mask = on ? (type_mask | (1 << t)) : (type_mask & ~(1 << t));
  force = true;
}

static lv_obj_t *mk_label(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, "");
  return l;
}

void actpanel_create(lv_obj_t *p, int w, double qth_lat, double qth_lon) {
  qlat = qth_lat;
  qlon = qth_lon;
  items = (Activation *)heap_caps_calloc(MAX_ITEMS, sizeof(Activation), MALLOC_CAP_SPIRAM);

  l_head = mk_label(p, 0, 5, &lv_font_montserrat_12, C_DIM);

  // Typ-Schalter POTA / WWFF
  for (int t = 0; t < ACT_TYPES; t++) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_size(b, 54, 24);
    lv_obj_set_pos(b, w - 112 + t * 58, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_state(b, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x374151), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ACT_COLORS[t]), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, C_TEXT, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0x000000), LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, type_toggled, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)t);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, ACT_NAMES[t]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);
    btn_type[t] = b;
  }

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
    r.badge = mk_label(r.box, 0, 2, &lv_font_montserrat_12, lv_color_hex(0x000000));
    lv_obj_set_style_bg_opa(r.badge, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r.badge, 2, 0);
    lv_obj_set_style_radius(r.badge, 2, 0);
    r.freq = mk_label(r.box, 16, 2, &lv_font_montserrat_12, C_TEXT);
    r.call = mk_label(r.box, 70, 1, &lv_font_montserrat_14, C_CALL);
    r.ref = mk_label(r.box, w - 66, 2, &lv_font_montserrat_12, C_DIM);
    lv_obj_set_width(r.ref, 66);
    lv_obj_set_style_text_align(r.ref, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(r.ref, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN);
  }
}

static void refresh() {
  n_items = act_snapshot(items, MAX_ITEMS, type_mask, g_settings.band_mask);
  // Mode-Filter
  int k = 0;
  for (int i = 0; i < n_items; i++)
    if (g_settings.mode_mask & mode_class(items[i].mode)) items[k++] = items[i];
  n_items = k;
  for (int i = 0; i < ROWS; i++) {
    Row &r = rows[i];
    if (i >= n_items) { lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN); continue; }
    const Activation &a = items[i];
    lv_obj_clear_flag(r.box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(r.badge, a.type == ACT_POTA ? "P" : "W");
    lv_obj_set_style_bg_color(r.badge, lv_color_hex(ACT_COLORS[a.type]), 0);
    char f[16];
    snprintf(f, sizeof f, "%.1f", a.khz);
    lv_label_set_text(r.freq, f);
    lv_label_set_text(r.call, a.call);
    lv_label_set_text(r.ref, a.ref);
  }
  char head[40];
  snprintf(head, sizeof head, "%d aktiv", n_items);
  lv_label_set_text(l_head, head);
}

void actpanel_update() {
  uint32_t sq = act_seq();
  static uint8_t seen_modes = 0xFF;
  bool bands_changed = g_settings.band_mask != seen_bands || g_settings.mode_mask != seen_modes;
  seen_modes = g_settings.mode_mask;
  if (force || bands_changed || (sq != seen_seq && millis() - last_refresh > 2000) ||
      millis() - last_refresh > 30000) {
    if (sq != seen_seq || force || bands_changed) map_dirty = true;
    seen_seq = sq;
    seen_bands = g_settings.band_mask;
    force = false;
    last_refresh = millis();
    refresh();
  }
}

bool actpanel_map_dirty() { return map_dirty; }

int actpanel_map_spots(MapSpot *ms, int max) {
  int n = 0;
  for (int i = 0; i < n_items && n < max; i++) {
    const Activation &a = items[i];
    if (a.lat == 0 && a.lon == 0) continue;
    ms[n++] = {a.lat, a.lon, ACT_COLORS[a.type], (uint8_t)(a.type == ACT_POTA ? SHAPE_SQUARE : SHAPE_DIAMOND)};
  }
  map_dirty = false;
  return n;
}
