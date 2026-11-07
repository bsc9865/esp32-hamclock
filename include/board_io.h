#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

// ---------- CH422G IO-Expander ----------
namespace ioexp {
  inline uint8_t state = 0;

  inline void write() {
    Wire.beginTransmission(CH422G_ADDR_OUT);
    Wire.write(state);
    Wire.endTransmission();
  }
  inline void set(uint8_t bits, bool on) {
    if (on) state |= bits; else state &= ~bits;
    write();
  }
  inline void begin() {
    Wire.beginTransmission(CH422G_ADDR_MODE);
    Wire.write(0x01);  // IO0-7 als Ausgaenge
    Wire.endTransmission();
    // Display-Reset pulsen: nach einem Software-Neustart (z.B. OTA) laeuft das Panel sonst mit
    // altem Zustand weiter, waehrend der ESP32 mitten im Bild neu einsetzt (Bild verschoben)
    state = EXIO_USB_SEL | EXIO_SD_CS;                 // LCD im Reset, Backlight aus, Touch im Reset
    write();
    delay(10);
    state |= EXIO_LCD_RST;
    write();
    delay(20);
  }
  inline void backlight(bool on) { set(EXIO_LCD_BL, on); }
}

// ---------- GT911 Touch-Controller ----------
namespace touch {
  inline uint8_t addr = 0x5D;

  inline bool writeReg(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(val);
    return Wire.endTransmission() == 0;
  }
  inline bool readReg(uint16_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
  }

  // Reset-Sequenz: INT waehrend des Resets LOW => I2C-Adresse 0x5D
  inline bool begin() {
    pinMode(PIN_TOUCH_INT, OUTPUT);
    digitalWrite(PIN_TOUCH_INT, LOW);
    ioexp::set(EXIO_TP_RST, false);
    delay(20);
    ioexp::set(EXIO_TP_RST, true);
    delay(10);
    pinMode(PIN_TOUCH_INT, INPUT);
    delay(100);

    uint8_t id[4] = {0};
    if (!readReg(0x8140, id, 4)) {
      addr = 0x14;  // Fallback-Adresse
      if (!readReg(0x8140, id, 4)) return false;
    }
    Serial.printf("GT911 gefunden (0x%02X), Produkt-ID: %c%c%c%c\n",
                  addr, id[0], id[1], id[2], id[3]);
    return true;
  }

  // Liefert true, wenn gerade beruehrt wird (erster Punkt in x/y)
  inline bool lastTouched = false;
  inline uint16_t lastX = 0, lastY = 0;

  inline bool read(uint16_t &x, uint16_t &y) {
    uint8_t status;
    if (readReg(0x814E, &status, 1) && (status & 0x80)) {  // neue Daten?
      uint8_t points = status & 0x0F;
      lastTouched = false;
      if (points > 0 && points <= 5) {
        uint8_t p[4];
        if (readReg(0x8150, p, 4)) {
          lastX = p[0] | (p[1] << 8);
          lastY = p[2] | (p[3] << 8);
          lastTouched = true;
        }
      }
      writeReg(0x814E, 0);  // Status quittieren
    }
    x = lastX; y = lastY;
    return lastTouched;
  }
}
