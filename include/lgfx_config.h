#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "board_pins.h"

// Display-Treiber fuer das 7"-RGB-Panel (Framebuffer im PSRAM).
// Touch und Hintergrundbeleuchtung laufen separat ueber I2C (GT911 / CH422G).
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB   _bus;
  lgfx::Panel_RGB _panel;

  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width  = LCD_WIDTH;
      cfg.memory_height = LCD_HEIGHT;
      cfg.panel_width   = LCD_WIDTH;
      cfg.panel_height  = LCD_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      cfg.pin_d0  = PIN_LCD_B0;
      cfg.pin_d1  = PIN_LCD_B1;
      cfg.pin_d2  = PIN_LCD_B2;
      cfg.pin_d3  = PIN_LCD_B3;
      cfg.pin_d4  = PIN_LCD_B4;
      cfg.pin_d5  = PIN_LCD_G0;
      cfg.pin_d6  = PIN_LCD_G1;
      cfg.pin_d7  = PIN_LCD_G2;
      cfg.pin_d8  = PIN_LCD_G3;
      cfg.pin_d9  = PIN_LCD_G4;
      cfg.pin_d10 = PIN_LCD_G5;
      cfg.pin_d11 = PIN_LCD_R0;
      cfg.pin_d12 = PIN_LCD_R1;
      cfg.pin_d13 = PIN_LCD_R2;
      cfg.pin_d14 = PIN_LCD_R3;
      cfg.pin_d15 = PIN_LCD_R4;

      cfg.pin_henable = PIN_LCD_DE;
      cfg.pin_vsync   = PIN_LCD_VSYNC;
      cfg.pin_hsync   = PIN_LCD_HSYNC;
      cfg.pin_pclk    = PIN_LCD_PCLK;
      cfg.freq_write  = 16000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch  = 8;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch  = 8;
      cfg.pclk_idle_high    = 1;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);
    setPanel(&_panel);
  }
};
