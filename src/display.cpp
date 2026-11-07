// Display-Treiber: ESP-IDF esp_lcd RGB-Panel mit Bounce-Buffern
//
// Der Framebuffer liegt im PSRAM. Damit PSRAM-Zugriffe von CPU (LVGL, Karte,
// WLAN/TLS) den Display-DMA nicht ausbremsen (Bild "springt"), wird das Bild
// zeilenweise ueber kleine Bounce-Buffer im internen SRAM ausgegeben.
#include "display.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include "board_pins.h"
#include "board_io.h"

static esp_lcd_panel_handle_t panel;

static constexpr int LCD_PCLK_HZ   = 16 * 1000 * 1000;
static constexpr int BOUNCE_LINES  = 20;   // 800*480 muss durch 2*BOUNCE_LINES*800 teilbar sein
static constexpr int LV_BUF_LINES  = 12;   // 2 x 19 KB intern (vorher 20 Zeilen = 2 x 32 KB)

static lv_disp_draw_buf_t draw_buf;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  // Kopiert synchron in den Framebuffer
  esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
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

static void panel_init() {
  esp_lcd_rgb_panel_config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = LCD_WIDTH;
  cfg.timings.v_res = LCD_HEIGHT;
  cfg.timings.hsync_pulse_width = 4;
  cfg.timings.hsync_back_porch = 8;
  cfg.timings.hsync_front_porch = 8;
  cfg.timings.vsync_pulse_width = 4;
  // laengere vertikale Austastluecke (wie in den Waveshare-Beispielen): mehr Zeit fuer die
  // automatische DMA-Neusynchronisation in VSYNC (CONFIG_LCD_RGB_RESTART_IN_VSYNC)
  cfg.timings.vsync_back_porch = 16;
  cfg.timings.vsync_front_porch = 16;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = LCD_WIDTH * BOUNCE_LINES;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = PIN_LCD_HSYNC;
  cfg.vsync_gpio_num = PIN_LCD_VSYNC;
  cfg.de_gpio_num = PIN_LCD_DE;
  cfg.pclk_gpio_num = PIN_LCD_PCLK;
  cfg.disp_gpio_num = -1;
  const int data_pins[16] = {
    PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
    PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
    PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4};
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data_pins[i];
  cfg.flags.fb_in_psram = 1;

  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

  // Framebuffer schwarz fuellen
  void *fb = nullptr;
  esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb);
  if (fb) memset(fb, 0, LCD_WIDTH * LCD_HEIGHT * 2);
}

void display_backlight(bool on) { ioexp::backlight(on); }

void display_init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  ioexp::begin();
  if (!touch::begin()) Serial.println("WARNUNG: GT911 nicht gefunden!");

  panel_init();
  ioexp::backlight(true);

  lv_init();
  size_t n = LCD_WIDTH * LV_BUF_LINES;
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

  Serial.printf("Display: %dx%d @ %d MHz, Bounce-Buffer %d Zeilen\n",
                LCD_WIDTH, LCD_HEIGHT, LCD_PCLK_HZ / 1000000, BOUNCE_LINES);
}
