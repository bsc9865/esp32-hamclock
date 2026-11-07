#pragma once
// Parser fuer NOAA-SWPC-Daten (ohne Arduino, host-testbar)
#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct WxPoint { time_t t; float v; };

struct WxAlert {
  time_t t;
  char title[72];     // z.B. "WATCH: Geomagnetic Storm Category G1 Predicted"
  char text[640];     // Meldungstext (gekuerzt, ASCII, mit Zeilenumbruechen)
};

// Kp-Index: products/noaa-planetary-k-index.json
int wx_parse_kp(const char *json, WxPoint *out, int max);
// 10,7-cm-Flux: products/10cm-flux-30-day.json
int wx_parse_flux(const char *json, WxPoint *out, int max);
// GOES-Roentgenfluss, nur Kanal 0.1-0.8 nm: json/goes/primary/xrays-6-hour.json
int wx_parse_xray(const char *json, WxPoint *out, int max);
// Meldungen: products/alerts.json (neueste zuerst)
int wx_parse_alerts(const char *json, WxAlert *out, int max);

// Roentgen-Klasse aus Fluss in W/m^2, z.B. 1.2e-6 -> "C1.2"
void wx_xray_class(float flux, char out[8]);

// Aurora-Vorhersage (OVATION) als Stream: json/ovation_aurora_latest.json
// Raster: 181 Zeilen (Breite -90..90) x 360 Spalten (Laenge 0..359), Wahrscheinlichkeit 0..100
static constexpr int AURORA_W = 360, AURORA_H = 181;
class OvationParser {
 public:
  explicit OvationParser(uint8_t *grid);   // grid: AURORA_W * AURORA_H Bytes
  void feed(const char *data, size_t len);
  int count() const { return n_; }          // Anzahl gelesener Punkte
 private:
  uint8_t *grid_;
  bool in_coords_ = false;
  char key_[16];
  int key_len_ = 0;
  int num_ = 0, sign_ = 1, idx_ = 0, n_ = 0;
  bool in_num_ = false;
  bool frac_ = false;     // Nachkommastellen werden ueberlesen
  int trip_[3];
};

// D-RAP: text/drap_global_frequencies.txt
// Raster 45 Zeilen (Breite 89..-89, Schritt 2) x 90 Spalten (Laenge -178..178, Schritt 4)
// Wert = hoechste beeintraechtigte Frequenz in MHz * 5 (0..255)
static constexpr int DRAP_W = 90, DRAP_H = 45;
bool wx_parse_drap(const char *text, uint8_t *grid);
