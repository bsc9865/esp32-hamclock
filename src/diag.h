#pragma once
// Diagnose: Neustart-Grund, Speicher, Task-Stacks, Statusseite im Heimnetz (http://esp32-hamclock.local)
#include <stddef.h>

void diag_begin();                    // frueh im setup(): Neustart-Grund ermitteln
const char *diag_reset_reason();      // z.B. "Stromversorgung", "Absturz (Panic)", "Watchdog", "Software/Update"
bool diag_reset_was_crash();          // Panic oder Watchdog
void web_begin();                     // Statusseite starten (eigener Task)
