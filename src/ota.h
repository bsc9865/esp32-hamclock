#pragma once
// Firmware-Updates:
//  1. ArduinoOTA im Heimnetz (z.B. "pio run -t upload --upload-port esp32-hamclock.local"), mit Passwort
//  2. Update aus den GitHub-Releases (Pruefung alle 6 h, Installation per Knopfdruck, TLS mit Zertifikatspruefung)
#include <stddef.h>

void ota_begin();
void ota_loop();                    // aus loop(): bedient ArduinoOTA (startet nach dem WLAN-Connect)

bool ota_check_github();            // aus dem Daten-Task: neueste Release-Version abfragen
const char *ota_latest_version();   // z.B. "v0.9.0" ("" = unbekannt)
bool ota_update_available();        // neuer als die laufende Firmware?
bool ota_install_latest();          // blockierend mit Fortschrittsanzeige; startet bei Erfolg neu
// Installation vormerken: laeuft dann in ota_loop() ausserhalb von LVGL-Ereignissen
// (innerhalb eines Klick-Handlers wuerde die Fortschrittsanzeige nicht neu gezeichnet)
void ota_request_install();
void ota_request_check();
bool ota_busy();                    // Pruefung/Installation vorgemerkt oder laufend
void ota_poll_requests();           // aus dem Daten-Task: vorgemerkte Pruefung ausfuehren

// Versionsvergleich "v1.2.3[-...]"; Rueckgabe <0, 0, >0
int ota_compare_versions(const char *a, const char *b);
