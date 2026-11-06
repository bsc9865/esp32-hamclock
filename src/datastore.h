#pragma once
// Gemeinsame Daten zwischen Abruf-Task (Core 0) und Oberflaeche (Core 1).
// Die Oberflaeche liest Kopien und erkennt Aenderungen an der Sequenznummer.
#include "solar.h"

void datastore_begin();     // startet den Abruf-Task

uint32_t solar_seq();       // erhoeht sich bei jedem neuen Datensatz
SolarData solar_get();
