#pragma once
// Ionosonden-Daten (KC2G/GIRO) und das daraus gebaute Ausbreitungsmodell
#include "propagation.h"

void propdata_begin();
bool propdata_fetch();                       // aus dem Daten-Task (alle 15 min)
uint32_t propdata_seq();
bool propdata_stale();                       // nur Messwerte aelter als 2 h (bis 8 h) verfuegbar
const PropModel *propdata_model();           // nullptr, solange keine Daten vorliegen
int propdata_stations(const Ionosonde **out);
// MUF-Raster fuer die Karte zum Zeitpunkt t (zwischengespeichert, wird bei Bedarf neu berechnet)
const uint8_t *propdata_muf_grid(time_t t);
