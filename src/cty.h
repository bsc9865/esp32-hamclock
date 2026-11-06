#pragma once
// Rufzeichen -> DXCC-Entitaet (Land, Kontinent, CQ-Zone, Position) ueber cty.dat
#include "data/cty_data.h"

struct CtyResult {
  const CtyEntity *ent;  // nullptr, wenn unbekannt
  uint8_t cq;
};

CtyResult cty_lookup(const char *call);
