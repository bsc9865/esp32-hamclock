#pragma once
// Frequenzen der Favoriten-Satelliten aus der SatNOGS DB (CC BY-SA 4.0), im Dateisystem zwischengespeichert
#include <stdint.h>
#include "satfreq_util.h"

enum SatTxKind : uint8_t { TX_TRANSPONDER = 0, TX_TRANSCEIVER = 1, TX_TRANSMITTER = 2 };

struct SatTx {
  int32_t norad;
  char desc[34];
  char mode[10];
  uint8_t kind;             // SatTxKind
  bool invert;              // invertierender Transponder
  uint64_t up_lo, up_hi;    // Hz, 0 = keiner / kein Bereich
  uint64_t dn_lo, dn_hi;
};

static constexpr int SATFREQ_PER_SAT = 8;

bool satfreq_fetch();                                  // Abruf-Job (holt nur, was fehlt oder aelter als 24 h ist)
int satfreq_get(int norad, SatTx *out, int max);       // Frequenzen eines Satelliten
uint32_t satfreq_seq();
