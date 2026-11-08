#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

void net_begin();               // WLAN starten
void net_poll();                // aus dem Daten-Task: startet SNTP nach dem ersten Connect
bool net_configured();          // WLAN-Zugangsdaten vorhanden?
bool net_connected();
String net_ip();
int  net_rssi();
bool time_valid();              // NTP synchronisiert?

// TLS-Client mit Pruefung des Server-Zertifikats (Mozilla-Stammzertifikate aus ESP-IDF)
void net_secure_client(WiFiClientSecure &c);

// HTTP(S)-GET, Ergebnis in out. Liefert true bei HTTP 200.
bool http_get(const char *url, String &out);

// HTTP(S)-GET als Stream: cb wird fuer jeden empfangenen Block aufgerufen (fuer grosse Antworten)
typedef void (*http_chunk_cb)(const char *data, size_t len, void *ctx);
bool http_stream(const char *url, http_chunk_cb cb, void *ctx);
