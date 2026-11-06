#pragma once
// Aktivierungen: POTA (Parks on the Air) und WWFF (World Wide Flora & Fauna)
#include <stdint.h>
#include <time.h>
#include "spots.h"

enum ActType : uint8_t { ACT_POTA = 0, ACT_WWFF = 1, ACT_TYPES };

extern const char *const ACT_NAMES[ACT_TYPES];
extern const uint32_t ACT_COLORS[ACT_TYPES];   // 0xRRGGBB

struct Activation {
  ActType type;
  Band band;
  char call[16];
  double khz;
  char mode[8];
  char ref[16];        // z.B. "US-6549", "DLFF-0123"
  char name[48];       // Park-/Gebietsname (ASCII)
  char loc[12];        // POTA: Region (z.B. "US-TX"), WWFF: leer
  float lat, lon;
  time_t t;            // Spot-Zeit UTC
  char comment[40];
};

void activations_begin();
bool activations_fetch_pota();   // aus dem Daten-Task aufrufen
bool activations_fetch_wwff();
uint32_t act_seq();

// Kopiert Aktivierungen (neueste zuerst), gefiltert nach Typ-Maske (Bit je ActType) und Band-Maske
int act_snapshot(Activation *out, int max, uint8_t type_mask, uint16_t band_mask);

// UTF-8 -> ASCII (Umlaute als ae/oe/ue, sonstige Zeichen als '?'); fuer die eingebauten Schriften
void utf8_to_ascii(const char *in, char *out, size_t n);
// "2026-11-06T18:20:46" (UTC) -> Unix-Zeit
time_t parse_iso_utc(const char *s);
