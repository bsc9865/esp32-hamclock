#pragma once
#include <stdint.h>
// Initialisiert I2C, IO-Expander, Touch, RGB-Display und LVGL.
void display_init();
void display_backlight(bool on);
// Bildspeicher des Displays (RGB565, LCD_WIDTH x LCD_HEIGHT, im PSRAM) fuer Screenshots
const uint16_t *display_framebuffer();
