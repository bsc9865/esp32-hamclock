#pragma once
#include <Arduino.h>

void net_begin();               // WLAN starten
void net_poll();                // aus dem Daten-Task: startet SNTP nach dem ersten Connect
bool net_configured();          // WLAN-Zugangsdaten vorhanden?
bool net_connected();
String net_ip();
int  net_rssi();
bool time_valid();              // NTP synchronisiert?

// HTTP(S)-GET, Ergebnis in out. Liefert true bei HTTP 200.
bool http_get(const char *url, String &out);
