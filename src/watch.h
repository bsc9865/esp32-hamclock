#pragma once
// Watchlist und Mode-Klassen fuer DX-Spots (ohne Arduino, host-testbar)
//
// Watchlist: Eintraege durch Komma, Leerzeichen oder Zeilenumbruch getrennt
//   3Y0J      genau dieses Rufzeichen (Zusaetze wie /P werden ignoriert)
//   VP8*      Rufzeichen-Anfang
//   @Bouvet   DXCC-Gebiet nach Namen (Teilstring, ohne Gross-/Kleinschreibung)
//   @3Y/b     DXCC-Gebiet nach Hauptpraefix
#include <stdint.h>
#include "data/cty_data.h"

bool watch_match(const char *list, const char *call, const CtyEntity *ent);

// Mode-Klassen (Bits fuer den Filter)
enum : uint8_t { MODE_CW = 1, MODE_SSB = 2, MODE_DIGI = 4, MODE_OTHER = 8 };
#define MODE_MASK_ALL 0x0F
uint8_t mode_class(const char *mode);
