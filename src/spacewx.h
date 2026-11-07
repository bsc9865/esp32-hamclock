#pragma once
// Weltraumwetter von NOAA SWPC: Verlaeufe, Meldungen, Aurora- und D-RAP-Raster
#include "spacewx_parse.h"

struct WxData {
  WxPoint kp[64];    int n_kp;
  WxPoint sfi[40];   int n_sfi;
  WxPoint xray[400]; int n_xray;
  WxAlert alerts[8]; int n_alerts;
};

void spacewx_begin();
uint32_t spacewx_seq();
void spacewx_get(WxData &out);            // Kopie (ca. 9 KB)
const uint8_t *spacewx_aurora();          // AURORA_W x AURORA_H oder nullptr
const uint8_t *spacewx_drap();            // DRAP_W x DRAP_H oder nullptr

// Abrufe (aus dem Daten-Task)
bool spacewx_fetch_kp();
bool spacewx_fetch_sfi();
bool spacewx_fetch_xray();
bool spacewx_fetch_alerts();
bool spacewx_fetch_aurora();
bool spacewx_fetch_drap();
