#include "propagation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "astro.h"
#include "json_scan.h"
#include "timeutil.h"

static constexpr double DEG = M_PI / 180.0;
static constexpr double RE = 6371.0;

const char *const PROP_BAND_NAMES[PROP_BANDS] = {"80", "40", "30", "20", "17", "15", "12", "10"};
const float PROP_BAND_MHZ[PROP_BANDS] = {3.6f, 7.1f, 10.12f, 14.15f, 18.1f, 21.2f, 24.94f, 28.5f};

// ---------- KC2G-Parser ----------
static double num(const char *v) {
  if (!v || *v == 'n') return NAN;   // null
  return atof(*v == '"' ? v + 1 : v);
}

int prop_parse_kc2g(const char *json, time_t now, int max_age_s, Ionosonde *out, int max) {
  int n = 0;
  if (max <= 0) return 0;
  // Top-Level-Objekte durchlaufen (enthalten ein verschachteltes "station"-Objekt)
  json_for_each_object(json, [&](const char *p, const char *e) {
    Ionosonde s{};
    s.mufd = (float)num(json_find_key(p, e, "mufd"));
    s.fof2 = (float)num(json_find_key(p, e, "fof2"));
    s.cs = (float)num(json_find_key(p, e, "cs"));
    s.lat = (float)num(json_find_key(p, e, "latitude"));
    float lon = (float)num(json_find_key(p, e, "longitude"));
    s.lon = lon > 180 ? lon - 360 : lon;
    const char *tv = json_find_key(p, e, "time");
    s.t = tv && *tv == '"' ? parse_iso_utc(tv + 1) : 0;
    const char *nm = json_find_key(p, e, "name");
    if (nm && *nm == '"') {
      size_t i = 0;
      for (nm++; *nm && *nm != '"' && i < sizeof s.name - 1; nm++)
        if ((unsigned char)*nm < 0x80) s.name[i++] = *nm;
      s.name[i] = 0;
    }
    bool fresh = s.t > 0 && now - s.t <= max_age_s && s.t <= now + 600;
    bool trusted = isnan(s.cs) || s.cs < 0 || s.cs >= 30;
    if (fresh && trusted && !isnan(s.mufd) && s.mufd > 1 && s.mufd < 60 && fabsf(s.lat) <= 90 && fabsf(s.lon) <= 180)
      out[n++] = s;
    return n < max;
  });
  return n;
}

// ---------- Modell ----------
// Parameter per Kreuzvalidierung mit KC2G-Daten (07.11.2026, 24 Ionosonden) gewaehlt:
//  - die F2-Schicht folgt dem Sonnenstand mit ca. 1 h Verzoegerung
//  - Nacht-MUF ca. 45 % des Mittagswerts
// Mittlerer Fehler an weggelassenen Stationen: 3,9 MHz (ohne diese Anpassung 4,8 MHz).
static constexpr int IONO_LAG_S = 3600;
static constexpr double NIGHT_FLOOR = 0.45;

static float daylight_factor(const SunPos &s, double lat, double lon) {
  double H = (s.gmst + lon - s.ra) * DEG;
  double c = sin(lat * DEG) * sin(s.decl * DEG) + cos(lat * DEG) * cos(s.decl * DEG) * cos(H);
  // weicher Uebergang ueber den Terminator (bis ca. 6 Grad unter dem Horizont)
  double x = (c + 0.1) / 1.1;
  if (x < 0) x = 0;
  if (x > 1) x = 1;
  return (float)(NIGHT_FLOOR + (1.0 - NIGHT_FLOOR) * sqrt(x));
}

float prop_daylight_factor(double lat, double lon, time_t t) {
  return daylight_factor(sun_position(t - IONO_LAG_S), lat, lon);
}

static double gc_km(double la1, double lo1, double la2, double lo2) {
  double c = sin(la1 * DEG) * sin(la2 * DEG) + cos(la1 * DEG) * cos(la2 * DEG) * cos((lo2 - lo1) * DEG);
  return acos(c > 1 ? 1 : (c < -1 ? -1 : c)) * RE;
}

void prop_build(PropModel &m, const Ionosonde *st, int n, time_t t, void (*yield_cb)()) {
  m.valid = n > 0;
  m.n_stations = n;
  if (!n) return;
  if (n > 128) n = 128;
  // Stationen als Einheitsvektoren; Entfernung ueber die Sehne (keine Winkelfunktionen pro Paar)
  float r[128], sx[128], sy[128], sz[128];
  double sum = 0;
  for (int i = 0; i < n; i++) {
    r[i] = st[i].mufd / prop_daylight_factor(st[i].lat, st[i].lon, st[i].t ? st[i].t : t);
    sum += r[i];
    float la = st[i].lat * (float)DEG, lo = st[i].lon * (float)DEG;
    sx[i] = cosf(la) * cosf(lo);
    sy[i] = cosf(la) * sinf(lo);
    sz[i] = sinf(la);
  }
  m.r_mean = (float)(sum / n);
  const float RE_F = (float)RE;
  const float soft2 = 800.0f * 800.0f;          // 800 km Glaettung: weniger "Inseln" um Einzelstationen
  const float w0 = 1.0f / (3500.0f * 3500.0f);  // Gewicht des globalen Mittelwerts (wirkt fern der Stationen)
  for (int gy = 0; gy < PROP_GH; gy++) {
    float la = (89.0f - gy * 2.0f) * (float)DEG;
    float cla = cosf(la), sla = sinf(la);
    for (int gx = 0; gx < PROP_GW; gx++) {
      float lo = (-179.0f + gx * 2.0f) * (float)DEG;
      float x = cla * cosf(lo), y = cla * sinf(lo), z = sla;
      float ws = w0, rs = w0 * m.r_mean;
      for (int i = 0; i < n; i++) {
        float dx = x - sx[i], dy = y - sy[i], dz = z - sz[i];
        float d2 = (dx * dx + dy * dy + dz * dz) * RE_F * RE_F;   // Sehne^2 in km^2
        float w = 1.0f / (d2 + soft2);
        ws += w;
        rs += w * r[i];
      }
      m.ratio[gy][gx] = rs / ws;
    }
    if (yield_cb) yield_cb();
  }
}

static float ratio_at(const PropModel &m, double lat, double lon) {
  // bilinear im 2-Grad-Raster
  double fy = (89.0 - lat) / 2.0, fx = (lon + 179.0) / 2.0;
  if (fy < 0) fy = 0;
  if (fy > PROP_GH - 1) fy = PROP_GH - 1;
  int y0 = (int)fy, y1 = y0 + 1 < PROP_GH ? y0 + 1 : y0;
  double ty = fy - y0;
  fx = fmod(fx + PROP_GW, PROP_GW);
  int x0 = (int)fx, x1 = (x0 + 1) % PROP_GW;
  double tx = fx - x0;
  double a = m.ratio[y0][x0] * (1 - tx) + m.ratio[y0][x1] * tx;
  double b = m.ratio[y1][x0] * (1 - tx) + m.ratio[y1][x1] * tx;
  return (float)(a * (1 - ty) + b * ty);
}

float prop_muf(const PropModel &m, double lat, double lon, time_t t) {
  if (!m.valid) return NAN;
  return ratio_at(m, lat, lon) * prop_daylight_factor(lat, lon, t);
}

void prop_muf_grid(const PropModel &m, time_t t, uint8_t *out) {
  SunPos s = sun_position(t - IONO_LAG_S);
  for (int gy = 0; gy < PROP_GH; gy++) {
    double lat = 89.0 - gy * 2.0;
    for (int gx = 0; gx < PROP_GW; gx++) {
      double v = m.valid ? m.ratio[gy][gx] * daylight_factor(s, lat, -179.0 + gx * 2.0) * 4.0 : 0;
      out[gy * PROP_GW + gx] = (uint8_t)(v > 255 ? 255 : (v < 0 ? 0 : v + 0.5));
    }
  }
}

// ---------- Pfad ----------
// Punkt auf dem Grosskreis im Abstand d (km) vom Start
static void gc_point(double la1, double lo1, double la2, double lo2, double d_km, double &la, double &lo) {
  double p1 = la1 * DEG, l1 = lo1 * DEG, p2 = la2 * DEG, l2 = lo2 * DEG;
  double D = gc_km(la1, lo1, la2, lo2) / RE;
  if (D < 1e-6) { la = la1; lo = lo1; return; }
  double f = d_km / RE / D;
  double A = sin((1 - f) * D) / sin(D), B = sin(f * D) / sin(D);
  double x = A * cos(p1) * cos(l1) + B * cos(p2) * cos(l2);
  double y = A * cos(p1) * sin(l1) + B * cos(p2) * sin(l2);
  double z = A * sin(p1) + B * sin(p2);
  la = atan2(z, sqrt(x * x + y * y)) / DEG;
  lo = atan2(y, x) / DEG;
}

static float drap_at(const uint8_t *g, double lat, double lon) {
  int r = (int)lround((89.0 - lat) / 2.0), c = (int)lround((lon + 178.0) / 4.0);
  if (r < 0) r = 0;
  if (r > 89) r = 89;
  c = ((c % 90) + 90) % 90;
  return g[r * 90 + c] / 5.0f;
}

void prop_path(const PropModel &m, const uint8_t *drap, float sfi, double lat1, double lon1,
               double lat2, double lon2, time_t t, PathForecast &o) {
  o.dist_km = (float)gc_km(lat1, lon1, lat2, lon2);
  o.bearing = (float)gc_bearing_deg(lat1, lon1, lat2, lon2);
  o.hops = (int)ceil(o.dist_km / 3500.0);
  if (o.hops < 1) o.hops = 1;
  double hop = o.dist_km / o.hops;

  // Kontrollpunkte: Mitte (kurze Pfade) bzw. je 2000 km von beiden Enden (lange Pfade)
  double cp[3][2];
  int ncp = 0;
  if (o.dist_km <= 4000) {
    gc_point(lat1, lon1, lat2, lon2, o.dist_km / 2, cp[0][0], cp[0][1]);
    ncp = 1;
  } else {
    gc_point(lat1, lon1, lat2, lon2, 2000, cp[0][0], cp[0][1]);
    gc_point(lat1, lon1, lat2, lon2, o.dist_km - 2000, cp[1][0], cp[1][1]);
    gc_point(lat1, lon1, lat2, lon2, o.dist_km / 2, cp[2][0], cp[2][1]);
    ncp = 3;
  }

  // MUF: schwaechster Kontrollpunkt; kuerzere Spruenge haben eine niedrigere MUF als 3000 km
  double muf = 1e9;
  for (int i = 0; i < ncp; i++) muf = fmin(muf, prop_muf(m, cp[i][0], cp[i][1], t));
  double k = hop >= 3000 ? 1.0 : sqrt(fmax(hop, 300.0) / 3000.0);
  o.muf = (float)(muf * k);

  // LUF: Tages-Absorption (D-Schicht) an den Kontrollpunkten, staerker mit SFI und Sprungzahl
  double luf = 0;
  for (int i = 0; i < ncp; i++) {
    double day = (prop_daylight_factor(cp[i][0], cp[i][1], t) - NIGHT_FLOOR) / (1.0 - NIGHT_FLOOR);   // 0 Nacht .. 1 Mittag
    double l = 1.5 + 4.5 * day * (1.0 + 0.005 * (sfi - 70.0)) * pow(o.hops, 0.3);
    if (drap) l = fmax(l, drap_at(drap, cp[i][0], cp[i][1]));
    luf = fmax(luf, l);
  }
  o.luf = (float)luf;

  for (int b = 0; b < PROP_BANDS; b++) {
    double f = PROP_BAND_MHZ[b];
    PropStatus s;
    if (f > o.muf || f < o.luf) s = PROP_CLOSED;
    else if (f > 0.85 * o.muf || f < 1.25 * o.luf) s = PROP_MARGINAL;
    else s = PROP_OPEN;
    o.band[b] = s;
  }
}
