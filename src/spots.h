#pragma once
// DX-Spots: Datenmodell, Baender, Parser fuer DX-Cluster-Zeilen (ohne Arduino, host-testbar)
#include <stdint.h>
#include <time.h>

enum Band : uint8_t {
  B_160, B_80, B_60, B_40, B_30, B_20, B_17, B_15, B_12, B_10, B_6, B_4, B_2, B_70CM,
  B_OTHER, BAND_COUNT
};

extern const char *const BAND_NAMES[BAND_COUNT];
extern const uint32_t BAND_COLORS[BAND_COUNT];   // 0xRRGGBB

Band band_from_khz(double khz);
const char *mode_guess(double khz, const char *comment);

#define BAND_MASK_DEFAULT 0x07FF   // 160 m bis 6 m

struct Spot {
  double khz = 0;
  char dx[16] = "";
  char spotter[16] = "";
  char comment[34] = "";
  char mode[6] = "";
  time_t t = 0;          // Spot-Zeit (UTC)
  Band band = B_OTHER;
  int16_t ent = -1;      // Index in cty_entities, -1 = unbekannt
  uint8_t cq = 0;
  float lat = 0, lon = 0;
};

// Parst eine Zeile "DX de SPOTTER:  14074.0  DXCALL  Kommentar  1200Z".
// now = aktuelle UTC-Zeit (fuer das Datum). Laender-Info wird ueber cty ergaenzt.
bool spot_parse(const char *line, time_t now, Spot &out);
