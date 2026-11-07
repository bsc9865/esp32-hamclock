#pragma once
// Vereinfachtes Ausbreitungsmodell (ohne Arduino, host-testbar)
//
// Grundidee: MUF(3000) = R(Ort) * f(Sonnen-Zenitwinkel)
//   f(chi)  - grober Tagesgang (Nacht ca. 45 % des Mittagswerts, 1 h Verzoegerung)
//   R(Ort)  - aus den Ionosonden (KC2G/GIRO) interpoliert: R_i = MUF_i / f(chi_i)
// Damit folgt die Karte auch zwischen den Stationen dem Tag/Nacht-Verlauf und laesst
// sich fuer einige Stunden grob fortschreiben. Keine VOACAP/P.533-Genauigkeit!
#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct Ionosonde {
  float lat, lon;     // Grad, Laenge -180..180
  float mufd;         // MUF(3000) in MHz
  float fof2;         // MHz
  float cs;           // Vertrauen 0..100, -1 = unbekannt
  time_t t;
  char name[28];
};

// KC2G stations.json parsen; nur Messungen juenger als max_age_s und mit ausreichendem Vertrauen
int prop_parse_kc2g(const char *json, time_t now, int max_age_s, Ionosonde *out, int max);

static constexpr int PROP_GW = 180, PROP_GH = 90;   // 2-Grad-Raster
struct PropModel {
  bool valid;
  int n_stations;
  float r_mean;
  float ratio[PROP_GH][PROP_GW];   // Zeile 0 = 89 Grad Nord, Spalte 0 = -179 Grad
};

float prop_daylight_factor(double lat, double lon, time_t t);      // f(chi)
void prop_build(PropModel &m, const Ionosonde *st, int n, time_t t);
float prop_muf(const PropModel &m, double lat, double lon, time_t t);   // MUF(3000) in MHz

// Bandprognose fuer einen Pfad
static constexpr int PROP_BANDS = 8;
extern const char *const PROP_BAND_NAMES[PROP_BANDS];   // 80 40 30 20 17 15 12 10
extern const float PROP_BAND_MHZ[PROP_BANDS];
enum PropStatus : uint8_t { PROP_CLOSED = 0, PROP_MARGINAL = 1, PROP_OPEN = 2 };

struct PathForecast {
  float dist_km, bearing;
  int hops;
  float muf, luf;          // MHz
  PropStatus band[PROP_BANDS];
};

// drap: optional D-RAP-Raster (90x90, MHz*5), nur fuer "jetzt" sinnvoll; sfi: Solarflux
void prop_path(const PropModel &m, const uint8_t *drap, float sfi, double lat1, double lon1,
               double lat2, double lon2, time_t t, PathForecast &out);
