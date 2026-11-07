#include "mapscreen.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "map_render.h"
#include "spacewx.h"
#include "settings.h"
#include "astro.h"
#include "settings.h"
#include "spots.h"
#include "dxpanel.h"
#include "actpanel.h"
#include "activations.h"
#include "dxcluster.h"
#include "net.h"
#include "ui_theme.h"

static constexpr int W = 800, H = 480;
static constexpr int MAX_LABELS = 60;
static constexpr float ZOOM_MIN = 1.0f, ZOOM_MAX = 8.0f;

static lv_obj_t *scr, *scr_prev, *img, *slider, *l_zoom, *info_box, *info_lbl;
static lv_obj_t *labels[MAX_LABELS];
static lv_timer_t *timer, *info_timer;
static uint16_t *buf;
static lv_img_dsc_t dsc;
static MapView view;
static float zoom = 1.0f;
static double qlat, qlon;
static lv_point_t drag;            // Verschiebung waehrend des Ziehens
static uint32_t seen_seq;
static time_t last_render;

// ---------- Rendern ----------
static bool add_label(int &k, double lat, double lon, const char *txt, uint32_t color) {
  if (k >= MAX_LABELS) return false;
  double x, y;
  map_view_to_xy(view, lat, lon, x, y);
  if (x < 0 || y < 12 || x > W - 60 || y > H) return true;
  lv_obj_t *l = labels[k++];
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_pos(l, (int)x + 7, (int)y - 9);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);
  return true;
}

static void place_labels() {
  int k = 0;
  if (zoom >= 2.0f) {
    const Spot *sp;
    int n = dxpanel_spots(&sp);
    for (int i = 0; i < n; i++)
      if (sp[i].ent >= 0 && !add_label(k, sp[i].lat, sp[i].lon, sp[i].dx, BAND_COLORS[sp[i].band])) break;
    const Activation *ac;
    int m = actpanel_items(&ac);
    for (int i = 0; i < m; i++)
      if (!add_label(k, ac[i].lat, ac[i].lon, ac[i].call, ACT_COLORS[ac[i].type])) break;
  }
  for (; k < MAX_LABELS; k++) lv_obj_add_flag(labels[k], LV_OBJ_FLAG_HIDDEN);
}

static void render() {
  if (!time_valid()) return;
  uint32_t t0 = millis();
  static MapSpot ms[100];
  int m = dxpanel_map_spots(ms, 60);
  m += actpanel_map_spots(ms + m, 100 - m);
  view = map_view_zoom(W, H, view.lat_c, view.lon_c, zoom);
  last_render = time(nullptr);
  MapOverlays ov;
  if (g_settings.layers & LAYER_AURORA) ov.aurora = spacewx_aurora();
  if (g_settings.layers & LAYER_DRAP) ov.drap = spacewx_drap();
  map_render(buf, view, last_render, qlat, qlon, ms, m, &ov);
  lv_obj_set_pos(img, 0, 0);
  lv_obj_invalidate(img);
  place_labels();
  seen_seq = dx_seq() + act_seq() + spacewx_seq();
  Serial.printf("Vollbild-Karte: Zoom %.1f gerendert in %lu ms\n", zoom, millis() - t0);
}

static void update_zoom_label() {
  char t[16];
  snprintf(t, sizeof t, "%.1fx", zoom);
  lv_label_set_text(l_zoom, t);
}

static void set_zoom(float z) {
  zoom = fminf(ZOOM_MAX, fmaxf(ZOOM_MIN, z));
  lv_slider_set_value(slider, (int)lroundf(zoom * 10), LV_ANIM_OFF);
  update_zoom_label();
  render();
}

// ---------- Info ----------
static void show_info(const char *text) {
  lv_label_set_text(info_lbl, text);
  lv_obj_clear_flag(info_box, LV_OBJ_FLAG_HIDDEN);
  lv_timer_reset(info_timer);
  lv_timer_resume(info_timer);
}

static void tap(int x, int y) {
  // Spot in der Naehe?
  const Spot *sp;
  int n = dxpanel_spots(&sp);
  int best = -1;
  double best_d = 22 * 22;
  for (int i = 0; i < n; i++) {
    if (sp[i].ent < 0) continue;
    double sx, sy;
    map_view_to_xy(view, sp[i].lat, sp[i].lon, sx, sy);
    double d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
    if (d < best_d) { best_d = d; best = i; }
  }
  const Activation *ac;
  int m = actpanel_items(&ac);
  int best_act = -1;
  for (int i = 0; i < m; i++) {
    double sx, sy;
    map_view_to_xy(view, ac[i].lat, ac[i].lon, sx, sy);
    double d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
    if (d < best_d) { best_d = d; best_act = i; best = -1; }
  }
  char text[200];
  if (best_act >= 0) {
    actpanel_format(ac[best_act], text, sizeof text);
  } else if (best >= 0) {
    dxpanel_format_spot(sp[best], text, sizeof text);
  } else {
    double lat, lon;
    map_view_to_latlon(view, x + 0.5, y + 0.5, lat, lon);
    if (lat > 90 || lat < -90) return;
    char loc[7];
    latlon_to_maidenhead(lat, lon, loc);
    snprintf(text, sizeof text, "%s   %.2f%c %.2f%c   %.0f km   %.0f\xC2\xB0", loc, fabs(lat), lat >= 0 ? 'N' : 'S',
             fabs(lon), lon >= 0 ? 'E' : 'W', gc_distance_km(qlat, qlon, lat, lon), gc_bearing_deg(qlat, qlon, lat, lon));
  }
  show_info(text);
}

// ---------- Touch auf der Karte: Ziehen = verschieben, kurzer Tipp = Info ----------
static void img_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) return;
  if (code == LV_EVENT_PRESSED) {
    drag = {0, 0};
  } else if (code == LV_EVENT_PRESSING) {
    lv_point_t v;
    lv_indev_get_vect(indev, &v);
    drag.x += v.x;
    drag.y += v.y;
    if (abs(drag.x) + abs(drag.y) > 8) {
      lv_obj_set_pos(img, drag.x, drag.y);
      for (auto l : labels) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (code == LV_EVENT_RELEASED) {
    if (abs(drag.x) + abs(drag.y) <= 8) {
      lv_point_t p;
      lv_indev_get_point(indev, &p);
      lv_obj_set_pos(img, 0, 0);
      tap(p.x, p.y);
    } else {
      view.lon_c -= drag.x * view.dpp;
      view.lat_c += drag.y * view.dpp;
      render();
    }
  }
}

// ---------- Bedienelemente ----------
static lv_obj_t *mk_btn(int x, int y, int w, int h, const char *txt, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(scr);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_60, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

static void on_back(lv_event_t *) {
  lv_scr_load(scr_prev);
  lv_obj_del_async(scr);   // Puffer wird im DELETE-Ereignis freigegeben
}

static void on_delete(lv_event_t *) {
  lv_timer_del(timer);
  lv_timer_del(info_timer);
  heap_caps_free(buf);
  buf = nullptr;
  scr = nullptr;
}

static void on_qth(lv_event_t *) {
  view.lat_c = qlat;
  view.lon_c = qlon;
  render();
}

static void periodic(lv_timer_t *) {
  time_t now = time(nullptr);
  if (now - last_render >= 60 || (dx_seq() + act_seq() + spacewx_seq() != seen_seq && now - last_render >= 15)) render();
}

void mapscreen_open() {
  if (scr) return;
  buf = (uint16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  if (!buf) return;
  memset(buf, 0, W * H * 2);
  LatLon q;
  maidenhead_to_latlon(g_settings.loc, q);
  qlat = q.lat;
  qlon = q.lon;
  if (zoom <= 1.0f) { view.lat_c = 0; view.lon_c = 0; }

  scr_prev = lv_scr_act();
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x04060C), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, on_delete, LV_EVENT_DELETE, nullptr);

  dsc.header.always_zero = 0;
  dsc.header.w = W;
  dsc.header.h = H;
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  dsc.data_size = W * H * 2;
  dsc.data = (const uint8_t *)buf;
  img = lv_img_create(scr);
  lv_img_set_src(img, &dsc);
  lv_obj_set_pos(img, 0, 0);
  lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(img, img_event, LV_EVENT_ALL, nullptr);

  for (int i = 0; i < MAX_LABELS; i++) {
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(l, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_50, 0);
    lv_obj_set_style_pad_hor(l, 2, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    labels[i] = l;
  }

  mk_btn(8, 8, 120, 40, LV_SYMBOL_LEFT "  Zurueck", on_back);

  // Ebenen: Aurora, D-RAP (umschaltbar, gespeichert)
  static const struct { const char *name; uint8_t bit; uint32_t color; } layer_btns[] = {
      {"Aurora", LAYER_AURORA, 0x28FF6E}, {"D-RAP", LAYER_DRAP, 0xFF461E}};
  for (int i = 0; i < 2; i++) {
    lv_obj_t *b = mk_btn(8, 360 + i * 54, 110, 44, layer_btns[i].name, nullptr);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(b, lv_color_hex(layer_btns[i].color), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, lv_color_hex(0x000000), LV_STATE_CHECKED);
    if (g_settings.layers & layer_btns[i].bit) lv_obj_add_state(b, LV_STATE_CHECKED);
    lv_obj_add_event_cb(b, [](lv_event_t *e) {
      uint8_t bit = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
      settings_save_layers(on ? (g_settings.layers | bit) : (g_settings.layers & ~bit));
      render();
    }, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)layer_btns[i].bit);
  }
  mk_btn(136, 8, 100, 40, LV_SYMBOL_HOME "  QTH", on_qth);

  // Zoom: Plus, Regler, Minus (rechts)
  mk_btn(740, 8, 52, 48, LV_SYMBOL_PLUS, [](lv_event_t *) { set_zoom(zoom < 2 ? zoom + 0.5f : zoom + 1.0f); });
  slider = lv_slider_create(scr);
  lv_obj_set_size(slider, 18, 300);
  lv_obj_set_pos(slider, 757, 84);
  lv_slider_set_range(slider, (int)(ZOOM_MIN * 10), (int)(ZOOM_MAX * 10));
  lv_slider_set_value(slider, (int)(zoom * 10), LV_ANIM_OFF);
  lv_obj_set_ext_click_area(slider, 16);
  lv_obj_add_event_cb(slider, [](lv_event_t *) {
    zoom = lv_slider_get_value(slider) / 10.0f;
    update_zoom_label();
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(slider, [](lv_event_t *) { render(); }, LV_EVENT_RELEASED, nullptr);
  mk_btn(740, 412, 52, 48, LV_SYMBOL_MINUS, [](lv_event_t *) { set_zoom(zoom <= 2 ? zoom - 0.5f : zoom - 1.0f); });
  l_zoom = lv_label_create(scr);
  lv_obj_set_style_text_color(l_zoom, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(l_zoom, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(l_zoom, LV_OPA_60, 0);
  lv_obj_set_style_pad_all(l_zoom, 3, 0);
  lv_obj_set_pos(l_zoom, 682, 20);
  update_zoom_label();

  info_box = lv_obj_create(scr);
  lv_obj_set_size(info_box, 600, LV_SIZE_CONTENT);
  lv_obj_set_pos(info_box, 128, H - 70);
  lv_obj_set_style_bg_color(info_box, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(info_box, LV_OPA_70, 0);
  lv_obj_set_style_border_width(info_box, 0, 0);
  lv_obj_set_style_pad_all(info_box, 8, 0);
  lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  info_lbl = lv_label_create(info_box);
  lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(info_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_add_flag(info_box, LV_OBJ_FLAG_HIDDEN);
  info_timer = lv_timer_create([](lv_timer_t *t) {
    lv_obj_add_flag(info_box, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
  }, 10000, nullptr);
  lv_timer_pause(info_timer);

  lv_scr_load(scr);
  render();
  timer = lv_timer_create(periodic, 1000, nullptr);
}
