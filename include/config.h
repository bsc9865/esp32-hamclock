#pragma once
// Stations- und Programmeinstellungen
// (spaeter per Touchscreen/Webseite aenderbar und im NVS gespeichert)

#define CALLSIGN  "DJ0SB"
#define LOCATOR   "JO50uh"
#define HOSTNAME  "esp32-hamclock"

// Zeitzone Mitteleuropa mit automatischer Sommerzeit (POSIX-TZ)
#define TZ_INFO   "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER1 "de.pool.ntp.org"
#define NTP_SERVER2 "pool.ntp.org"

// Datenquellen und Abrufintervalle
#define URL_HAMQSL_SOLAR   "https://www.hamqsl.com/solarxml.php"
#define SOLAR_REFRESH_S    (30 * 60)
#define RETRY_S            (2 * 60)

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
