// Dashboard-Layout (800x480):
//  +----------------------------------------------------------+  y=0
//  | Rufzeichen  Locator                      WLAN  Version   |  Kopfzeile (40)
//  +------------------+------------------+--------------------+  y=44
//  | Uhr (UTC/Lokal)  | Sonnendaten      | HF-Bedingungen     |  Panels (142)
//  +------------------+------------------+--------------------+  y=192
//  | Weltkarte 560x280                   | Infospalte         |
//  +-------------------------------------+--------------------+  y=480
#include "ui.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "config.h"
#include "net.h"
#include "astro.h"
#include "datastore.h"
#include "worldmap.h"
#include "settings.h"
#include "settings_ui.h"
#include "dxpanel.h"
#include "actpanel.h"
#include "satpanel.h"
#include "spacewx_ui.h"
#include "prop_ui.h"
#include "ota.h"
#include "map_render.h"

#include "ui_theme.h"

static LatLon qth;

// ---------- Helfer ----------
static lv_obj_t *pane(lv_obj_t *parent, int x, int y, int w, int h, const char *title) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_set_pos(p, x, y);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_bg_color(p, C_PANE, 0);
  lv_obj_set_style_border_color(p, C_BORDER, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_radius(p, 6, 0);
  lv_obj_set_style_pad_all(p, 6, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  if (title) {
    lv_obj_t *t = lv_label_create(p);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, C_ACCENT, 0);
    lv_label_set_text(t, title);
    lv_obj_set_pos(t, 0, 0);
  }
  return p;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col,
                       int x, int y, const char *txt = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, txt);
  return l;
}

static void set_text(lv_obj_t *l, const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  lv_label_set_text(l, buf);
}

static lv_color_t cond_color(Cond c) {
  switch (c) {
    case COND_GOOD: return C_GOOD;
    case COND_FAIR: return C_FAIR;
    case COND_POOR: return C_POOR;
    default: return C_DIM;
  }
}
static const char *cond_text(Cond c) {
  switch (c) {
    case COND_GOOD: return "Gut";
    case COND_FAIR: return "Mittel";
    case COND_POOR: return "Schlecht";
    default: return "--";
  }
}

// ---------- Widgets ----------
static lv_obj_t *l_status;
static lv_obj_t *l_utc, *l_local, *l_date;
static lv_obj_t *l_sfi, *l_ssn, *l_a, *l_k, *l_xray, *l_wind, *l_bz, *l_geo;
static lv_obj_t *l_hf[4][2];
static lv_obj_t *l_rise, *l_set, *l_sunalt, *l_moon, *l_aurora, *l_es6, *l_es4, *l_es2, *l_stand, *l_mem;

static void open_settings(lv_event_t *) { settings_ui_open(); }

static void build_header(lv_obj_t *scr) {
  lv_obj_t *call = label(scr, &lv_font_montserrat_28, C_CALL, 10, 6, g_settings.call);
  lv_obj_update_layout(call);
  label(scr, &lv_font_montserrat_16, C_DIM, 22 + lv_obj_get_width(call), 14, g_settings.loc);
  l_status = label(scr, &lv_font_montserrat_14, C_DIM, 0, 14);
  lv_obj_set_width(l_status, 470);
  lv_obj_set_style_text_align(l_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(l_status, 270, 14);

  // Zahnrad: Einstellungen
  lv_obj_t *b = lv_btn_create(scr);
  lv_obj_set_size(b, 46, 36);
  lv_obj_set_pos(b, 748, 4);
  lv_obj_set_style_bg_color(b, C_PANE, 0);
  lv_obj_set_style_border_color(b, C_BORDER, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_ext_click_area(b, 8);
  lv_obj_add_event_cb(b, open_settings, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(l, C_TEXT, 0);
  lv_obj_center(l);
}

static void build_clock(lv_obj_t *scr) {
  lv_obj_t *p = pane(scr, 6, 44, 262, 142, "UTC");
  l_utc = label(p, &lv_font_montserrat_48, C_TEXT, 0, 16, "--:--:--");
  l_local = label(p, &lv_font_montserrat_20, C_TEXT, 0, 74, "--:--:--");
  l_date = label(p, &lv_font_montserrat_16, C_DIM, 0, 102, "");
}

static lv_obj_t *solar_cell(lv_obj_t *p, int col, int row, const char *name) {
  int x = col * 124, y = 22 + row * 28;
  label(p, &lv_font_montserrat_14, C_DIM, x, y + 4, name);
  return label(p, &lv_font_montserrat_20, C_TEXT, x + 50, y, "--");
}

static void build_solar(lv_obj_t *scr) {
  lv_obj_t *p = pane(scr, 272, 44, 258, 142, "Sonne / Geomagnetik  " LV_SYMBOL_RIGHT);
  // Antippen oeffnet die Seite "Weltraumwetter"
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(p, lv_color_hex(0x1A2433), LV_STATE_PRESSED);
  lv_obj_add_event_cb(p, [](lv_event_t *) { spacewx_ui_open(); }, LV_EVENT_CLICKED, nullptr);
  l_sfi  = solar_cell(p, 0, 0, "SFI");
  l_ssn  = solar_cell(p, 1, 0, "SSN");
  l_a    = solar_cell(p, 0, 1, "A");
  l_k    = solar_cell(p, 1, 1, "K");
  l_xray = solar_cell(p, 0, 2, "X-Ray");
  l_bz   = solar_cell(p, 1, 2, "Bz");
  l_wind = solar_cell(p, 0, 3, "Wind");
  l_geo  = solar_cell(p, 1, 3, "Feld");
  lv_obj_set_style_text_font(l_geo, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_font(l_wind, &lv_font_montserrat_16, 0);
}

static void build_bands(lv_obj_t *scr) {
  lv_obj_t *p = pane(scr, 534, 44, 260, 142, "HF-Bedingungen  " LV_SYMBOL_RIGHT);
  // Antippen oeffnet die Seite "Ausbreitung"
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(p, lv_color_hex(0x1A2433), LV_STATE_PRESSED);
  lv_obj_add_event_cb(p, [](lv_event_t *) { prop_ui_open(); }, LV_EVENT_CLICKED, nullptr);
  label(p, &lv_font_montserrat_14, C_DIM, 96, 20, "Tag");
  label(p, &lv_font_montserrat_14, C_DIM, 172, 20, "Nacht");
  for (int i = 0; i < 4; i++) {
    int y = 42 + i * 23;
    label(p, &lv_font_montserrat_16, C_TEXT, 0, y, HF_BAND_NAMES[i]);
    l_hf[i][0] = label(p, &lv_font_montserrat_16, C_DIM, 96, y, "--");
    l_hf[i][1] = label(p, &lv_font_montserrat_16, C_DIM, 172, y, "--");
  }
}

static void build_map(lv_obj_t *scr) {
  worldmap_create(scr, 6, 194, qth.lat, qth.lon);
}

// Rechte Spalte mit zwei Reitern: DX-Spots | Sonne/Mond
static constexpr int TABS = 4;
static lv_obj_t *tab_btn[TABS], *tab_pane[TABS];

static void select_tab(int i) {
  for (int k = 0; k < TABS; k++) {
    if (k == i) {
      lv_obj_clear_flag(tab_pane[k], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_state(tab_btn[k], LV_STATE_CHECKED);
    } else {
      lv_obj_add_flag(tab_pane[k], LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_state(tab_btn[k], LV_STATE_CHECKED);
    }
  }
}

static void tab_clicked(lv_event_t *e) { select_tab((int)(intptr_t)lv_event_get_user_data(e)); }

static void build_info(lv_obj_t *scr) {
  static const char *names[TABS] = {"DX", "Aktiv", "Sat", "Sonne"};
  for (int i = 0; i < TABS; i++) {
    lv_obj_t *b = lv_btn_create(scr);
    lv_obj_set_pos(b, 570 + i * 57, 192);
    lv_obj_set_size(b, 53, 30);
    lv_obj_set_style_pad_hor(b, 2, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, C_PANE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1E3A5F), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(b, C_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_add_event_cb(b, tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, names[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
    tab_btn[i] = b;
    tab_pane[i] = pane(scr, 570, 226, 224, 248, nullptr);
  }

  // Reiter 0: DX-Spots
  dxpanel_create(tab_pane[0], 0, 0, 210, 234, qth.lat, qth.lon);

  // Reiter 1: Aktivierungen (POTA/WWFF)
  actpanel_create(tab_pane[1], 210, qth.lat, qth.lon);

  // Reiter 2: Satelliten
  satpanel_create(tab_pane[2], 210);

  // Reiter 3: Sonne & Mond
  lv_obj_t *p = tab_pane[3];
  const lv_font_t *f = &lv_font_montserrat_16;
  label(p, f, C_DIM, 0, 0, LV_SYMBOL_UP " Aufg.");
  l_rise = label(p, f, C_TEXT, 90, 0, "--:--");
  label(p, f, C_DIM, 0, 22, LV_SYMBOL_DOWN " Unterg.");
  l_set = label(p, f, C_TEXT, 90, 22, "--:--");
  label(p, f, C_DIM, 0, 44, "Hoehe");
  l_sunalt = label(p, f, C_TEXT, 90, 44, "--");
  label(p, f, C_DIM, 0, 66, "Mond");
  l_moon = label(p, &lv_font_montserrat_14, C_TEXT, 90, 68, "--");

  label(p, &lv_font_montserrat_14, C_ACCENT, 0, 96, "VHF");
  label(p, &lv_font_montserrat_14, C_DIM, 0, 116, "Aurora");
  l_aurora = label(p, &lv_font_montserrat_14, C_TEXT, 90, 116, "--");
  label(p, &lv_font_montserrat_14, C_DIM, 0, 134, "Es EU 6m");
  l_es6 = label(p, &lv_font_montserrat_14, C_TEXT, 90, 134, "--");
  label(p, &lv_font_montserrat_14, C_DIM, 0, 152, "Es EU 4m");
  l_es4 = label(p, &lv_font_montserrat_14, C_TEXT, 90, 152, "--");
  label(p, &lv_font_montserrat_14, C_DIM, 0, 170, "Es EU 2m");
  l_es2 = label(p, &lv_font_montserrat_14, C_TEXT, 90, 170, "--");

  char t[40];
  snprintf(t, sizeof t, "QTH %s", g_settings.loc);
  label(p, &lv_font_montserrat_12, C_DIM, 0, 194, t);
  l_stand = label(p, &lv_font_montserrat_12, C_DIM, 70, 194, "Daten: --");
  l_mem = label(p, &lv_font_montserrat_12, C_DIM, 0, 212, "");

  select_tab(0);
}

// ---------- Aktualisierung ----------
static const char *WD[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                           "Donnerstag", "Freitag", "Samstag"};

static void update_clock() {
  if (!time_valid()) {
    lv_label_set_text(l_utc, "--:--:--");
    lv_label_set_text(l_local, net_connected() ? "Warte auf NTP ..." : "keine Zeit");
    return;
  }
  time_t now = time(nullptr);
  struct tm u, l;
  gmtime_r(&now, &u);
  localtime_r(&now, &l);
  set_text(l_utc, "%02d:%02d:%02d", u.tm_hour, u.tm_min, u.tm_sec);
  char tz[8];
  strftime(tz, sizeof tz, "%Z", &l);
  set_text(l_local, "%02d:%02d:%02d %s", l.tm_hour, l.tm_min, l.tm_sec, tz);
  set_text(l_date, "%s, %02d.%02d.%04d", WD[l.tm_wday], l.tm_mday, l.tm_mon + 1, l.tm_year + 1900);
}

static void update_status() {
  if (!net_configured())
    set_text(l_status, LV_SYMBOL_WARNING " WLAN nicht eingerichtet   " FW_VERSION);
  else if (net_connected() && ota_update_available())
    set_text(l_status, LV_SYMBOL_DOWNLOAD " Update %s verfuegbar   " LV_SYMBOL_WIFI " %d dBm   %s", ota_latest_version(), net_rssi(), FW_VERSION);
  else if (net_connected())
    set_text(l_status, LV_SYMBOL_WIFI " %s  %d dBm   %s", net_ip().c_str(), net_rssi(), FW_VERSION);
  else
    set_text(l_status, LV_SYMBOL_WIFI " verbinde ...   " FW_VERSION);
}

static void update_sun_moon() {
  if (!time_valid()) return;
  time_t now = time(nullptr);
  time_t rise, set;
  struct tm t;
  if (sun_rise_set(now, qth.lat, qth.lon, rise, set)) {
    localtime_r(&rise, &t);
    set_text(l_rise, "%02d:%02d", t.tm_hour, t.tm_min);
    localtime_r(&set, &t);
    set_text(l_set, "%02d:%02d", t.tm_hour, t.tm_min);
  }
  double alt, az;
  sun_alt_az(now, qth.lat, qth.lon, alt, az);
  set_text(l_sunalt, "%.1f\xC2\xB0 / %.0f\xC2\xB0", alt, az);
  double age, illum;
  moon_phase(now, age, illum);
  set_text(l_moon, "%s %d%%", moon_phase_name(age), (int)lround(illum * 100));
}

static void update_solar() {
  SolarData s = solar_get();
  if (!s.valid) return;
  set_text(l_sfi, "%d", s.sfi);
  set_text(l_ssn, "%d", s.ssn);
  set_text(l_a, "%d", s.a);
  set_text(l_k, "%d", s.k);
  lv_obj_set_style_text_color(l_k, s.k >= 5 ? C_POOR : s.k == 4 ? C_FAIR : s.k <= 2 ? C_GOOD : C_TEXT, 0);
  lv_obj_set_style_text_color(l_a, s.a >= 30 ? C_POOR : s.a >= 15 ? C_FAIR : C_TEXT, 0);
  set_text(l_xray, "%s", s.xray);
  lv_obj_set_style_text_color(l_xray, s.xray[0] == 'X' ? C_POOR : s.xray[0] == 'M' ? C_FAIR : C_TEXT, 0);
  if (!isnan(s.bz)) {
    set_text(l_bz, "%.1f", s.bz);
    lv_obj_set_style_text_color(l_bz, s.bz <= -10 ? C_POOR : s.bz <= -5 ? C_FAIR : C_TEXT, 0);
  }
  if (!isnan(s.solarwind)) set_text(l_wind, "%.0f km/s", s.solarwind);
  set_text(l_geo, "%s", s.geomag);

  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 2; j++) {
      lv_label_set_text(l_hf[i][j], cond_text(s.hf[i][j]));
      lv_obj_set_style_text_color(l_hf[i][j], cond_color(s.hf[i][j]), 0);
    }

  auto vhf = [](lv_obj_t *l, const char *v) {
    bool closed = strstr(v, "Closed") != nullptr || !*v;
    lv_label_set_text(l, closed ? "geschlossen" : v);
    lv_obj_set_style_text_color(l, closed ? C_DIM : C_GOOD, 0);
  };
  vhf(l_aurora, s.vhf_aurora);
  vhf(l_es6, s.es_eu_6m);
  vhf(l_es4, s.es_eu_4m);
  vhf(l_es2, s.es_eu_2m);
  set_text(l_stand, "N0NBH %s", s.updated);
}

static void timer_fast(lv_timer_t *) {
  update_clock();
  dxpanel_update();
  actpanel_update();
  satpanel_update();
}

// Karte jede Minute neu berechnen (sobald die Uhrzeit gueltig ist)
// Karte neu berechnen: jede Minute (Greyline) oder bei neuen Spots (hoechstens alle 15 s)
static void timer_map(lv_timer_t *) {
  static time_t last = 0;
  if (!time_valid()) return;
  time_t now = time(nullptr);
  bool spots = (dxpanel_map_dirty() || actpanel_map_dirty()) && now - last >= 15;
  bool sats = satpanel_map_dirty() && now - last >= 30;   // Satelliten wandern langsam ueber die Karte
  if (now - last >= 60 || spots || sats) {
    last = now;
    static MapSpot ms[100];
    int n = dxpanel_map_spots(ms, 50);
    n += actpanel_map_spots(ms + n, 84 - n);
    n += satpanel_map_spots(ms + n, 100 - n);
    worldmap_set_spots(ms, n);
    worldmap_render(now);
  }
}

// Speicher-Ueberwachung: Anzeige + alle 5 Minuten eine Zeile auf Serial
static void update_mem() {
  uint32_t fi = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
  uint32_t mi = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
  uint32_t fp = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
  uint32_t up = millis() / 60000;
  set_text(l_mem, "RAM %lu K (min %lu)  PSRAM %lu K  %luh%02lu",
           fi, mi, fp, up / 60, up % 60);
  lv_obj_set_style_text_color(l_mem, mi < 30 ? C_POOR : mi < 60 ? C_FAIR : C_DIM, 0);
  static uint32_t last_log = 0;
  if (millis() - last_log >= 5 * 60000UL || last_log == 0) {
    last_log = millis();
    Serial.printf("[Status] Laufzeit %lu min, intern frei %lu KB (min %lu, Block %u), PSRAM frei %lu KB\n",
                  up, fi, mi, heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024, fp);
  }
}

static void timer_slow(lv_timer_t *) {
  static uint32_t seen_seq = 0;
  update_status();
  update_mem();
  update_sun_moon();
  if (solar_seq() != seen_seq) {
    seen_seq = solar_seq();
    update_solar();
  }
}

void ui_build() {
  maidenhead_to_latlon(g_settings.loc, qth);
  Serial.printf("QTH %s: %.4f N, %.4f E\n", g_settings.loc, qth.lat, qth.lon);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  build_header(scr);
  build_clock(scr);
  build_solar(scr);
  build_bands(scr);
  build_map(scr);
  build_info(scr);

  lv_timer_create(timer_fast, 200, nullptr);
  lv_timer_create(timer_slow, 2000, nullptr);
  lv_timer_create(timer_map, 1000, nullptr);
  timer_fast(nullptr);
  timer_slow(nullptr);

  if (!settings_wifi_configured()) settings_ui_open();   // Ersteinrichtung
}
