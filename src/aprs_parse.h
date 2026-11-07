#pragma once
// APRS-Paketparser (APRS-IS-Textzeilen), ohne Arduino, host-testbar.
// Unterstuetzt: unkomprimierte und komprimierte Positionen (!, =, /, @), Mic-E (` und '), Objekte (;)
#include <stdint.h>

struct AprsPos {
  char call[12];       // Absender bzw. Objektname
  float lat, lon;
  char sym_table, sym_code;
  bool object;
  char comment[44];    // Rest des Pakets (ASCII)
};

bool aprs_parse(const char *line, AprsPos &out);

enum AprsCat : uint8_t { APRS_MOBILE, APRS_FIXED, APRS_DIGI, APRS_WX, APRS_OTHER, APRS_CATS };
AprsCat aprs_category(char table, char code);
extern const uint32_t APRS_COLORS[APRS_CATS];
extern const char *const APRS_CAT_NAMES[APRS_CATS];
