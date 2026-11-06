#pragma once
#include <time.h>
#include <stdint.h>
#include <math.h>

// Solar- und Ausbreitungsdaten von hamqsl.com (N0NBH)
enum Cond : uint8_t { COND_UNKNOWN = 0, COND_POOR, COND_FAIR, COND_GOOD };

struct SolarData {
  bool valid = false;
  time_t fetched = 0;      // Zeitpunkt des Abrufs
  char updated[32] = "";   // Zeitstempel der Quelle
  int sfi = -1, ssn = -1, a = -1, k = -1;
  char xray[8] = "";
  float solarwind = NAN;   // km/s
  float bz = NAN;          // nT
  float protonflux = NAN;
  float muf = NAN;         // MHz (falls von der Quelle geliefert)
  char geomag[16] = "";
  char noise[12] = "";
  // Baender: 0=80-40m, 1=30-20m, 2=17-15m, 3=12-10m; [0]=Tag [1]=Nacht
  Cond hf[4][2] = {};
  char vhf_aurora[20] = "";
  char es_eu_6m[20] = "", es_eu_4m[20] = "", es_eu_2m[20] = "";
};

extern const char *const HF_BAND_NAMES[4];

bool solar_fetch(SolarData &out);  // laedt und parst die Daten (blockierend)
