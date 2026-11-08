#pragma once
// Satelliten-Verwaltung: TLE-Daten (CelesTrak, im Dateisystem zwischengespeichert),
// Favoriten, Berechnung in eigenem Task, Ergebnis als Momentaufnahme fuer die Oberflaeche
#include <stdint.h>
#include <time.h>
#include "satellite.h"

static constexpr int SAT_MAX_FAV = 16;
static constexpr int SAT_TRACK_N = 120;
static constexpr int SAT_FP_N = 72;

struct SatRow {
  int norad;
  char name[16];
  SatState st;          // aktuelle Position und Blickwinkel
  bool valid;
  bool geo;             // geostationaer (keine Ueberfluege)
  bool has_pass;
  SatPass pass;         // naechster (oder laufender) Ueberflug
};

struct SatView {
  int n;
  SatRow rows[SAT_MAX_FAV];
  int sel_norad;                          // ausgewaehlter Satellit (Bodenspur, Ausleuchtzone), 0 = keiner
  int trk_n;
  float trk_lat[SAT_TRACK_N], trk_lon[SAT_TRACK_N];
  float fp_lat[SAT_FP_N], fp_lon[SAT_FP_N];
  time_t tle_time;                        // Alter der TLE-Daten
  int tle_count;
};

void sats_begin(double qth_lat, double qth_lon);
uint32_t sats_seq();
void sats_get(SatView &out);              // Kopie (ca. 3 KB)
void sats_select(int norad);              // Bodenspur fuer diesen Satelliten (0 = keiner)
int sats_selected();

// Alle Satelliten aus der TLE-Datei (fuer die Auswahl)
int sats_catalog(int *norad, char (*names)[16], int max);
bool sats_is_favorite(int norad);
void sats_set_favorites(const int *norad, int n);   // speichert im NVS

bool sats_fetch_tle();

int sats_favorites(int *norad, int max);  // aktuelle Favoriten

// ---------- Detailansicht eines Satelliten ----------
static constexpr int SAT_DET_PASSES = 6;
static constexpr int SAT_POLAR_N = 64;

struct SatDetail {
  int norad;                              // 0 = keine Daten
  char name[16];
  char full_name[26];
  bool geo;
  bool valid;
  SatState st;                            // jetzt
  double range_rate;                      // km/s, positiv = entfernt sich
  time_t t;                               // Zeitpunkt von st
  int n_pass;
  SatPass pass[SAT_DET_PASSES];           // naechste Ueberfluege (der erste ggf. laufend)
  int polar_n;                            // Bahn des ersten Ueberflugs fuer das Polardiagramm
  float polar_az[SAT_POLAR_N], polar_el[SAT_POLAR_N];
};

void sats_detail_open(int norad);         // Detailberechnung starten (0 = beenden)
uint32_t sats_detail_seq();
void sats_detail_get(SatDetail &out);                    // aus dem Daten-Task (holt nur, wenn veraltet)
