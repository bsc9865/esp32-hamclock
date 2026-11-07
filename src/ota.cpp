#include "ota.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>
#include "config.h"
#include "net.h"
#include "settings.h"
#include "ui_theme.h"

// ESP-IDF-Zertifikatsbuendel (Mozilla-Stammzertifikate), in libmbedtls enthalten
extern const uint8_t crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static char latest[24] = "";
static char asset_url[200] = "";
static bool ota_started = false;
static volatile bool req_install = false, req_check = false, busy = false;

void ota_request_install() { req_install = true; }
void ota_request_check() { req_check = true; }
bool ota_busy() { return req_install || req_check || busy; }

// ---------- Versionen ----------
static void parse_ver(const char *s, int v[3], bool &suffix) {
  v[0] = v[1] = v[2] = 0;
  suffix = false;
  if (*s == 'v' || *s == 'V') s++;
  for (int i = 0; i < 3 && *s; i++) {
    v[i] = atoi(s);
    while (isdigit((unsigned char)*s)) s++;
    if (*s == '.') s++;
    else break;
  }
  suffix = *s == '-';
}

int ota_compare_versions(const char *a, const char *b) {
  int va[3], vb[3];
  bool sa, sb;
  parse_ver(a, va, sa);
  parse_ver(b, vb, sb);
  for (int i = 0; i < 3; i++)
    if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
  return 0;   // Entwickler-Builds (v0.8.0-3-g...) gelten als gleich alt wie v0.8.0
}

const char *ota_latest_version() { return latest; }
bool ota_update_available() { return latest[0] && ota_compare_versions(latest, FW_VERSION) > 0; }

// ---------- Fortschrittsanzeige ----------
static lv_obj_t *ov, *ov_lbl, *ov_bar;

static void overlay_show(const char *title) {
  if (!ov) {
    ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ov, 800, 480);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_80, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_radius(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    ov_lbl = lv_label_create(ov);
    lv_obj_set_style_text_font(ov_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ov_lbl, C_TEXT, 0);
    lv_obj_set_style_text_align(ov_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ov_lbl, LV_ALIGN_CENTER, 0, -40);
    ov_bar = lv_bar_create(ov);
    lv_obj_set_size(ov_bar, 600, 28);
    lv_obj_align(ov_bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(ov_bar, 0, 100);
  }
  lv_label_set_text(ov_lbl, title);
  lv_bar_set_value(ov_bar, 0, LV_ANIM_OFF);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_HIDDEN);
  lv_timer_handler();
}

static void overlay_progress(int pct, const char *title = nullptr) {
  static int last = -1;
  if (pct == last && !title) return;
  last = pct;
  if (title) lv_label_set_text(ov_lbl, title);
  lv_bar_set_value(ov_bar, pct, LV_ANIM_OFF);
  lv_timer_handler();
}

static void overlay_hide() {
  if (ov) lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
}

// ---------- ArduinoOTA ----------
void ota_begin() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([] {
    Serial.println("OTA: Update ueber das Netzwerk beginnt");
    overlay_show(LV_SYMBOL_DOWNLOAD "  Firmware-Update ueber WLAN ...");
  });
  ArduinoOTA.onProgress([](unsigned int cur, unsigned int total) {
    overlay_progress(total ? (int)(cur * 100ULL / total) : 0);
  });
  ArduinoOTA.onEnd([] {
    Serial.println("OTA: fertig, Neustart");
    overlay_progress(100, LV_SYMBOL_OK "  Update fertig - Neustart");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("OTA: Fehler %d\n", (int)e);
    overlay_progress(0, LV_SYMBOL_WARNING "  Update fehlgeschlagen");
    delay(2000);
    overlay_hide();
  });
}

void ota_loop() {
  if (req_check) {
    req_check = false;
    busy = true;
    ota_check_github();
    busy = false;
  }
  if (req_install) {
    req_install = false;
    busy = true;
    ota_install_latest();
    busy = false;
  }
  if (!ota_started) {
    if (!net_connected()) return;
    if (!g_settings.ota_pw[0]) return;   // ohne Passwort kein Update ueber das Netzwerk
    ArduinoOTA.setPassword(g_settings.ota_pw);
    ArduinoOTA.begin();
    ota_started = true;
    Serial.printf("OTA: bereit unter %s.local (%s)\n", HOSTNAME, net_ip().c_str());
  }
  ArduinoOTA.handle();
}

// ---------- GitHub-Releases ----------
static void secure_client(WiFiClientSecure &c) {
  c.setCACertBundle(crt_bundle_start, crt_bundle_end - crt_bundle_start);
  c.setTimeout(20);
}

bool ota_check_github() {
  if (!net_connected()) return false;
  WiFiClientSecure client;
  secure_client(client);
  HTTPClient http;
  http.begin(client, URL_GITHUB_LATEST);
  http.setUserAgent("ESP32-HamClock/" FW_VERSION);
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Update-Pruefung: HTTP %d\n", code);
    http.end();
    return false;
  }
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return false;
  const char *tag = doc["tag_name"] | "";
  asset_url[0] = 0;
  for (JsonObject a : doc["assets"].as<JsonArray>())
    if (strcmp(a["name"] | "", OTA_ASSET_NAME) == 0) strncpy(asset_url, a["browser_download_url"] | "", sizeof asset_url - 1);
  if (!*tag || !asset_url[0]) return false;
  strncpy(latest, tag, sizeof latest - 1);
  Serial.printf("Update-Pruefung: neueste Version %s, laufend %s%s\n", latest, FW_VERSION,
                ota_update_available() ? "  -> Update verfuegbar" : "");
  return true;
}

bool ota_install_latest() {
  if (!asset_url[0] && !ota_check_github()) return false;
  char title[64];
  snprintf(title, sizeof title, LV_SYMBOL_DOWNLOAD "  Installiere %s ...", latest);
  overlay_show(title);
  WiFiClientSecure client;
  secure_client(client);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int cur, int total) { overlay_progress(total > 0 ? (int)(cur * 100LL / total) : 0); });
  Serial.printf("Update: lade %s\n", asset_url);
  t_httpUpdate_return r = httpUpdate.update(client, asset_url);
  if (r == HTTP_UPDATE_OK) {
    overlay_progress(100, LV_SYMBOL_OK "  Update fertig - Neustart");
    Serial.println("Update: erfolgreich, Neustart");
    delay(1500);
    ESP.restart();
    return true;
  }
  Serial.printf("Update: fehlgeschlagen (%d) %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  overlay_progress(0, LV_SYMBOL_WARNING "  Update fehlgeschlagen");
  delay(2500);
  overlay_hide();
  return false;
}
