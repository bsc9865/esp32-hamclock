// Detailansicht eines Satelliten (Vollbild): Polardiagramm, Live-Werte, Ueberfluege, Frequenzen mit Doppler
#include "satdetail.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "sats.h"
#include "satfreq.h"
#include "ui_theme.h"

static constexpr int PR = 150;                 // Radius des Polardiagramms
static constexpr int PX = 30, PY = 112;        // Lage des Diagramms (oben links)
static constexpr int MAX_ROWS = 7;
static constexpr uint32_t COL_TRACK = 0xFFDC00, COL_AOS = 0x4ADE80, COL_LOS = 0xF87171, COL_SAT = 0x4ADE80;

static lv_obj_t *scr, *scr_prev, *l_title, *l_full, *l_live, *l_status, *l_passes, *l_fhead, *map_btn;
static lv_obj_t *track, *dot_aos, *dot_los, *dot_sat, *l_aos, *l_los;
static lv_obj_t *f_desc[MAX_ROWS], *f_val[MAX_ROWS];
static lv_point_t pts[SAT_POLAR_N];
static lv_timer_t *timer;
static SatDetail *d;                           // PSRAM
static SatTx *tx;                             // SATFREQ_PER_SAT, PSRAM
static int cur_norad;
static uint32_t seen_seq, seen_fseq;
static int last_polar_n = -1;
static time_t last_aos = 0;

static void polar_xy(float az, float el, lv_coord_t &x, lv_coord_t &y) {
  float r = PR * (90.0f - (el < 0 ? 0 : el)) / 90.0f;
  x = (lv_coord_t)lroundf(PR + r * sinf(az * (float)M_PI / 180.0f));
  y = (lv_coord_t)lroundf(PR - r * cosf(az * (float)M_PI / 180.0f));
}

static lv_obj_t *mk_label(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c, const char *t = "") {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, t);
  return l;
}

static lv_obj_t *mk_dot(lv_obj_t *p, int size, uint32_t color) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_size(o, size, size);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_border_color(o, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

static void place(lv_obj_t *o, float az, float el) {
  lv_coord_t x, y;
  polar_xy(az, el, x, y);
  lv_obj_set_pos(o, x - lv_obj_get_width(o) / 2, y - lv_obj_get_height(o) / 2);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void fmt_hm(time_t t, char *out, size_t n, bool weekday) {
  static const char *WD[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  struct tm l;
  localtime_r(&t, &l);
  if (weekday) snprintf(out, n, "%s %02d:%02d", WD[l.tm_wday], l.tm_hour, l.tm_min);
  else snprintf(out, n, "%02d:%02d", l.tm_hour, l.tm_min);
}

// MHz mit 4 Nachkommastellen (100 Hz)
static void fmt_mhz(double hz, char *out, size_t n) { snprintf(out, n, "%.4f", hz / 1e6); }

// ---------- Aktualisieren ----------
static void update_polar() {
  if (d->polar_n == last_polar_n && (d->n_pass == 0 || d->pass[0].aos == last_aos)) return;
  last_polar_n = d->polar_n;
  last_aos = d->n_pass ? d->pass[0].aos : 0;
  if (d->polar_n < 2) {
    lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t *o : {dot_aos, dot_los, l_aos, l_los}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  for (int i = 0; i < d->polar_n; i++) polar_xy(d->polar_az[i], d->polar_el[i], pts[i].x, pts[i].y);
  lv_line_set_points(track, pts, d->polar_n);
  lv_obj_clear_flag(track, LV_OBJ_FLAG_HIDDEN);
  place(dot_aos, d->polar_az[0], d->polar_el[0]);
  place(dot_los, d->polar_az[d->polar_n - 1], d->polar_el[d->polar_n - 1]);
  lv_obj_set_pos(l_aos, lv_obj_get_x(dot_aos) + 12, lv_obj_get_y(dot_aos) - 4);
  lv_obj_set_pos(l_los, lv_obj_get_x(dot_los) + 12, lv_obj_get_y(dot_los) - 4);
  lv_obj_clear_flag(l_aos, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(l_los, LV_OBJ_FLAG_HIDDEN);
}

static void update_freqs(bool doppler) {
  int n = satfreq_get(cur_norad, tx, SATFREQ_PER_SAT);
  if (n == 0) lv_label_set_text(l_fhead, "Frequenzen: noch keine Daten von SatNOGS ...");
  else lv_label_set_text(l_fhead, doppler ? "Frequenzen mit Doppler-Korrektur (jetzt)"
                                          : "Frequenzen nominal (Doppler waehrend des Ueberflugs)");
  double rr = doppler ? d->range_rate : 0;
  for (int i = 0; i < MAX_ROWS; i++) {
    if (i >= n) {
      lv_obj_add_flag(f_desc[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(f_val[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const SatTx &t = tx[i];
    static const char *KIND[] = {"Transponder", "Transceiver", "Sender"};
    char desc[64];
    snprintf(desc, sizeof desc, "%s  (%s%s)", t.desc, KIND[t.kind], t.invert ? ", invertierend" : "");
    lv_label_set_text(f_desc[i], desc);
    // Transponder: Mitte des Durchlassbereichs; sonst untere Frequenz
    double dn = t.dn_hi > t.dn_lo ? (t.dn_lo + t.dn_hi) / 2.0 : (double)t.dn_lo;
    double up = t.up_hi > t.up_lo ? (t.up_lo + t.up_hi) / 2.0 : (double)t.up_lo;
    char a[16], b[16], v[80];
    int k = 0;
    v[0] = 0;
    if (dn > 0) { fmt_mhz(satfreq_rx(dn, rr), a, sizeof a); k += snprintf(v + k, sizeof v - k, "RX %s   ", a); }
    if (up > 0) { fmt_mhz(satfreq_tx(up, rr), b, sizeof b); k += snprintf(v + k, sizeof v - k, "TX %s   ", b); }
    snprintf(v + k, sizeof v - k, "%s%s", t.mode, t.dn_hi > t.dn_lo ? "  Mitte" : "");
    lv_label_set_text(f_val[i], v);
    lv_obj_clear_flag(f_desc[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(f_val[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void update() {
  if (sats_detail_seq() == seen_seq && satfreq_seq() == seen_fseq) return;
  seen_seq = sats_detail_seq();
  seen_fseq = satfreq_seq();
  sats_detail_get(*d);
  if (d->norad != cur_norad) return;   // Berechnung laeuft noch

  lv_label_set_text(l_title, d->name);
  lv_label_set_text(l_full, d->full_name);
  lv_obj_set_pos(l_full, 150 + lv_obj_get_width(l_title) + 4, 20);
  const SatState &s = d->st;
  char buf[200];
  snprintf(buf, sizeof buf, "Az %.0f\xC2\xB0  El %.1f\xC2\xB0  %.0f km  %+.2f km/s  Hoehe %.0f km",
           s.az, s.el, s.range_km, d->range_rate, s.alt_km);
  lv_label_set_text(l_live, buf);
  bool visible = d->valid && s.el > 0;
  lv_obj_set_style_text_color(l_live, visible ? lv_color_hex(COL_SAT) : C_TEXT, 0);

  // Status unter dem Diagramm
  time_t now = d->t;
  if (d->geo) {
    snprintf(buf, sizeof buf, visible ? "geostationaer, sichtbar" : "geostationaer, unter dem Horizont");
  } else if (d->n_pass && d->pass[0].aos <= now && d->pass[0].los > now) {
    long r = (long)(d->pass[0].los - now);
    snprintf(buf, sizeof buf, "sichtbar  -  LOS in %ld:%02ld min", r / 60, r % 60);
  } else if (d->n_pass) {
    long r = (long)(d->pass[0].aos - now);
    char t[12];
    fmt_hm(d->pass[0].aos, t, sizeof t, false);
    if (r < 3600) snprintf(buf, sizeof buf, "AOS in %ld:%02ld min (%s), max %.0f\xC2\xB0", r / 60, r % 60, t, d->pass[0].max_el);
    else snprintf(buf, sizeof buf, "AOS in %ld h %02ld min (%s), max %.0f\xC2\xB0", r / 3600, (r % 3600) / 60, t, d->pass[0].max_el);
  } else {
    snprintf(buf, sizeof buf, "kein Ueberflug in den naechsten 36 h");
  }
  lv_label_set_text(l_status, buf);

  // Ueberfluege
  if (d->geo) {
    lv_label_set_text(l_passes, "keine Ueberfluege (geostationaer)");
  } else {
    int k = 0;
    buf[0] = 0;
    for (int i = 0; i < d->n_pass && i < 5; i++) {
      const SatPass &p = d->pass[i];
      char a[16];
      fmt_hm(p.aos, a, sizeof a, true);
      long dur = (long)(p.los - p.aos);
      k += snprintf(buf + k, sizeof buf - k, "%s%s   max %2.0f\xC2\xB0   Az %3.0f\xC2\xB0 > %3.0f\xC2\xB0   %ld:%02ld min",
                    i ? "\n" : "", a, p.max_el, p.az_aos, p.az_los, dur / 60, dur % 60);
      if (k >= (int)sizeof buf - 1) break;
    }
    lv_label_set_text(l_passes, d->n_pass ? buf : "keine in den naechsten 36 h");
  }

  update_polar();
  if (visible && d->polar_n) place(dot_sat, (float)s.az, (float)s.el);
  else if (visible) place(dot_sat, (float)s.az, (float)s.el);
  else lv_obj_add_flag(dot_sat, LV_OBJ_FLAG_HIDDEN);

  update_freqs(visible);
  bool sel = sats_selected() == cur_norad;
  if (sel != lv_obj_has_state(map_btn, LV_STATE_CHECKED)) {
    if (sel) lv_obj_add_state(map_btn, LV_STATE_CHECKED); else lv_obj_clear_state(map_btn, LV_STATE_CHECKED);
  }
}

// ---------- Aufbau ----------
static void on_back(lv_event_t *) {
  sats_detail_open(0);
  lv_scr_load(scr_prev);
  lv_obj_del_async(scr);
}

static void on_delete(lv_event_t *) {
  lv_timer_del(timer);
  scr = nullptr;
}

static void build_polar(lv_obj_t *p) {
  lv_obj_t *box = lv_obj_create(p);
  lv_obj_set_pos(box, PX, PY);
  lv_obj_set_size(box, 2 * PR + 1, 2 * PR + 1);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  // Elevationsringe 0, 30, 60 Grad
  for (int i = 0; i < 3; i++) {
    int r = PR - i * PR / 3;
    lv_obj_t *c = lv_obj_create(box);
    lv_obj_set_size(c, 2 * r, 2 * r);
    lv_obj_set_pos(c, PR - r, PR - r);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x0E1622), 0);
    lv_obj_set_style_bg_opa(c, i == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(c, i == 0 ? lv_color_hex(0x475569) : C_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (i) mk_label(box, PR + 3, PR - r + 1, &lv_font_montserrat_12, C_DIM, i == 1 ? "30" : "60");
  }
  // Achsen
  static lv_point_t ax_v[2] = {{PR, 0}, {PR, 2 * PR}}, ax_h[2] = {{0, PR}, {2 * PR, PR}};
  for (lv_point_t *a : {ax_v, ax_h}) {
    lv_obj_t *l = lv_line_create(box);
    lv_line_set_points(l, a, 2);
    lv_obj_set_style_line_color(l, C_BORDER, 0);
    lv_obj_set_style_line_width(l, 1, 0);
  }
  mk_label(p, PX + PR - 4, PY - 18, &lv_font_montserrat_14, C_TEXT, "N");
  mk_label(p, PX + 2 * PR + 6, PY + PR - 8, &lv_font_montserrat_14, C_TEXT, "O");
  mk_label(p, PX + PR - 4, PY + 2 * PR + 2, &lv_font_montserrat_14, C_TEXT, "S");
  mk_label(p, PX - 18, PY + PR - 8, &lv_font_montserrat_14, C_TEXT, "W");

  track = lv_line_create(box);
  lv_obj_set_style_line_color(track, lv_color_hex(COL_TRACK), 0);
  lv_obj_set_style_line_width(track, 3, 0);
  lv_obj_set_style_line_rounded(track, true, 0);
  lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
  dot_aos = mk_dot(box, 10, COL_AOS);
  dot_los = mk_dot(box, 10, COL_LOS);
  l_aos = mk_label(box, 0, 0, &lv_font_montserrat_12, lv_color_hex(COL_AOS), "AOS");
  l_los = mk_label(box, 0, 0, &lv_font_montserrat_12, lv_color_hex(COL_LOS), "LOS");
  lv_obj_add_flag(l_aos, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(l_los, LV_OBJ_FLAG_HIDDEN);
  dot_sat = mk_dot(box, 16, COL_SAT);
  lv_obj_set_style_border_color(dot_sat, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(dot_sat, 2, 0);
}

void satdetail_open(int norad) {
  if (scr || !norad) return;
  if (!d) d = (SatDetail *)heap_caps_calloc(1, sizeof(SatDetail), MALLOC_CAP_SPIRAM);
  if (!tx) tx = (SatTx *)heap_caps_calloc(SATFREQ_PER_SAT, sizeof(SatTx), MALLOC_CAP_SPIRAM);
  if (!d || !tx) return;
  cur_norad = norad;
  seen_seq = seen_fseq = 0xFFFFFFFF;
  last_polar_n = -1;
  last_aos = 0;
  sats_detail_open(norad);

  scr_prev = lv_scr_act();
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1018), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, on_delete, LV_EVENT_DELETE, nullptr);

  lv_obj_t *b = lv_btn_create(scr);
  lv_obj_set_pos(b, 8, 8);
  lv_obj_set_size(b, 120, 40);
  lv_obj_add_event_cb(b, on_back, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = lv_label_create(b);
  lv_label_set_text(bl, LV_SYMBOL_LEFT "  Zurueck");
  lv_obj_center(bl);

  l_title = mk_label(scr, 150, 8, &lv_font_montserrat_28, C_ACCENT, "...");
  l_full = mk_label(scr, 300, 20, &lv_font_montserrat_14, C_DIM);

  // Bodenspur auf der Karte an/aus
  map_btn = lv_btn_create(scr);
  lv_obj_set_pos(map_btn, 572, 8);
  lv_obj_set_size(map_btn, 220, 40);
  lv_obj_add_flag(map_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_bg_color(map_btn, lv_color_hex(0x1F2937), 0);
  lv_obj_set_style_bg_color(map_btn, lv_color_hex(0xB45309), LV_STATE_CHECKED);
  lv_obj_t *ml = lv_label_create(map_btn);
  lv_label_set_text(ml, LV_SYMBOL_GPS "  Bahn auf Karte");
  lv_obj_center(ml);
  lv_obj_add_event_cb(map_btn, [](lv_event_t *e) {
    sats_select(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? cur_norad : 0);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  if (sats_selected() == norad) lv_obj_add_state(map_btn, LV_STATE_CHECKED);

  build_polar(scr);
  l_status = mk_label(scr, PX - 10, PY + 2 * PR + 24, &lv_font_montserrat_16, C_TEXT);
  mk_label(scr, PX - 10, 456, &lv_font_montserrat_12, C_DIM, "Frequenzen: SatNOGS DB (CC BY-SA 4.0)");

  const int RX = 372;
  l_live = mk_label(scr, RX, 62, &lv_font_montserrat_14, C_TEXT);
  lv_label_set_long_mode(l_live, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(l_live, 800 - RX - 6);
  mk_label(scr, RX, 90, &lv_font_montserrat_14, C_ACCENT, "Naechste Ueberfluege (Ortszeit)");
  l_passes = mk_label(scr, RX, 110, &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_style_text_line_space(l_passes, 2, 0);
  l_fhead = mk_label(scr, RX, 214, &lv_font_montserrat_14, C_ACCENT);
  for (int i = 0; i < MAX_ROWS; i++) {
    f_desc[i] = mk_label(scr, RX, 236 + i * 34, &lv_font_montserrat_12, C_DIM);
    lv_label_set_long_mode(f_desc[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(f_desc[i], 420);
    f_val[i] = mk_label(scr, RX, 250 + i * 34, &lv_font_montserrat_16, C_TEXT);
    lv_obj_add_flag(f_desc[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(f_val[i], LV_OBJ_FLAG_HIDDEN);
  }

  lv_scr_load(scr);
  timer = lv_timer_create([](lv_timer_t *) { update(); }, 250, nullptr);
}
