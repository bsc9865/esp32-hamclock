#pragma once
// Satellitenfrequenzen: Bandfilter und Doppler (ohne Arduino-Abhaengigkeit, auf dem PC testbar)
#include <stdint.h>

// Liegt die Frequenz (Hz) in einem Amateurfunkband mit Satellitenfunk?
static inline bool satfreq_amateur_hz(uint64_t hz) {
  static const uint32_t BANDS_KHZ[][2] = {
      {21000, 21450},       {28000, 29700},       {144000, 146000},     {430000, 440000},
      {1240000, 1300000},   {2400000, 2450000},   {3400000, 3410000},   {5650000, 5670000},
      {5830000, 5850000},   {10450000, 10500000}, {24000000, 24250000}, {47000000, 47200000}};
  uint64_t khz = hz / 1000;
  for (auto &b : BANDS_KHZ)
    if (khz >= b[0] && khz <= b[1]) return true;
  return false;
}

static constexpr double SPEED_OF_LIGHT_KMS = 299792.458;

// Empfangsfrequenz am Boden fuer eine Sendefrequenz des Satelliten.
// range_rate in km/s, positiv = Satellit entfernt sich
static inline double satfreq_rx(double f_down, double range_rate) {
  return f_down * (1.0 - range_rate / SPEED_OF_LIGHT_KMS);
}

// Sendefrequenz am Boden, damit der Satellit auf f_up empfaengt
static inline double satfreq_tx(double f_up, double range_rate) {
  return f_up / (1.0 - range_rate / SPEED_OF_LIGHT_KMS);
}
