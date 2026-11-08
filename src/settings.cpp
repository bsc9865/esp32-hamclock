#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include "config.h"
#include "astro.h"
#include "spots.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

Settings g_settings;

static const char *NS = "hamclock";

static void copy(char *dst, size_t n, const String &v) {
  strncpy(dst, v.c_str(), n - 1);
  dst[n - 1] = 0;
}

// getString ohne Fehler-Log, wenn der Schluessel (noch) fehlt
static String get_str(Preferences &p, const char *key, const char *def) {
  return p.isKey(key) ? p.getString(key, def) : String(def);
}

bool settings_valid_call(const char *call) {
  size_t n = strlen(call);
  if (n < 3 || n > 12) return false;
  for (size_t i = 0; i < n; i++) {
    char c = call[i];
    if (!(isupper((unsigned char)c) || isdigit((unsigned char)c) || c == '/' || c == '-')) return false;
  }
  return true;
}

bool settings_valid_cluster(const char *cl) {
  const char *colon = strrchr(cl, ':');
  size_t hl = colon ? (size_t)(colon - cl) : strlen(cl);
  if (hl < 3 || hl > 46 || !memchr(cl, '.', hl)) return false;
  for (size_t i = 0; i < hl; i++) {
    char c = cl[i];
    if (!(isalnum((unsigned char)c) || c == '.' || c == '-')) return false;
  }
  if (!colon) return true;
  if (!colon[1]) return false;
  for (const char *p = colon + 1; *p; p++) if (!isdigit((unsigned char)*p)) return false;
  long port = atol(colon + 1);
  return port >= 1 && port <= 65535;
}

bool settings_normalize_locator(const char *in, char out[7]) {
  size_t n = strlen(in);
  if (n != 4 && n != 6) return false;
  char t[7] = {0};
  for (size_t i = 0; i < n; i++) t[i] = (i < 4) ? toupper(in[i]) : tolower(in[i]);
  LatLon ll;
  if (!maidenhead_to_latlon(t, ll)) return false;
  strcpy(out, t);
  return true;
}

void settings_load() {
  Preferences p;
  p.begin(NS, false);   // legt den Namensraum beim ersten Start an (kein Fehler-Log)
  bool has = p.isKey("call");
  Settings s;
  copy(s.ssid, sizeof s.ssid, get_str(p, "ssid", ""));
  copy(s.pass, sizeof s.pass, get_str(p, "pass", ""));
  copy(s.call, sizeof s.call, get_str(p, "call", CALLSIGN));
  copy(s.loc, sizeof s.loc, get_str(p, "loc", LOCATOR));
  copy(s.cluster, sizeof s.cluster, get_str(p, "cluster", DX_CLUSTER_DEFAULT));
  copy(s.ota_pw, sizeof s.ota_pw, get_str(p, "otapw", ""));
  copy(s.watch, sizeof s.watch, get_str(p, "watch", ""));
  s.mode_mask = p.isKey("modes") ? p.getUChar("modes", 0x0F) : 0x0F;
  bool has_otapw = p.isKey("otapw");
  s.band_mask = p.isKey("bands") ? p.getUShort("bands", BAND_MASK_DEFAULT) : BAND_MASK_DEFAULT;
  s.layers = p.isKey("layers") ? p.getUChar("layers", LAYERS_DEFAULT) : LAYERS_DEFAULT;
  s.aurora_pct = p.isKey("aurpct") ? p.getUChar("aurpct", 100) : 100;
  p.end();

#ifdef WIFI_SSID
  // Einmalige Uebernahme der Entwickler-Zugangsdaten aus secrets.h
  if (!has && strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "MeinWLAN") != 0) {
    copy(s.ssid, sizeof s.ssid, WIFI_SSID);
    copy(s.pass, sizeof s.pass, WIFI_PASSWORD);
    settings_save(s);
    Serial.println("Einstellungen: WLAN aus secrets.h uebernommen");
  }
#else
  (void)has;
#endif
#ifdef OTA_PASSWORD
  // Einmalige Uebernahme des OTA-Passworts aus secrets.h
  if (!has_otapw && strlen(OTA_PASSWORD) > 0) {
    copy(s.ota_pw, sizeof s.ota_pw, OTA_PASSWORD);
    settings_save(s);
    Serial.println("Einstellungen: OTA-Passwort aus secrets.h uebernommen");
  }
#else
  (void)has_otapw;
#endif

  char loc[7];
  if (!settings_normalize_locator(s.loc, loc)) strcpy(loc, LOCATOR);
  strcpy(s.loc, loc);
  g_settings = s;
  Serial.printf("Einstellungen: %s / %s, WLAN '%s'\n", s.call, s.loc, s.ssid);
}

bool settings_save(const Settings &s) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  bool ok = p.putString("ssid", s.ssid) == strlen(s.ssid) || !*s.ssid;
  p.putString("pass", s.pass);
  p.putString("call", s.call);
  p.putString("loc", s.loc);
  p.putString("cluster", s.cluster);
  p.putUShort("bands", s.band_mask);
  p.putUChar("layers", s.layers);
  p.putUChar("aurpct", s.aurora_pct);
  p.putString("otapw", s.ota_pw);
  p.putString("watch", s.watch);
  p.putUChar("modes", s.mode_mask);
  p.end();
  g_settings = s;
  return ok;
}

bool settings_wifi_configured() { return g_settings.ssid[0] != 0; }

bool settings_save_band_mask(uint16_t mask) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putUShort("bands", mask);
  p.end();
  g_settings.band_mask = mask;
  return true;
}

bool settings_save_layers(uint8_t layers) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putUChar("layers", layers);
  p.end();
  g_settings.layers = layers;
  return true;
}

bool settings_save_aurora_pct(uint8_t pct) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putUChar("aurpct", pct);
  p.end();
  g_settings.aurora_pct = pct;
  return true;
}

bool settings_cluster_hostport(char *host, size_t n, uint16_t &port) {
  const char *c = g_settings.cluster;
  const char *colon = strrchr(c, ':');
  size_t len = colon ? (size_t)(colon - c) : strlen(c);
  if (len == 0 || len >= n) return false;
  memcpy(host, c, len);
  host[len] = 0;
  port = colon ? (uint16_t)atoi(colon + 1) : 7300;
  return port != 0;
}

// ---------- Uebernahme aus anderen Tasks ----------
static Settings pending;
static bool pending_set = false, pending_restart = false;
static volatile uint32_t applied_seq = 0;
static portMUX_TYPE pending_mux = portMUX_INITIALIZER_UNLOCKED;   // schuetzt pending*

void settings_request(const Settings &s, bool restart) {
  taskENTER_CRITICAL(&pending_mux);
  pending = s;
  pending_restart = pending_restart || restart;
  pending_set = true;
  taskEXIT_CRITICAL(&pending_mux);
}

uint32_t settings_seq() { return applied_seq; }

void settings_apply_pending() {
  static Settings s;
  bool restart;
  taskENTER_CRITICAL(&pending_mux);
  bool have = pending_set;
  if (have) { s = pending; restart = pending_restart; pending_set = pending_restart = false; }
  taskEXIT_CRITICAL(&pending_mux);
  if (!have) return;
  settings_save(s);
  applied_seq = applied_seq + 1;
  Serial.println("Einstellungen: uebernommen (Web)");
  if (restart) {
    delay(800);   // Antwortseite noch ausliefern lassen
    ESP.restart();
  }
}
