#pragma once
// Konfigurationsseite im Browser: http://esp32-hamclock.local/config
// Geschuetzt per HTTP-Basic-Auth (Benutzer "admin", Kennwort = OTA-Passwort, falls gesetzt)
class WebServer;
void webconfig_register(WebServer &server);
