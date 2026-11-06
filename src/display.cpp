#include "display.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "lgfx_config.h"
#include "board_io.h"

static LGFX tft;

static constexpr int BUF_LINES = 40;
static lv_disp_draw_buf_t draw_buf;

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

void display_backlight(bool on) { ioexp::backlight(on); }

void display_init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  ioexp::begin();
  if (!touch::begin()) Serial.println("WARNUNG: GT911 nicht gefunden!");

  tft.init();
  tft.fillScreen(TFT_BLACK);
  ioexp::backlight(true);

  lv_init();
  size_t n = LCD_WIDTH * BUF_LINES;
  auto *b1 = (lv_color_t *)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto *b2 = (lv_color_t *)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&draw_buf, b1, b2, n);

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
}
