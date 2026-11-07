// Einstellungsseite (800x480)
//
//  Einstellungen                                   FW-Version
//  WLAN-Netz [ssid............] [Suchen]   Rufzeichen [......]
//  Passwort  [********........] [x] zeigen Locator    [......]
//  +---------------------------------+     [Speichern & Neustart]
//  | Netzliste                       |     [Abbrechen]
//  |                                 |     Statuszeile
//  +---------------------------------+
//  Bildschirmtastatur (unten, nur beim Tippen eingeblendet)
#include "settings_ui.h"
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "settings.h"
#include "ui_theme.h"
#include "config.h"
#include "ota.h"

static lv_obj_t *scr_prev, *scr;
static lv_obj_t *ta_ssid, *ta_pass, *ta_call, *ta_loc, *ta_cluster, *ta_otapw, *l_fw, *btn_fw;
static lv_obj_t *list, *kb, *l_status, *btn_scan;
static lv_timer_t *scan_timer;

static void status(const char *txt, lv_color_t col) {
  lv_label_set_text(l_status, txt);
  lv_obj_set_style_text_color(l_status, col, 0);
}

static lv_obj_t *mk_label(lv_obj_t *p, int x, int y, const char *t, const lv_font_t *f = &lv_font_montserrat_16,
                          lv_color_t c = C_DIM) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_label_set_text(l, t);
  lv_obj_set_pos(l, x, y);
  return l;
}

static void ta_event(lv_event_t *e) {
  lv_obj_t *ta = lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, (ta == ta_call || ta == ta_loc) ? LV_KEYBOARD_MODE_TEXT_UPPER
                                                             : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(ta, LV_STATE_FOCUSED);
  }
}

static lv_obj_t *mk_ta(lv_obj_t *p, int x, int y, int w, const char *txt, size_t maxlen) {
  lv_obj_t *ta = lv_textarea_create(p);
  lv_obj_set_pos(ta, x, y);
  lv_obj_set_size(ta, w, 44);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, maxlen);
  lv_textarea_set_text(ta, txt);
  lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
  lv_obj_add_event_cb(ta, ta_event, LV_EVENT_ALL, nullptr);
  return ta;
}

// ---------- WLAN-Suche ----------
static void net_selected(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  const char *ssid = (const char *)lv_obj_get_user_data(btn);
  lv_textarea_set_text(ta_ssid, ssid);
  lv_textarea_set_text(ta_pass, "");
  lv_obj_add_state(ta_pass, LV_STATE_FOCUSED);
  lv_event_send(ta_pass, LV_EVENT_FOCUSED, nullptr);
  status("Passwort eingeben", C_TEXT);
}

static void free_list_data() {
  uint32_t n = lv_obj_get_child_cnt(list);
  for (uint32_t i = 0; i < n; i++) {
    void *d = lv_obj_get_user_data(lv_obj_get_child(list, i));
    if (d) free(d);
  }
  lv_obj_clean(list);
}

static void scan_poll(lv_timer_t *) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  lv_timer_del(scan_timer);
  scan_timer = nullptr;
  lv_obj_clear_state(btn_scan, LV_STATE_DISABLED);
  free_list_data();
  if (n <= 0) {
    status(n == 0 ? "Keine Netze gefunden" : "Suche fehlgeschlagen", C_POOR);
    WiFi.scanDelete();
    return;
  }
  // Doppelte SSIDs (mehrere Access Points) nur einmal, staerkstes Signal zuerst
  int shown = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ssid && WiFi.RSSI(j) >= WiFi.RSSI(i)) dup = true;
    if (dup) continue;
    char txt[64];
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    snprintf(txt, sizeof txt, "%s   (%d dBm)%s", ssid.c_str(), WiFi.RSSI(i), open ? "  offen" : "");
    lv_obj_t *b = lv_list_add_btn(list, open ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_CLOSE, txt);
    lv_obj_set_user_data(b, strdup(ssid.c_str()));
    lv_obj_add_event_cb(b, net_selected, LV_EVENT_CLICKED, nullptr);
    shown++;
  }
  WiFi.scanDelete();
  char msg[48];
  snprintf(msg, sizeof msg, "%d Netze gefunden - bitte waehlen", shown);
  status(msg, C_TEXT);
}

static void scan_start(lv_event_t *) {
  if (scan_timer) return;
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();  // laufende Verbindungsversuche stoppen
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    status("Suche konnte nicht gestartet werden", C_POOR);
    return;
  }
  lv_obj_add_state(btn_scan, LV_STATE_DISABLED);
  status("Suche WLAN-Netze ...", C_ACCENT);
  scan_timer = lv_timer_create(scan_poll, 300, nullptr);
}

// ---------- Speichern / Abbrechen ----------
static void close_screen() {
  if (scan_timer) { lv_timer_del(scan_timer); scan_timer = nullptr; }
  free_list_data();
  lv_scr_load(scr_prev);
  lv_obj_del_async(scr);
  scr = nullptr;
}

static void on_cancel(lv_event_t *) {
  if (!settings_wifi_configured() && !*lv_textarea_get_text(ta_ssid)) {
    status("Bitte zuerst ein WLAN einrichten", C_FAIR);
    return;
  }
  close_screen();
}

static void restart_cb(lv_timer_t *) { ESP.restart(); }

static void on_save(lv_event_t *) {
  Settings s = g_settings;
  const char *ssid = lv_textarea_get_text(ta_ssid);
  const char *call = lv_textarea_get_text(ta_call);
  if (!*ssid) { status("WLAN-Netz fehlt", C_POOR); return; }
  if (strlen(call) < 3) { status("Rufzeichen ungueltig", C_POOR); return; }
  char loc[7];
  if (!settings_normalize_locator(lv_textarea_get_text(ta_loc), loc)) {
    status("Locator ungueltig (z.B. JO50uh)", C_POOR);
    return;
  }
  strncpy(s.ssid, ssid, sizeof s.ssid - 1);
  strncpy(s.pass, lv_textarea_get_text(ta_pass), sizeof s.pass - 1);
  size_t i = 0;
  for (; call[i] && i < sizeof s.call - 1; i++) s.call[i] = toupper(call[i]);
  s.call[i] = 0;
  strcpy(s.loc, loc);
  const char *cl = lv_textarea_get_text(ta_cluster);
  if (!*cl || !strchr(cl, '.')) { status("DX-Cluster ungueltig (host:port)", C_POOR); return; }
  strncpy(s.cluster, cl, sizeof s.cluster - 1);
  s.cluster[sizeof s.cluster - 1] = 0;
  strncpy(s.ota_pw, lv_textarea_get_text(ta_otapw), sizeof s.ota_pw - 1);
  s.ota_pw[sizeof s.ota_pw - 1] = 0;
  if (!settings_save(s)) { status("Speichern fehlgeschlagen!", C_POOR); return; }
  status("Gespeichert - Neustart ...", C_GOOD);
  lv_timer_create(restart_cb, 800, nullptr);
}

static void show_pass(lv_event_t *e) {
  bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  lv_textarea_set_password_mode(ta_pass, !on);
}

static lv_obj_t *mk_btn(lv_obj_t *p, int x, int y, int w, const char *txt, lv_event_cb_t cb, lv_color_t bg) {
  lv_obj_t *b = lv_btn_create(p);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, 48);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

// ---------- Firmware ----------
static void update_fw_ui() {
  char buf[96];
  bool avail = ota_update_available();
  if (avail) snprintf(buf, sizeof buf, "Firmware " FW_VERSION "\nNeu: %s verfuegbar", ota_latest_version());
  else if (ota_latest_version()[0]) snprintf(buf, sizeof buf, "Firmware " FW_VERSION "\naktuell (neueste: %s)", ota_latest_version());
  else snprintf(buf, sizeof buf, "Firmware " FW_VERSION "\nUpdate-Stand unbekannt");
  lv_label_set_text(l_fw, buf);
  lv_obj_set_style_text_color(l_fw, avail ? C_GOOD : C_DIM, 0);
  lv_label_set_text(lv_obj_get_child(btn_fw, 0), avail ? LV_SYMBOL_DOWNLOAD " Installieren" : LV_SYMBOL_REFRESH " Pruefen");
  lv_obj_set_style_bg_color(btn_fw, avail ? lv_color_hex(0x15803D) : lv_color_hex(0x1E40AF), 0);
}

static lv_timer_t *fw_timer;

// wartet, bis die vorgemerkte Pruefung (in ota_loop) fertig ist
static void fw_poll(lv_timer_t *t) {
  if (ota_busy()) return;
  lv_timer_del(t);
  fw_timer = nullptr;
  if (!scr) return;
  if (ota_latest_version()[0]) status(ota_update_available() ? "Neue Version gefunden" : "Firmware ist aktuell", C_GOOD);
  else status("Pruefung fehlgeschlagen", C_POOR);
  update_fw_ui();
}

static void on_firmware(lv_event_t *) {
  if (ota_busy()) return;
  if (ota_update_available()) {
    status("Update wird gestartet ...", C_ACCENT);
    ota_request_install();   // Fortschritt und Ergebnis zeigt die Update-Anzeige
    return;
  }
  status("Pruefe GitHub ...", C_ACCENT);
  ota_request_check();
  if (!fw_timer) fw_timer = lv_timer_create(fw_poll, 300, nullptr);
}

void settings_ui_open() {
  if (scr) return;
  scr_prev = lv_scr_act();
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  mk_label(scr, 12, 8, LV_SYMBOL_SETTINGS "  Einstellungen", &lv_font_montserrat_28, C_ACCENT);
  lv_obj_t *v = mk_label(scr, 0, 16, "Firmware " FW_VERSION, &lv_font_montserrat_14);
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -12, 16);

  // Linke Spalte: WLAN
  mk_label(scr, 12, 56, "WLAN-Netz");
  ta_ssid = mk_ta(scr, 110, 48, 200, g_settings.ssid, 32);
  btn_scan = mk_btn(scr, 318, 46, 110, LV_SYMBOL_REFRESH " Suchen", scan_start, lv_color_hex(0x1E40AF));
  mk_label(scr, 12, 108, "Passwort");
  ta_pass = mk_ta(scr, 110, 100, 200, g_settings.pass, 64);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_obj_t *cb = lv_checkbox_create(scr);
  lv_checkbox_set_text(cb, "zeigen");
  lv_obj_set_style_text_color(cb, C_DIM, 0);
  lv_obj_set_pos(cb, 322, 110);
  lv_obj_add_event_cb(cb, show_pass, LV_EVENT_VALUE_CHANGED, nullptr);

  list = lv_list_create(scr);
  lv_obj_set_pos(list, 12, 156);
  lv_obj_set_size(list, 416, 314);
  lv_obj_set_style_bg_color(list, C_PANE, 0);
  lv_obj_set_style_border_color(list, C_BORDER, 0);

  // Rechte Spalte: Station + Aktionen
  mk_label(scr, 452, 56, "Rufzeichen");
  ta_call = mk_ta(scr, 560, 48, 228, g_settings.call, 12);
  mk_label(scr, 452, 108, "Locator");
  ta_loc = mk_ta(scr, 560, 100, 228, g_settings.loc, 6);
  mk_label(scr, 452, 160, "DX-Cluster");
  ta_cluster = mk_ta(scr, 560, 152, 228, g_settings.cluster, 47);
  lv_obj_set_style_text_font(ta_cluster, &lv_font_montserrat_16, 0);
  mk_label(scr, 452, 212, "OTA-Passw.");
  ta_otapw = mk_ta(scr, 560, 204, 228, g_settings.ota_pw, 32);
  lv_textarea_set_password_mode(ta_otapw, true);
  lv_obj_set_style_text_font(ta_otapw, &lv_font_montserrat_16, 0);
  mk_btn(scr, 452, 258, 336, LV_SYMBOL_SAVE "  Speichern & Neustart", on_save, lv_color_hex(0x15803D));
  mk_btn(scr, 452, 312, 336, LV_SYMBOL_CLOSE "  Abbrechen", on_cancel, lv_color_hex(0x374151));
  l_status = mk_label(scr, 452, 368, "", &lv_font_montserrat_14, C_TEXT);

  // Firmware: Version und Update aus den GitHub-Releases
  l_fw = mk_label(scr, 452, 410, "", &lv_font_montserrat_14, C_DIM);
  lv_obj_set_width(l_fw, 170);
  lv_label_set_long_mode(l_fw, LV_LABEL_LONG_WRAP);
  btn_fw = mk_btn(scr, 628, 404, 160, "", on_firmware, lv_color_hex(0x1E40AF));
  lv_obj_set_style_text_font(lv_obj_get_child(btn_fw, 0), &lv_font_montserrat_16, 0);
  update_fw_ui();
  lv_obj_set_width(l_status, 336);
  lv_label_set_long_mode(l_status, LV_LABEL_LONG_WRAP);

  kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, 800, 200);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_scr_load(scr);
  if (!settings_wifi_configured()) {
    status("Willkommen! Bitte WLAN einrichten.", C_ACCENT);
    scan_start(nullptr);
  }
}
