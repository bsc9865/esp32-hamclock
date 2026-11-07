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
#include "watch.h"
#include "config.h"

static constexpr int ROWS = 11;
static constexpr int ROW_H = 19;
static constexpr int MAP_SPOTS = 40;
static Spot *all_spots;            // alle Spots aus dem Cluster (PSRAM), werden hier gefiltert
static bool watched[MAP_SPOTS];    // Watchlist-Treffer in view[]
static lv_obj_t *toast;            // Hinweis bei neuem Watchlist-Treffer
static lv_timer_t *toast_timer;
static uint32_t seen_settings = 0;

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

static void mode_toggled(lv_event_t *e) {
  uint8_t bit = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  g_settings.mode_mask = on ? (g_settings.mode_mask | bit) : (g_settings.mode_mask & ~bit);
  force = true;
}

static void popup_close(lv_event_t *) {
  settings_save(g_settings);   // Baender und Modes
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
  lv_obj_set_size(box, 560, 396);
  lv_obj_center(box);
  lv_obj_set_style_bg_color(box, C_PANE, 0);
  lv_obj_set_style_border_color(box, C_BORDER, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(box);
  lv_label_set_text(t, "Baender und Modes anzeigen");
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
  static const struct { const char *name; uint8_t bit; } modes[] = {
      {"CW", MODE_CW}, {"SSB", MODE_SSB}, {"Digi", MODE_DIGI}, {"sonst.", MODE_OTHER}};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *btn = lv_btn_create(box);
    lv_obj_set_size(btn, 120, 44);
    lv_obj_set_pos(btn, i * 132, 36 + 4 * 58 + 6);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x374151), 0);
    lv_obj_set_style_bg_color(btn, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x000000), LV_STATE_CHECKED);
    if (g_settings.mode_mask & modes[i].bit) lv_obj_add_state(btn, LV_STATE_CHECKED);
    lv_obj_add_event_cb(btn, mode_toggled, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)modes[i].bit);
    lv_obj_t *ml = lv_label_create(btn);
    lv_label_set_text(ml, modes[i].name);
    lv_obj_center(ml);
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
  all_spots = (Spot *)heap_caps_calloc(DX_MAX_SPOTS, sizeof(Spot), MALLOC_CAP_SPIRAM);

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

// ---------- Watchlist-Hinweis ----------
static void hide_toast(lv_timer_t *) {
  if (toast) lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(toast_timer);
}

static void show_toast(const Spot &s) {
  if (!toast) {
    toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(toast, 560, LV_SIZE_CONTENT);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x701A75), 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(0xF0ABFC), 0);
    lv_obj_set_style_border_width(toast, 2, 0);
    lv_obj_set_style_pad_all(toast, 10, 0);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(toast, [](lv_event_t *) { hide_toast(nullptr); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(toast);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(l, 536);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    toast_timer = lv_timer_create(hide_toast, 30000, nullptr);
  }
  char buf[160];
  const char *land = s.ent >= 0 ? cty_entities[s.ent].name : "";
  snprintf(buf, sizeof buf, LV_SYMBOL_BELL "  %s  %.1f kHz %s\n%s  (de %s)", s.dx, s.khz, s.mode, land, s.spotter);
  lv_label_set_text(lv_obj_get_child(toast, 0), buf);
  lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
  lv_timer_reset(toast_timer);
  lv_timer_resume(toast_timer);
}

// bereits gemeldete Treffer (Rufzeichen + Band), damit jeder Treffer nur einmal meldet
struct Alerted { char dx[16]; uint8_t band; time_t t; };
static Alerted alerted[24];
static int n_alerted = 0;

static bool already_alerted(const Spot &s) {
  for (int i = 0; i < n_alerted; i++)
    if (alerted[i].band == s.band && !strcmp(alerted[i].dx, s.dx) && s.t - alerted[i].t < 3600) return true;
  Alerted &a = alerted[n_alerted < 24 ? n_alerted++ : (int)(s.t % 24)];
  strncpy(a.dx, s.dx, sizeof a.dx - 1);
  a.dx[sizeof a.dx - 1] = 0;
  a.band = s.band;
  a.t = s.t;
  return false;
}

static void refresh() {
  // alle Spots holen, dann filtern: Watchlist-Treffer immer, sonst Band- und Mode-Filter
  int n_all = dx_snapshot(all_spots, DX_MAX_SPOTS, 0xFFFF);
  n_view = 0;
  const Spot *newest_hit = nullptr;
  for (int i = 0; i < n_all && n_view < MAP_SPOTS; i++) {
    const Spot &s = all_spots[i];
    const CtyEntity *ent = s.ent >= 0 ? &cty_entities[s.ent] : nullptr;
    bool hit = g_settings.watch[0] && watch_match(g_settings.watch, s.dx, ent);
    bool pass = (s.band >= 16 || (g_settings.band_mask & (1u << s.band))) && (g_settings.mode_mask & mode_class(s.mode));
    if (!hit && !pass) continue;
    watched[n_view] = hit;
    view[n_view++] = s;
    if (hit && !newest_hit && !already_alerted(s)) newest_hit = &view[n_view - 1];
  }
  if (newest_hit && time(nullptr) - newest_hit->t < 15 * 60) show_toast(*newest_hit);
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
    int age = (int)((now - s.t) / 60);
    if (age < 0) age = 0;
    lv_label_set_text_fmt(r.time, "%d'", age);
    if (watched[i]) {
      lv_label_set_text_fmt(r.call, LV_SYMBOL_BELL " %s", s.dx);
      lv_obj_set_style_text_color(r.call, lv_color_hex(0xF0ABFC), 0);
    } else {
      lv_label_set_text(r.call, s.dx);
      lv_obj_set_style_text_color(r.call, age < 5 ? C_CALL : C_TEXT, 0);
    }
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
  if (settings_seq() != seen_settings) { seen_settings = settings_seq(); force = true; }   // z.B. Watchlist geaendert
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

int dxpanel_map_spots(MapSpot *ms, int max) {
  int n = 0;
  for (int i = 0; i < n_view && n < max; i++)
    if (view[i].ent >= 0) ms[n++] = {view[i].lat, view[i].lon, watched[i] ? 0xF0ABFCu : BAND_COLORS[view[i].band], SHAPE_DX};
  map_dirty = false;
  return n;
}
