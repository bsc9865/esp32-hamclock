#pragma once
// Pinbelegung Waveshare ESP32-S3-Touch-LCD-7

// I2C (CH422G IO-Expander + GT911 Touch)
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_TOUCH_INT 4

// RGB-Display (RGB565, 16 Datenleitungen)
#define PIN_LCD_HSYNC 46
#define PIN_LCD_VSYNC 3
#define PIN_LCD_DE    5
#define PIN_LCD_PCLK  7
// Blau B3..B7
#define PIN_LCD_B0 14
#define PIN_LCD_B1 38
#define PIN_LCD_B2 18
#define PIN_LCD_B3 17
#define PIN_LCD_B4 10
// Gruen G2..G7
#define PIN_LCD_G0 39
#define PIN_LCD_G1 0
#define PIN_LCD_G2 45
#define PIN_LCD_G3 48
#define PIN_LCD_G4 47
#define PIN_LCD_G5 21
// Rot R3..R7
#define PIN_LCD_R0 1
#define PIN_LCD_R1 2
#define PIN_LCD_R2 42
#define PIN_LCD_R3 41
#define PIN_LCD_R4 40

#define LCD_WIDTH  800
#define LCD_HEIGHT 480

// CH422G: I2C-Adressen fuer die Register
#define CH422G_ADDR_MODE 0x24  // Systemparameter (0x01 = IO0-7 als Ausgang)
#define CH422G_ADDR_OUT  0x38  // Ausgangsregister IO0-7

// Bits am CH422G (EXIOx)
#define EXIO_TP_RST  (1 << 1)  // Touch Reset
#define EXIO_LCD_BL  (1 << 2)  // Hintergrundbeleuchtung
#define EXIO_LCD_RST (1 << 3)  // Display Reset
#define EXIO_SD_CS   (1 << 4)  // SD-Karte CS
#define EXIO_USB_SEL (1 << 5)  // USB/CAN-Umschaltung
