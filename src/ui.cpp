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

// ---------- Farben ----------
#define C_BG      lv_color_hex(0x0A0E14)
#define C_PANE    lv_color_hex(0x131A24)
#define C_BORDER  lv_color_hex(0x263142)
#define C_ACCENT  lv_color_hex(0x38BDF8)
#define C_TEXT    lv_color_hex(0xE6EDF3)
#define C_DIM     lv_color_hex(0x7D8B9C)
#define C_GOOD    lv_color_hex(0x22C55E)
#define C_FAIR    lv_color_hex(0xEAB308)
#define C_POOR    lv_color_hex(0xEF4444)
#define C_CALL    lv_color_hex(0xFBBF24)

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
static lv_obj_t *l_rise, *l_set, *l_sunalt, *l_moon, *l_aurora, *l_es6, *l_es4, *l_es2, *l_stand;

static void build_header(lv_obj_t *scr) {
  label(scr, &lv_font_montserrat_28, C_CALL, 10, 6, CALLSIGN);
  label(scr, &lv_font_montserrat_16, C_DIM, 130, 14, LOCATOR);
  l_status = label(scr, &lv_font_montserrat_14, C_DIM, 0, 14);
  lv_obj_set_width(l_status, 520);
  lv_obj_set_style_text_align(l_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(l_status, 270, 14);
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
  lv_obj_t *p = pane(scr, 272, 44, 258, 142, "Sonne / Geomagnetik");
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
  lv_obj_t *p = pane(scr, 534, 44, 260, 142, "HF-Bedingungen");
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

static void build_info(lv_obj_t *scr) {
  lv_obj_t *p = pane(scr, 570, 192, 224, 282, "Sonne & Mond @ " LOCATOR);
  const lv_font_t *f = &lv_font_montserrat_16;
  label(p, f, C_DIM, 0, 22, LV_SYMBOL_UP " Aufg.");
  l_rise = label(p, f, C_TEXT, 90, 22, "--:--");
  label(p, f, C_DIM, 0, 44, LV_SYMBOL_DOWN " Unterg.");
  l_set = label(p, f, C_TEXT, 90, 44, "--:--");
  label(p, f, C_DIM, 0, 66, "Hoehe");
  l_sunalt = label(p, f, C_TEXT, 90, 66, "--");
  label(p, f, C_DIM, 0, 88, "Mond");
  l_moon = label(p, &lv_font_montserrat_14, C_TEXT, 90, 90, "--");

  lv_obj_t *t = label(p, &lv_font_montserrat_14, C_ACCENT, 0, 120, "VHF");
  (void)t;
  label(p, &lv_font_montserrat_14, C_DIM, 0, 142, "Aurora");
  l_aurora = label(p, &lv_font_montserrat_14, C_TEXT, 90, 142, "--");
  label(p, &lv_font_montserrat_14, C_DIM, 0, 162, "Es EU 6m");
  l_es6 = label(p, &lv_font_montserrat_14, C_TEXT, 90, 162, "--");
  label(p, &lv_font_montserrat_14, C_DIM, 0, 182, "Es EU 4m");
  l_es4 = label(p, &lv_font_montserrat_14, C_TEXT, 90, 182, "--");
  label(p, &lv_font_montserrat_14, C_DIM, 0, 202, "Es EU 2m");
  l_es2 = label(p, &lv_font_montserrat_14, C_TEXT, 90, 202, "--");

  l_stand = label(p, &lv_font_montserrat_12, C_DIM, 0, 248, "Daten: --");
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
    set_text(l_status, LV_SYMBOL_WARNING " WLAN nicht konfiguriert   " FW_VERSION);
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
  set_text(l_stand, "N0NBH:%s", s.updated);
}

static void timer_fast(lv_timer_t *) { update_clock(); }

// Karte jede Minute neu berechnen (sobald die Uhrzeit gueltig ist)
static void timer_map(lv_timer_t *) {
  static time_t last = 0;
  if (!time_valid()) return;
  time_t now = time(nullptr);
  if (now - last >= 60) {
    last = now;
    worldmap_render(now);
  }
}

static void timer_slow(lv_timer_t *) {
  static uint32_t seen_seq = 0;
  update_status();
  update_sun_moon();
  if (solar_seq() != seen_seq) {
    seen_seq = solar_seq();
    update_solar();
  }
}

void ui_build() {
  maidenhead_to_latlon(LOCATOR, qth);
  Serial.printf("QTH %s: %.4f N, %.4f E\n", LOCATOR, qth.lat, qth.lon);

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
}
