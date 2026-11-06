#pragma once
// DX-Cluster-Client (Telnet, DXSpider) mit Spot-Speicher
#include <stdint.h>
#include "spots.h"

enum DxState : uint8_t { DX_OFF, DX_CONNECTING, DX_ONLINE, DX_ERROR };

void dxcluster_begin();                 // startet den Client-Task (Core 0)
DxState dxcluster_state();
const char *dxcluster_server();         // "host:port"
uint32_t dx_seq();                      // aendert sich bei neuen Spots

// Kopiert die aktuellen Spots (neueste zuerst, juenger als DX_RETENTION_S),
// gefiltert nach Band-Maske. Liefert die Anzahl.
int dx_snapshot(Spot *out, int max, uint16_t band_mask);
