#pragma once
// Zeit-Hilfsfunktionen ohne Arduino-Abhaengigkeit (host-testbar)
#include <time.h>
#include <stdint.h>

int64_t days_from_civil(int y, unsigned m, unsigned d);
// "2026-11-06T18:20:46", "2026-11-06 18:20:46.123" oder "2026-11-06T18:20:46Z" (UTC) -> Unix-Zeit
time_t parse_iso_utc(const char *s);
