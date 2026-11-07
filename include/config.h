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

// DX-Cluster (DXSpider, Telnet)
#define DX_CLUSTER_DEFAULT "dxspider.co.uk:7300"
#define DX_LOGIN_SSID      "-7"        // Login als RUFZEICHEN-7 (vermeidet Konflikt mit Logbuch-Programmen)
#define DX_MAX_SPOTS       200
#define DX_RETENTION_S     (30 * 60)

// Aktivierungen (POTA, WWFF)
#define URL_POTA_SPOTS     "https://api.pota.app/spot/activator"
#define URL_WWFF_SPOTS     "https://spots.wwff.co/static/spots.json"
#define ACT_REFRESH_S      120
#define ACT_RETENTION_S    (60 * 60)

// Weltraumwetter (NOAA SWPC)
#define SWPC               "https://services.swpc.noaa.gov/"
#define URL_SWPC_KP        SWPC "products/noaa-planetary-k-index.json"
#define URL_SWPC_SFI       SWPC "products/10cm-flux-30-day.json"
#define URL_SWPC_XRAY      SWPC "json/goes/primary/xrays-6-hour.json"
#define URL_SWPC_ALERTS    SWPC "products/alerts.json"
#define URL_SWPC_AURORA    SWPC "json/ovation_aurora_latest.json"
#define URL_SWPC_DRAP      SWPC "text/drap_global_frequencies.txt"

// Satelliten (CelesTrak bittet: dieselben Daten hoechstens alle 2 h abrufen)
#define URL_CELESTRAK_AMATEUR "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"
#define TLE_REFRESH_S      (12 * 3600)

// Ausbreitung: Ionosonden von KC2G (Daten von GIRO/INGV)
#define URL_KC2G_STATIONS  "https://prop.kc2g.com/api/stations.json"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
