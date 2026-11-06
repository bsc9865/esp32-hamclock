#include "net.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

bool net_configured() {
  return strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "MeinWLAN") != 0;
}

void net_begin() {
  // Zeitzone sofort setzen; SNTP startet erst in net_poll() (siehe dort)
  setenv("TZ", TZ_INFO, 1);
  tzset();
  if (!net_configured()) {
    Serial.println("WLAN nicht konfiguriert (include/secrets.h)");
    return;
  }
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
    Serial.printf("WLAN verbunden, IP %s\n", WiFi.localIP().toString().c_str());
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("WLAN getrennt, Grund %d\n", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.persistent(false);   // keine Flash-Schreibzugriffe (stoeren das Display)
  WiFi.mode(WIFI_STA);

  // Diagnose: sichtbare Netze auflisten
  int n = WiFi.scanNetworks();
  Serial.printf("%d WLANs gefunden:\n", n);
  for (int i = 0; i < n; i++)
    Serial.printf("  %-32s Kanal %2d  %4d dBm  Verschl. %d%s\n", WiFi.SSID(i).c_str(),
                  WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i),
                  WiFi.SSID(i) == WIFI_SSID ? "  <== Ziel" : "");
  WiFi.scanDelete();

  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Verbinde mit WLAN '%s' ...\n", WIFI_SSID);
}

// Wird aus dem Daten-Task aufgerufen.
// Reihenfolge ist wichtig: Der Arduino-Core leert beim ersten hostByName() nach
// einer IP-Aenderung den lwIP-DNS-Cache ohne TCPIP-Lock. Laeuft dabei gerade eine
// DNS-Anfrage von SNTP, stuerzt lwIP ab (assert "Required to lock TCPIP core").
// Deshalb: zuerst einmal selbst aufloesen, danach SNTP starten.
void net_poll() {
  static bool sntp_started = false;
  if (sntp_started || !net_connected()) return;
  IPAddress ip;
  WiFi.hostByName(NTP_SERVER1, ip);
  configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
  sntp_started = true;
  Serial.printf("SNTP gestartet (%s = %s)\n", NTP_SERVER1, ip.toString().c_str());
}

bool net_connected() { return WiFi.status() == WL_CONNECTED; }
String net_ip() { return WiFi.localIP().toString(); }
int net_rssi() { return WiFi.RSSI(); }

bool time_valid() { return time(nullptr) > 1700000000; }

bool http_get(const char *url, String &out) {
  if (!net_connected()) return false;
  WiFiClientSecure secure;
  WiFiClient plain;
  HTTPClient http;
  bool https = strncmp(url, "https", 5) == 0;
  if (https) {
    secure.setInsecure();  // keine Zertifikatspruefung (oeffentliche Daten)
    http.begin(secure, url);
  } else {
    http.begin(plain, url);
  }
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("ESP32-HamClock/" FW_VERSION);
  int code = http.GET();
  bool ok = code == HTTP_CODE_OK;
  if (ok) out = http.getString();
  else Serial.printf("HTTP %d fuer %s\n", code, url);
  http.end();
  return ok;
}
