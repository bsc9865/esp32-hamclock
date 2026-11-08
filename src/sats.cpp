#include "task_util.h"
#include "sats.h"
#include <atomic>
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/idf_additions.h>
#include "net.h"
#include "config.h"

static constexpr int CATALOG_MAX = 160;
static const char *TLE_FILE = "/amateur.tle";
static const int DEFAULT_FAV[] = {25544, 27607, 44909, 7530, 24278, 39444, 53109, 61781, 43700, 50466, 40908};

static SemaphoreHandle_t mtx;        // Katalog und Favoriten (lange gehalten waehrend der Berechnung)
static SemaphoreHandle_t view_mtx;   // veroeffentlichte Ergebnisse (nur kurz fuer Kopien)
static Satellite *catalog;                // alle Satelliten aus der TLE-Datei (PSRAM)
static int n_catalog = 0;
static int fav[SAT_MAX_FAV], n_fav = 0;
static SatView *view;                     // veroeffentlichte Ergebnisse (PSRAM)
static std::atomic<uint32_t> seq{0};
static volatile int sel = 0;
static volatile bool recalc_passes = true;
static Observer qth;
static time_t tle_time = 0;
static bool fs_ok = false;

uint32_t sats_seq() { return seq; }

// Detailansicht
static volatile int det_norad = 0;
static SatDetail *det;                    // veroeffentlicht (PSRAM), Schutz durch view_mtx
static std::atomic<uint32_t> det_seq{0};

void sats_detail_open(int norad) { det_norad = norad; }
uint32_t sats_detail_seq() { return det_seq; }
void sats_detail_get(SatDetail &out) {
  xSemaphoreTake(view_mtx, portMAX_DELAY);
  out = *det;
  xSemaphoreGive(view_mtx);
}

int sats_favorites(int *norad, int max) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  int n = n_fav < max ? n_fav : max;
  memcpy(norad, fav, n * sizeof(int));
  xSemaphoreGive(mtx);
  return n;
}

void sats_get(SatView &out) {
  xSemaphoreTake(view_mtx, portMAX_DELAY);
  out = *view;
  xSemaphoreGive(view_mtx);
}

void sats_select(int norad) { sel = norad; seq++; }
int sats_selected() { return sel; }

// ---------- Favoriten (NVS) ----------
static void load_favorites() {
  Preferences p;
  p.begin("hamclock", true);
  String s = p.isKey("sats") ? p.getString("sats", "") : String("");
  p.end();
  n_fav = 0;
  if (s.length() == 0) {
    for (int id : DEFAULT_FAV) fav[n_fav++] = id;
    return;
  }
  for (const char *c = s.c_str(); *c && n_fav < SAT_MAX_FAV;) {
    int id = atoi(c);
    if (id > 0) fav[n_fav++] = id;
    const char *k = strchr(c, ',');
    if (!k) break;
    c = k + 1;
  }
}

bool sats_is_favorite(int norad) {
  for (int i = 0; i < n_fav; i++) if (fav[i] == norad) return true;
  return false;
}

void sats_set_favorites(const int *ids, int n) {
  String s;
  xSemaphoreTake(mtx, portMAX_DELAY);
  n_fav = 0;
  for (int i = 0; i < n && n_fav < SAT_MAX_FAV; i++) {
    fav[n_fav++] = ids[i];
    if (s.length()) s += ",";
    s += String(ids[i]);
  }
  xSemaphoreGive(mtx);
  Preferences p;
  p.begin("hamclock", false);
  p.putString("sats", s);
  p.end();
  recalc_passes = true;
}

int sats_catalog(int *norad, char (*names)[16], int max) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  int n = 0;
  for (; n < n_catalog && n < max; n++) {
    norad[n] = catalog[n].norad();
    strncpy(names[n], catalog[n].name(), 15);
    names[n][15] = 0;
  }
  xSemaphoreGive(mtx);
  return n;
}

// ---------- TLE ----------
static int parse_tle(const char *text, Satellite *out, int max) {
  int n = 0;
  const char *p = text;
  char l0[40], l1[80], l2[80];
  auto getline = [&](char *dst, size_t sz) {
    if (!*p) return false;
    const char *e = strchr(p, '\n');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    size_t c = len < sz - 1 ? len : sz - 1;
    memcpy(dst, p, c);
    dst[c] = 0;
    if (c && dst[c - 1] == '\r') dst[c - 1] = 0;
    p = e ? e + 1 : p + len;
    return true;
  };
  while (n < max && getline(l0, sizeof l0) && getline(l1, sizeof l1) && getline(l2, sizeof l2)) {
    if (l1[0] != '1' || l2[0] != '2') continue;
    if (out[n].init(l0, l1, l2)) n++;
  }
  return n;
}

static bool load_catalog(const String &text, time_t when) {
  static Satellite *tmp = (Satellite *)heap_caps_malloc(CATALOG_MAX * sizeof(Satellite), MALLOC_CAP_SPIRAM);
  int n = parse_tle(text.c_str(), tmp, CATALOG_MAX);
  if (n < 10) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  memcpy(catalog, tmp, n * sizeof(Satellite));
  n_catalog = n;
  tle_time = when;
  xSemaphoreGive(mtx);
  recalc_passes = true;
  return true;
}

bool sats_fetch_tle() {
  if (tle_time && time(nullptr) - tle_time < TLE_REFRESH_S) return true;   // noch aktuell
  String body;
  if (!http_get(URL_CELESTRAK_AMATEUR, body)) return false;
  time_t now = time(nullptr);
  if (!load_catalog(body, now)) {
    Serial.println("TLE: Daten unbrauchbar");
    return false;
  }
  if (fs_ok) {
    File f = LittleFS.open(TLE_FILE, "w");
    if (f) {
      f.printf("#%ld\n", (long)now);
      f.print(body);
      f.close();
    }
  }
  Serial.printf("TLE: %d Satelliten von CelesTrak geladen\n", n_catalog);
  return true;
}

static void load_cached() {
  if (!fs_ok || !LittleFS.exists(TLE_FILE)) return;
  File f = LittleFS.open(TLE_FILE, "r");
  String s = f.readString();
  f.close();
  if (!s.startsWith("#")) return;
  int nl = s.indexOf('\n');
  time_t when = (time_t)s.substring(1, nl).toInt();
  if (load_catalog(s.substring(nl + 1), when))
    Serial.printf("TLE: %d Satelliten aus dem Speicher (Stand %ld)\n", n_catalog, (long)when);
}

// ---------- Berechnung ----------
static Satellite *find(int norad) {
  for (int i = 0; i < n_catalog; i++) if (catalog[i].norad() == norad) return &catalog[i];
  return nullptr;
}

// Kopie eines Katalogeintrags unter kurzer Sperre (die Bahnberechnung laeuft danach ohne Sperre,
// damit Oberflaeche und Abruf-Task nicht warten muessen)
static bool copy_sat(int norad, Satellite &out) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  Satellite *s = find(norad);
  if (s) out = *s;
  xSemaphoreGive(mtx);
  return s != nullptr;
}

// Detailwerte fuer den geoeffneten Satelliten (jede Sekunde); Ueberfluege nur bei Bedarf neu
static void update_detail(time_t now) {
  static SatDetail *w = (SatDetail *)heap_caps_calloc(1, sizeof(SatDetail), MALLOC_CAP_SPIRAM);
  static Satellite *s = (Satellite *)heap_caps_calloc(1, sizeof(Satellite), MALLOC_CAP_SPIRAM);
  static time_t last_passes = 0;
  int norad = det_norad;
  if (!norad) { w->norad = 0; return; }
  if (!copy_sat(norad, *s)) return;
  bool fresh = w->norad != norad;
  if (fresh) memset(w, 0, sizeof *w);
  w->norad = norad;
  strncpy(w->name, s->name(), sizeof w->name - 1);
  strncpy(w->full_name, s->full_name(), sizeof w->full_name - 1);
  w->geo = s->is_geo();
  w->t = now;
  SatState a, b;
  w->valid = s->state(now, qth, w->st) && s->state(now - 1, qth, a) && s->state(now + 1, qth, b);
  w->range_rate = w->valid ? (b.range_km - a.range_km) / 2.0 : 0;
  // Ueberfluege: beim Oeffnen, nach Ende des ersten und alle 10 min
  if (!w->geo && (fresh || (w->n_pass && w->pass[0].los < now) || now - last_passes > 600)) {
    w->n_pass = s->passes(qth, now, now + 36 * 3600, 0, w->pass, SAT_DET_PASSES);
    w->polar_n = 0;
    if (w->n_pass) {
      const SatPass &p = w->pass[0];
      int dur = (int)(p.los - p.aos);
      int step = dur / (SAT_POLAR_N - 1);
      if (step < 1) step = 1;
      for (time_t t = p.aos; t <= p.los && w->polar_n < SAT_POLAR_N; t += step) {
        SatState st;
        if (!s->state(t, qth, st)) break;
        w->polar_az[w->polar_n] = (float)st.az;
        w->polar_el[w->polar_n] = (float)(st.el < 0 ? 0 : st.el);
        w->polar_n++;
      }
    }
    last_passes = now;
  }
  xSemaphoreTake(view_mtx, portMAX_DELAY);
  *det = *w;
  xSemaphoreGive(view_mtx);
  det_seq++;
}

static void sat_task(void *) {
  static SatView *work = (SatView *)heap_caps_calloc(1, sizeof(SatView), MALLOC_CAP_SPIRAM);
  static Satellite *sats = (Satellite *)heap_caps_calloc(SAT_MAX_FAV + 1, sizeof(Satellite), MALLOC_CAP_SPIRAM);
  static SatPass passes[SAT_MAX_FAV];
  static bool has_pass[SAT_MAX_FAV];
  static int pass_norad[SAT_MAX_FAV];
  time_t last_pass = 0, last_track = 0, last_full = 0;
  int last_sel = -1;
  int pass_next = SAT_MAX_FAV;   // naechster Favorit, dessen Ueberflug berechnet wird (>= nf: fertig)
  uint32_t pass_t0 = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (time(nullptr) < 1700000000 || n_catalog == 0) continue;
    time_t now = time(nullptr);
    update_detail(now);

    // Favoriten und ausgewaehlten Satelliten kopieren (kurze Sperre)
    int ids[SAT_MAX_FAV];
    bool present[SAT_MAX_FAV];
    xSemaphoreTake(mtx, portMAX_DELAY);
    int nf = n_fav;
    memcpy(ids, fav, sizeof ids);
    for (int i = 0; i < nf; i++) {
      Satellite *c = find(ids[i]);
      present[i] = c != nullptr;
      if (c) sats[i] = *c;
    }
    int cur_sel = sel;
    Satellite *c = cur_sel ? find(cur_sel) : nullptr;
    bool sel_ok = c != nullptr;
    if (c) sats[SAT_MAX_FAV] = *c;
    time_t tle_t = tle_time;
    int n_cat = n_catalog;
    xSemaphoreGive(mtx);

    // Ueberfluege alle 15 min (oder nach Aenderung) bzw. wenn einer vorbei ist; je Durchlauf ein Satellit
    bool expired = false;
    for (int i = 0; i < nf; i++) if (has_pass[i] && passes[i].los < now) expired = true;
    if (pass_next >= nf && (recalc_passes || expired || now - last_pass > 15 * 60)) {
      recalc_passes = false;
      pass_next = 0;
      pass_t0 = millis();
    }
    bool pass_done = false;
    if (pass_next < nf) {
      int i = pass_next++;
      pass_norad[i] = ids[i];
      has_pass[i] = present[i] && sats[i].passes(qth, now, now + 24 * 3600, 0, &passes[i], 1) == 1;
      if (pass_next >= nf) {
        last_pass = now;
        pass_done = true;
        Serial.printf("Satelliten: Ueberfluege fuer %d Favoriten in %lu ms berechnet\n", nf, millis() - pass_t0);
      }
    }

    // Liste, Bodenspur usw. alle 5 s (bzw. sofort nach Aenderung oder neuen Ueberfluegen)
    if (now - last_full < 5 && !pass_done && cur_sel == last_sel) continue;
    last_full = now;

    work->n = 0;
    for (int i = 0; i < nf; i++) {
      if (!present[i]) continue;
      Satellite &s = sats[i];
      SatRow &r = work->rows[work->n++];
      r.norad = ids[i];
      strncpy(r.name, s.name(), sizeof r.name);
      r.valid = s.state(now, qth, r.st);
      r.geo = s.is_geo();
      r.has_pass = has_pass[i] && pass_norad[i] == ids[i];
      if (r.has_pass) r.pass = passes[i];
    }

    // Bodenspur und Ausleuchtzone des ausgewaehlten Satelliten
    work->sel_norad = cur_sel;
    if (sel_ok && (cur_sel != last_sel || now - last_track >= 30)) {
      Satellite &ss = sats[SAT_MAX_FAV];
      double per = ss.period_min() * 60;
      if (per > 6 * 3600) per = 6 * 3600;
      int step = (int)(per / (SAT_TRACK_N - 20));
      if (step < 20) step = 20;
      work->trk_n = ss.ground_track(now - 10 * step, now + (time_t)per, step, work->trk_lat, work->trk_lon, SAT_TRACK_N);
      SatState st;
      if (ss.state(now, qth, st)) Satellite::footprint(st.lat, st.lon, st.alt_km, SAT_FP_N, work->fp_lat, work->fp_lon);
      last_track = now;
      last_sel = cur_sel;
    } else if (!sel_ok) {
      work->trk_n = 0;
      last_sel = 0;
    }
    work->tle_time = tle_t;
    work->tle_count = n_cat;

    xSemaphoreTake(view_mtx, portMAX_DELAY);
    *view = *work;
    xSemaphoreGive(view_mtx);
    seq++;
  }
}

void sats_begin(double qth_lat, double qth_lon) {
  qth = {qth_lat, qth_lon, 0.4};
  mtx = xSemaphoreCreateMutex();
  view_mtx = xSemaphoreCreateMutex();
  catalog = (Satellite *)heap_caps_calloc(CATALOG_MAX, sizeof(Satellite), MALLOC_CAP_SPIRAM);
  view = (SatView *)heap_caps_calloc(1, sizeof(SatView), MALLOC_CAP_SPIRAM);
  det = (SatDetail *)heap_caps_calloc(1, sizeof(SatDetail), MALLOC_CAP_SPIRAM);
  fs_ok = LittleFS.begin(true, "/littlefs", 5, "spiffs");
  if (!fs_ok) Serial.println("LittleFS: nicht verfuegbar");
  load_favorites();
  load_cached();
  // Stack im PSRAM: der Task greift nicht auf den Flash zu (spart internen RAM)
  task_start_psram(sat_task, "sats", 8192, 0);
}
