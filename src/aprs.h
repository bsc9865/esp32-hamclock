#pragma once
// APRS-IS-Client (nur Empfang, Login mit pass -1) fuer Stationen im Umkreis des QTH
#include <time.h>
#include "aprs_parse.h"

struct AprsStation {
  AprsPos p;
  time_t t;      // zuletzt gehoert
};

void aprs_begin(double lat, double lon);
uint32_t aprs_seq();
int aprs_snapshot(AprsStation *out, int max);   // neueste zuerst, juenger als APRS_RETENTION_S
bool aprs_online();
