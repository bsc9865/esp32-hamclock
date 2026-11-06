// Demo fuer Waveshare ESP32-S3-Touch-LCD-7
// Display (LovyanGFX) + Touch (GT911) + Oberflaeche (LVGL 8)

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "lgfx_config.h"
#include "board_io.h"

static LGFX tft;

// ---------- LVGL-Anbindung ----------
static constexpr int BUF_LINES = 40;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1, *buf2;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&px->full);
  lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  uint16_t x, y;
  if (touch::read(x, y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ---------- Demo-Oberflaeche ----------
static lv_obj_t *lbl_counter, *lbl_slider, *lbl_info, *lbl_touch;
static int counter = 0;

static void btn_cb(lv_event_t *) {
  lv_label_set_text_fmt(lbl_counter, "%d", ++counter);
}

static void slider_cb(lv_event_t *e) {
  lv_obj_t *s = lv_event_get_target(e);
  lv_label_set_text_fmt(lbl_slider, "Wert: %d %%", (int)lv_slider_get_value(s));
}

static void info_timer_cb(lv_timer_t *) {
  uint32_t s = millis() / 1000;
  lv_label_set_text_fmt(lbl_info,
      "Laufzeit: %02lu:%02lu:%02lu\n"
      "Freier Heap: %lu KB\n"
      "Freies PSRAM: %lu KB\n"
      "CPU: %lu MHz",
      s / 3600, (s / 60) % 60, s % 60,
      ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024,
      ESP.getCpuFreqMHz());
  if (touch::lastTouched)
    lv_label_set_text_fmt(lbl_touch, "Touch: x=%u  y=%u", touch::lastX, touch::lastY);
  else
    lv_label_set_text(lbl_touch, "Touch: -");
}

static void build_ui() {
  lv_obj_t *tabs = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 60);

  // --- Tab 1: Bedienelemente ---
  lv_obj_t *t1 = lv_tabview_add_tab(tabs, LV_SYMBOL_HOME "  Start");
  lv_obj_set_flex_flow(t1, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(t1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *card1 = lv_obj_create(t1);
  lv_obj_set_size(card1, 340, 320);
  lv_obj_t *title = lv_label_create(card1);
  lv_label_set_text(title, "Zaehler");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
  lbl_counter = lv_label_create(card1);
  lv_obj_set_style_text_font(lbl_counter, &lv_font_montserrat_48, 0);
  lv_label_set_text(lbl_counter, "0");
  lv_obj_align(lbl_counter, LV_ALIGN_CENTER, 0, -30);
  lv_obj_t *btn = lv_btn_create(card1);
  lv_obj_set_size(btn, 220, 70);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = lv_label_create(btn);
  lv_label_set_text(bl, LV_SYMBOL_PLUS "  Druecken");
  lv_obj_center(bl);

  lv_obj_t *card2 = lv_obj_create(t1);
  lv_obj_set_size(card2, 340, 320);
  lv_obj_t *arc = lv_arc_create(card2);
  lv_obj_set_size(arc, 180, 180);
  lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 0);
  lv_arc_set_value(arc, 40);
  lv_obj_t *sl = lv_slider_create(card2);
  lv_obj_set_width(sl, 260);
  lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -50);
  lv_slider_set_value(sl, 40, LV_ANIM_OFF);
  lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lbl_slider = lv_label_create(card2);
  lv_label_set_text(lbl_slider, "Wert: 40 %");
  lv_obj_align(lbl_slider, LV_ALIGN_BOTTOM_MID, 0, 0);

  // --- Tab 2: Systeminfo ---
  lv_obj_t *t2 = lv_tabview_add_tab(tabs, LV_SYMBOL_SETTINGS "  Info");
  lv_obj_t *head = lv_label_create(t2);
  lv_obj_set_style_text_font(head, &lv_font_montserrat_28, 0);
  lv_label_set_text(head, "ESP32-HamClock  " FW_VERSION);
  lbl_info = lv_label_create(t2);
  lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 0, 60);
  lbl_touch = lv_label_create(t2);
  lv_obj_align(lbl_touch, LV_ALIGN_TOP_LEFT, 0, 200);

  lv_timer_create(info_timer_cb, 250, nullptr);
  info_timer_cb(nullptr);
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32-HamClock " FW_VERSION " ===");
  Serial.printf("PSRAM: %lu KB\n", ESP.getPsramSize() / 1024);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  ioexp::begin();

  if (!touch::begin()) Serial.println("WARNUNG: GT911 nicht gefunden!");

  tft.init();
  tft.fillScreen(TFT_BLACK);
  ioexp::backlight(true);

  lv_init();
  size_t buf_size = LCD_WIDTH * BUF_LINES;
  buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  buf2 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_size);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_WIDTH;
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_cb;
  lv_indev_drv_register(&indev_drv);

  build_ui();
  Serial.println("Setup fertig.");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
