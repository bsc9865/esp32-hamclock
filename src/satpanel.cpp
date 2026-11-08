#include "satpanel.h"
#include <Arduino.h>
#include <algorithm>
#include "ui_theme.h"
#include "sats.h"
#include "map_render.h"
#include "worldmap.h"
#include "satdetail.h"

static constexpr int ROWS = 11, ROW_H = 19;
static constexpr uint32_t COL_SAT = 0xE5E7EB, COL_VISIBLE = 0x4ADE80, COL_SELECTED = 0xFFDC00;

struct Row { lv_obj_t *box, *name, *info; };
static Row rows[ROWS];
static SatView *sv;                 // letzte Momentaufnahme (PSRAM)
static int order[SAT_MAX_FAV];      // Anzeigereihenfolge
static lv_obj_t *l_head, *popup, *pop_list;
static uint32_t seen_seq = 0xFFFFFFFF, last_refresh = 0;
static bool map_dirty = true, force = true;

static void fmt_time(time_t t, char *out, size_t n) {
  struct tm l;
  localtime_r(&t, &l);
  snprintf(out, n, "%02d:%02d", l.tm_hour, l.tm_min);
}

int satpanel_rows(const SatRow **out) {
  *out = sv ? sv->rows : nullptr;
  return sv ? sv->n : 0;
}

void satpanel_format(const SatRow &r, char *buf, size_t n) {
  char a[8], m[8], l[8];
  const SatState &s = r.st;
  if (r.geo) {
    snprintf(buf, n, "%s  geostationaer  Az %.0f\xC2\xB0  El %.1f\xC2\xB0  Entf. %.0f km\nSubsatellitenpunkt %.1f\xC2\xB0%c",
             r.name, s.az, s.el, s.range_km, fabs(s.lon), s.lon >= 0 ? 'E' : 'W');
  } else if (r.has_pass) {
    fmt_time(r.pass.aos, a, sizeof a);
    fmt_time(r.pass.tmax, m, sizeof m);
    fmt_time(r.pass.los, l, sizeof l);
    long d = (long)(r.pass.los - r.pass.aos);
    snprintf(buf, n, "%s  AOS %s Az %.0f\xC2\xB0  max %.0f\xC2\xB0 %s Az %.0f\xC2\xB0  LOS %s Az %.0f\xC2\xB0  (%ld:%02ld)\n"
             "jetzt Az %.0f\xC2\xB0 El %.1f\xC2\xB0  Entf. %.0f km  Hoehe %.0f km",
             r.name, a, r.pass.az_aos, r.pass.max_el, m, r.pass.az_max, l, r.pass.az_los, d / 60, d % 60,
             s.az, s.el, s.range_km, s.alt_km);
  } else {
    snprintf(buf, n, "%s  kein Ueberflug in den naechsten 24 h\njetzt Az %.0f\xC2\xB0 El %.1f\xC2\xB0  Hoehe %.0f km",
             r.name, s.az, s.el, s.alt_km);
  }
}

static void row_clicked(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (!sv || i >= sv->n) return;
  const SatRow &r = sv->rows[order[i]];
  sats_select(r.norad);       // Bodenspur auf der Karte (in der Detailansicht abschaltbar)
  satdetail_open(r.norad);
  force = true;
}

// ---------- Auswahl der Favoriten ----------
static void popup_save(lv_event_t *) {
  int ids[SAT_MAX_FAV], n = 0;
  uint32_t cnt = lv_obj_get_child_cnt(pop_list);
  for (uint32_t i = 0; i < cnt && n < SAT_MAX_FAV; i++) {
    lv_obj_t *cb = lv_obj_get_child(pop_list, i);
    if (lv_obj_has_state(cb, LV_STATE_CHECKED)) ids[n++] = (int)(intptr_t)lv_obj_get_user_data(cb);
  }
  sats_set_favorites(ids, n);
  lv_obj_del(popup);
  popup = nullptr;
  force = true;
}

static void popup_check(lv_event_t *e) {
  // hoechstens SAT_MAX_FAV Favoriten
  int n = 0;
  uint32_t cnt = lv_obj_get_child_cnt(pop_list);
  for (uint32_t i = 0; i < cnt; i++)
    if (lv_obj_has_state(lv_obj_get_child(pop_list, i), LV_STATE_CHECKED)) n++;
  if (n > SAT_MAX_FAV) lv_obj_clear_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static void open_selection(lv_event_t *) {
  if (popup) return;
  static int ids[160];
  static char names[160][16];
  int n = sats_catalog(ids, names, 160);

  popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(popup, 800, 480);
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(popup, LV_OPA_60, 0);
  lv_obj_set_style_border_width(popup, 0, 0);
  lv_obj_set_style_radius(popup, 0, 0);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(popup);
  lv_obj_set_size(box, 640, 440);
  lv_obj_center(box);
  lv_obj_set_style_bg_color(box, C_PANE, 0);
  lv_obj_set_style_border_color(box, C_BORDER, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *t = lv_label_create(box);
  lv_label_set_text_fmt(t, "Satelliten auswaehlen (hoechstens %d)", SAT_MAX_FAV);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t, C_ACCENT, 0);

  pop_list = lv_obj_create(box);
  lv_obj_set_pos(pop_list, 0, 34);
  lv_obj_set_size(pop_list, 610, 300);
  lv_obj_set_flex_flow(pop_list, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(pop_list, 6, 0);
  lv_obj_set_style_bg_color(pop_list, lv_color_hex(0x0B1018), 0);
  lv_obj_set_style_border_color(pop_list, C_BORDER, 0);
  // Favoriten zuerst, dann alphabetisch
  int idx[160];
  for (int i = 0; i < n; i++) idx[i] = i;
  std::sort(idx, idx + n, [&](int a, int b) {
    bool fa = sats_is_favorite(ids[a]), fb = sats_is_favorite(ids[b]);
    if (fa != fb) return fa;
    return strcmp(names[a], names[b]) < 0;
  });
  for (int k = 0; k < n; k++) {
    int i = idx[k];
    lv_obj_t *cb = lv_checkbox_create(pop_list);
    lv_checkbox_set_text(cb, names[i]);
    lv_obj_set_width(cb, 186);
    lv_obj_set_style_text_color(cb, C_TEXT, 0);
    lv_obj_set_user_data(cb, (void *)(intptr_t)ids[i]);
    if (sats_is_favorite(ids[i])) lv_obj_add_state(cb, LV_STATE_CHECKED);
    lv_obj_add_event_cb(cb, popup_check, LV_EVENT_VALUE_CHANGED, nullptr);
  }

  lv_obj_t *ok = lv_btn_create(box);
  lv_obj_set_size(ok, 200, 48);
  lv_obj_set_pos(ok, 410, 346);
  lv_obj_set_style_bg_color(ok, lv_color_hex(0x15803D), 0);
  lv_obj_add_event_cb(ok, popup_save, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(ok);
  lv_label_set_text(l, LV_SYMBOL_OK "  Uebernehmen");
  lv_obj_center(l);
  lv_obj_t *info = lv_label_create(box);
  lv_label_set_text_fmt(info, "%d Satelliten aus CelesTrak (amateur)", n);
  lv_obj_set_style_text_color(info, C_DIM, 0);
  lv_obj_set_pos(info, 0, 360);
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

void satpanel_create(lv_obj_t *p, int w) {
  sv = (SatView *)heap_caps_calloc(1, sizeof(SatView), MALLOC_CAP_SPIRAM);
  l_head = mk_label(p, 0, 5, &lv_font_montserrat_12, C_DIM);
  lv_obj_t *b = lv_btn_create(p);
  lv_obj_set_size(b, 76, 24);
  lv_obj_set_pos(b, w - 76, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x1E40AF), 0);
  lv_obj_set_ext_click_area(b, 6);
  lv_obj_add_event_cb(b, open_selection, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = lv_label_create(b);
  lv_label_set_text(bl, LV_SYMBOL_LIST " Auswahl");
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
  lv_obj_center(bl);

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
    r.name = mk_label(r.box, 0, 1, &lv_font_montserrat_14, C_TEXT);
    r.info = mk_label(r.box, 80, 2, &lv_font_montserrat_12, C_DIM);
    lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN);
  }
}

static void refresh() {
  sats_get(*sv);
  time_t now = time(nullptr);
  // Reihenfolge: sichtbar, dann nach AOS, geostationaer und ohne Ueberflug zuletzt
  for (int i = 0; i < sv->n; i++) order[i] = i;
  std::sort(order, order + sv->n, [&](int a, int b) {
    const SatRow &x = sv->rows[a], &y = sv->rows[b];
    auto key = [&](const SatRow &r) -> double {
      if (r.geo) return 4e12;
      if (r.st.el > 0) return -1e9 + (90 - r.st.el);
      return r.has_pass ? (double)r.pass.aos : 3e12;
    };
    return key(x) < key(y);
  });
  for (int i = 0; i < ROWS; i++) {
    Row &r = rows[i];
    if (i >= sv->n) { lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN); continue; }
    const SatRow &s = sv->rows[order[i]];
    lv_obj_clear_flag(r.box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(r.name, s.name);
    bool selected = s.norad == sv->sel_norad;
    lv_obj_set_style_text_color(r.name, selected ? lv_color_hex(COL_SELECTED) : C_TEXT, 0);
    char buf[48];
    lv_color_t c = C_DIM;
    if (s.geo) {
      snprintf(buf, sizeof buf, "geo  El %.0f\xC2\xB0", s.st.el);
    } else if (s.st.el > 0) {
      snprintf(buf, sizeof buf, "jetzt El %.0f\xC2\xB0 Az %.0f\xC2\xB0", s.st.el, s.st.az);
      c = lv_color_hex(COL_VISIBLE);
    } else if (s.has_pass) {
      char a[8];
      fmt_time(s.pass.aos, a, sizeof a);
      long in = (long)(s.pass.aos - now) / 60;
      if (in < 60) snprintf(buf, sizeof buf, "%s (%ld') max %.0f\xC2\xB0", a, in, s.pass.max_el);
      else snprintf(buf, sizeof buf, "%s  max %.0f\xC2\xB0", a, s.pass.max_el);
      if (s.pass.max_el >= 30) c = C_TEXT;
    } else {
      snprintf(buf, sizeof buf, "--");
    }
    lv_label_set_text(r.info, buf);
    lv_obj_set_style_text_color(r.info, c, 0);
  }
  if (sv->tle_count == 0) lv_label_set_text(l_head, "TLE werden geladen ...");
  else {
    long age_h = (long)(now - sv->tle_time) / 3600;
    lv_label_set_text_fmt(l_head, "%d Sat.  TLE %ldh alt", sv->n, age_h);
  }
}

void satpanel_update() {
  uint32_t sq = sats_seq();
  if (force || (sq != seen_seq && millis() - last_refresh > 2000)) {
    if (sq != seen_seq || force) map_dirty = true;
    seen_seq = sq;
    force = false;
    last_refresh = millis();
    refresh();
  }
}

bool satpanel_map_dirty() { return map_dirty; }

int satpanel_map_spots(MapSpot *ms, int max) {
  int n = 0;
  for (int i = 0; sv && i < sv->n && n < max; i++) {
    const SatRow &r = sv->rows[i];
    if (!r.valid) continue;
    uint32_t c = r.norad == sv->sel_norad ? COL_SELECTED : (r.st.el > 0 ? COL_VISIBLE : COL_SAT);
    ms[n++] = {(float)r.st.lat, (float)r.st.lon, c, SHAPE_SAT};
  }
  map_dirty = false;
  return n;
}

void satpanel_overlays(MapOverlays &ov) {
  if (!sv || !sv->sel_norad) return;
  ov.trk_lat = sv->trk_lat;
  ov.trk_lon = sv->trk_lon;
  ov.trk_n = sv->trk_n;
  ov.fp_lat = sv->fp_lat;
  ov.fp_lon = sv->fp_lon;
  ov.fp_n = SAT_FP_N;
}
