#pragma once
#include <time.h>

struct LatLon { double lat, lon; };

// Maidenhead-Locator (4 oder 6 Stellen) -> Mittelpunkt des Feldes
bool maidenhead_to_latlon(const char *loc, LatLon &out);

// Sonnenstand
struct SunPos {
  double decl;          // Deklination = Breite des Subsolarpunkts [Grad]
  double subsolar_lon;  // Laenge des Subsolarpunkts [Grad]
  double ra;            // Rektaszension [Grad]
  double gmst;          // Sternzeit Greenwich [Grad]
  double eot_min;       // Zeitgleichung [Minuten]
};
SunPos sun_position(time_t t);

// Hoehe/Azimut der Sonne am Beobachtungsort [Grad]
void sun_alt_az(time_t t, double lat, double lon, double &alt, double &az);

// Auf- und Untergang fuer den lokalen Kalendertag von t.
// Rueckgabe false bei Polartag/-nacht.
bool sun_rise_set(time_t t, double lat, double lon, time_t &rise, time_t &set);

// Mondphase: Alter in Tagen (0..29.5) und beleuchteter Anteil (0..1)
void moon_phase(time_t t, double &age_days, double &illum);
const char *moon_phase_name(double age_days);
