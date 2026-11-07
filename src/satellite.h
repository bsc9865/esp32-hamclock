#pragma once
// Satellitenbahnen (SGP4) ohne Arduino-Abhaengigkeit: Position, Blickwinkel, Ueberfluege
#include <time.h>
#include <stdint.h>
#include "sgp4unit.h"

struct Observer { double lat, lon, alt_km; };

struct SatState {
  double lat, lon, alt_km;   // Subsatellitenpunkt und Hoehe
  double az, el, range_km;   // vom Beobachter aus
};

struct SatPass {
  time_t aos, tmax, los;     // Aufgang, hoechster Punkt, Untergang (UTC)
  float max_el;
  float az_aos, az_max, az_los;
  bool ongoing;              // lief zum Startzeitpunkt bereits
};

class Satellite {
 public:
  bool init(const char *name, const char *line1, const char *line2);
  int norad() const { return (int)rec_.satnum; }
  const char *name() const { return name_; }        // Kurzname, z.B. "ISS", "AO-7", "QO-100"
  const char *full_name() const { return full_; }   // Name aus der TLE
  double period_min() const;
  bool is_geo() const { return period_min() > 1300 && period_min() < 1580; }

  bool state(time_t t, const Observer &o, SatState &s);
  // Ueberfluege im Zeitraum [from, to] mit Mindest-Elevation (Grad)
  int passes(const Observer &o, time_t from, time_t to, float min_el, SatPass *out, int max);
  // Bodenspur: Punkte von from bis to im Abstand step_s
  int ground_track(time_t from, time_t to, int step_s, float *lat, float *lon, int max);

  // Ausleuchtzone (Elevation 0 Grad) als Kreis aus n Punkten um den Subsatellitenpunkt
  static void footprint(double lat, double lon, double alt_km, int n, float *plat, float *plon);
  // Kurzname aus dem TLE-Namen ableiten
  static void short_name(const char *full, char *out, size_t n);

 private:
  bool eci(time_t t, double r[3]);
  double elevation(time_t t, const Observer &o, double *az = nullptr);
  elsetrec rec_;
  char name_[16];
  char full_[26];
};
