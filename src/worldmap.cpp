#include "worldmap.h"
#include <Arduino.h>
#include <math.h>
#include "map_render.h"
#include "astro.h"

static uint16_t *fb;            // Kartenpuffer im PSRAM (RGB565)
static lv_img_dsc_t dsc;
static lv_obj_t *img, *info_box, *info_lbl;
static lv_timer_t *info_timer;
static double qlat, qlon;
static constexpr int MAX_MAP_SPOTS = 60;
static MapSpot *map_spots;
static int n_map_spots = 0;

void worldmap_set_spots(const MapSpot *sp, int n) {
  if (n > MAX_MAP_SPOTS) n = MAX_MAP_SPOTS;
  memcpy(map_spots, sp, n * sizeof(MapSpot));
  n_map_spots = n;
}

void worldmap_render(time_t t) {
  uint32_t t0 = millis();
  map_render(fb, t, qlat, qlon, map_spots, n_map_spots);
  lv_obj_invalidate(img);
  static bool first = true;
  if (first) { Serial.printf("Karte gerendert in %lu ms\n", millis() - t0); first = false; }
}

// ---------- Touch: Info zu einem Punkt ----------
static void hide_info(lv_timer_t *) {
  lv_obj_add_flag(info_box, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(info_timer);
}

void worldmap_show_info(const char *text) {
  lv_label_set_text(info_lbl, text);
  lv_obj_clear_flag(info_box, LV_OBJ_FLAG_HIDDEN);
  lv_timer_reset(info_timer);
  lv_timer_resume(info_timer);
}

static void map_clicked(lv_event_t *e) {
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);
  lv_area_t a;
  lv_obj_get_coords(img, &a);
  int x = p.x - a.x1, y = p.y - a.y1;
  if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
  double lat, lon;
  worldmap_to_latlon(x, y, lat, lon);
  char loc[7];
  latlon_to_maidenhead(lat, lon, loc);
  double d = gc_distance_km(qlat, qlon, lat, lon);
  double b = gc_bearing_deg(qlat, qlon, lat, lon);
  char buf[96];
  snprintf(buf, sizeof buf, "%s   %.1f%c %.1f%c   %.0f km   %.0f\xC2\xB0",
           loc, fabs(lat), lat >= 0 ? 'N' : 'S', fabs(lon), lon >= 0 ? 'E' : 'W', d, b);
  worldmap_show_info(buf);
}

void worldmap_create(lv_obj_t *parent, int x, int y, double qth_lat, double qth_lon) {
  qlat = qth_lat;
  qlon = qth_lon;
  fb = (uint16_t *)heap_caps_malloc(MAP_W * MAP_H * 2, MALLOC_CAP_SPIRAM);
  map_spots = (MapSpot *)heap_caps_malloc(MAX_MAP_SPOTS * sizeof(MapSpot), MALLOC_CAP_SPIRAM);
  memcpy(fb, map_day, MAP_W * MAP_H * 2);

  dsc.header.always_zero = 0;
  dsc.header.w = MAP_W;
  dsc.header.h = MAP_H;
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  dsc.data_size = MAP_W * MAP_H * 2;
  dsc.data = (const uint8_t *)fb;

  img = lv_img_create(parent);
  lv_img_set_src(img, &dsc);
  lv_obj_set_pos(img, x, y);
  lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(img, map_clicked, LV_EVENT_PRESSED, nullptr);

  info_box = lv_obj_create(parent);
  lv_obj_set_size(info_box, MAP_W - 16, LV_SIZE_CONTENT);
  lv_obj_set_pos(info_box, x + 8, y + MAP_H - 62);
  lv_obj_set_style_bg_color(info_box, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(info_box, LV_OPA_70, 0);
  lv_obj_set_style_border_width(info_box, 0, 0);
  lv_obj_set_style_radius(info_box, 4, 0);
  lv_obj_set_style_pad_all(info_box, 6, 0);
  lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  info_lbl = lv_label_create(info_box);
  lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(info_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_add_flag(info_box, LV_OBJ_FLAG_HIDDEN);
  info_timer = lv_timer_create(hide_info, 8000, nullptr);
  lv_timer_pause(info_timer);
}
