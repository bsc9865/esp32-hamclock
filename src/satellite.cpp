#include "satellite.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sgp4io.h"

static constexpr double DEG = M_PI / 180.0;
static constexpr double WGS_A = 6378.137;                 // km
static constexpr double WGS_F = 1.0 / 298.257223563;
static constexpr double WGS_E2 = WGS_F * (2.0 - WGS_F);

static inline double jd_of(time_t t) { return t / 86400.0 + 2440587.5; }

void Satellite::short_name(const char *full, char *out, size_t n) {
  // "OSCAR 7 (AO-7)" -> "AO-7"; "ISS (ZARYA)" -> "ISS"; "RS-44 & BREEZE-KM R/B" -> "RS-44"
  if (strncmp(full, "ES'HAIL 2", 9) == 0) { snprintf(out, n, "QO-100"); return; }
  if (strncmp(full, "ISS", 3) == 0) { snprintf(out, n, "ISS"); return; }
  const char *o = strchr(full, '('), *c = o ? strchr(o, ')') : nullptr;
  if (o && c && c - o - 1 > 0 && c - o - 1 < (int)n) {
    // Klammerinhalt verwenden, wenn er wie eine OSCAR-Bezeichnung aussieht (Buchstaben-Ziffern)
    const char *p = o + 1;
    bool letters = isalpha((unsigned char)*p), dash = false;
    for (const char *q = p; q < c; q++) if (*q == '-') dash = true;
    if (letters && dash) {
      snprintf(out, n, "%.*s", (int)(c - p), p);
      return;
    }
  }
  // sonst bis zum ersten " &" bzw. " (" , Leerzeichen am Ende entfernen
  size_t len = strcspn(full, "&(");
  while (len > 0 && full[len - 1] == ' ') len--;
  if (len >= n) len = n - 1;
  memcpy(out, full, len);
  out[len] = 0;
}

bool Satellite::init(const char *name, const char *line1, const char *line2) {
  char l1[130], l2[130];
  strncpy(l1, line1, sizeof l1 - 1); l1[sizeof l1 - 1] = 0;
  strncpy(l2, line2, sizeof l2 - 1); l2[sizeof l2 - 1] = 0;
  memset(&rec_, 0, sizeof rec_);
  twoline2rv(l1, l2, 'i', wgs72, rec_);
  // Namen ohne Leerzeichen am Ende
  size_t len = strlen(name);
  while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\r' || name[len - 1] == '\n')) len--;
  if (len >= sizeof full_) len = sizeof full_ - 1;
  memcpy(full_, name, len);
  full_[len] = 0;
  short_name(full_, name_, sizeof name_);
  return rec_.error == 0 && rec_.satnum > 0;
}

double Satellite::period_min() const { return rec_.no > 0 ? 2.0 * M_PI / rec_.no : 0; }

bool Satellite::eci(time_t t, double r[3]) {
  double v[3];
  double tsince = (jd_of(t) - rec_.jdsatepoch) * 1440.0;
  return sgp4(wgs72, rec_, tsince, r, v) && rec_.error == 0;
}

// TEME -> erdfest (ohne Polbewegung)
static void teme_to_ecef(const double r[3], double jd, double e[3]) {
  double g = gstime(jd);
  e[0] = r[0] * cos(g) + r[1] * sin(g);
  e[1] = -r[0] * sin(g) + r[1] * cos(g);
  e[2] = r[2];
}

static void ecef_to_geo(const double e[3], double &lat, double &lon, double &alt) {
  lon = atan2(e[1], e[0]);
  double p = sqrt(e[0] * e[0] + e[1] * e[1]);
  double la = atan2(e[2], p * (1 - WGS_E2)), h = 0;
  for (int i = 0; i < 6; i++) {
    double N = WGS_A / sqrt(1 - WGS_E2 * sin(la) * sin(la));
    h = p / cos(la) - N;
    la = atan2(e[2], p * (1 - WGS_E2 * N / (N + h)));
  }
  lat = la / DEG;
  lon = lon / DEG;
  alt = h;
}

static void obs_ecef(const Observer &o, double e[3]) {
  double la = o.lat * DEG, lo = o.lon * DEG;
  double N = WGS_A / sqrt(1 - WGS_E2 * sin(la) * sin(la));
  e[0] = (N + o.alt_km) * cos(la) * cos(lo);
  e[1] = (N + o.alt_km) * cos(la) * sin(lo);
  e[2] = (N * (1 - WGS_E2) + o.alt_km) * sin(la);
}

static void look_angles(const double sat[3], const Observer &o, double &az, double &el, double &range) {
  double ob[3];
  obs_ecef(o, ob);
  double dx = sat[0] - ob[0], dy = sat[1] - ob[1], dz = sat[2] - ob[2];
  double la = o.lat * DEG, lo = o.lon * DEG;
  double south = sin(la) * cos(lo) * dx + sin(la) * sin(lo) * dy - cos(la) * dz;
  double east = -sin(lo) * dx + cos(lo) * dy;
  double zen = cos(la) * cos(lo) * dx + cos(la) * sin(lo) * dy + sin(la) * dz;
  range = sqrt(dx * dx + dy * dy + dz * dz);
  el = asin(zen / range) / DEG;
  az = atan2(east, -south) / DEG;
  if (az < 0) az += 360.0;
}

bool Satellite::state(time_t t, const Observer &o, SatState &s) {
  double r[3], e[3];
  if (!eci(t, r)) return false;
  teme_to_ecef(r, jd_of(t), e);
  ecef_to_geo(e, s.lat, s.lon, s.alt_km);
  look_angles(e, o, s.az, s.el, s.range_km);
  return true;
}

double Satellite::elevation(time_t t, const Observer &o, double *az) {
  SatState s;
  if (!state(t, o, s)) return -90;
  if (az) *az = s.az;
  return s.el;
}

int Satellite::passes(const Observer &o, time_t from, time_t to, float min_el, SatPass *out, int max) {
  if (max <= 0 || is_geo() || period_min() > 600) return 0;   // geostationaer / sehr hoch: keine Ueberfluege
  const int step = 60;
  int n = 0;
  auto bisect = [&](time_t a, time_t b, bool rising) {
    // Nullstelle der Elevation zwischen a und b auf 1 s genau
    while (b - a > 1) {
      time_t m = a + (b - a) / 2;
      bool up = elevation(m, o) > 0;
      if (up == rising) b = m; else a = m;
    }
    return b;
  };

  time_t t = from;
  double e_prev = elevation(t, o);
  bool in_pass = false;
  SatPass cur{};
  if (e_prev > 0) {
    // laeuft schon: Aufgang rueckwaerts suchen (hoechstens 30 min)
    time_t a = from;
    while (a > from - 1800 && elevation(a - step, o) > 0) a -= step;
    cur.aos = bisect(a - step, a, true);
    cur.ongoing = true;
    cur.max_el = -90;
    in_pass = true;
  }
  for (t = from + step; t <= to && n < max; t += step) {
    double az;
    double e = elevation(t, o, &az);
    if (!in_pass && e_prev <= 0 && e > 0) {
      cur = SatPass{};
      cur.aos = bisect(t - step, t, true);
      cur.max_el = -90;
      in_pass = true;
    }
    if (in_pass && e > cur.max_el) { cur.max_el = (float)e; cur.tmax = t; }
    if (in_pass && e_prev > 0 && e <= 0) {
      cur.los = bisect(t - step, t, false);
      // Maximum genauer bestimmen (Goldener Schnitt um den groben Hoechstpunkt)
      double a = (double)cur.tmax - step, b = (double)cur.tmax + step;
      const double g = 0.6180339887;
      for (int i = 0; i < 14; i++) {
        double c = b - g * (b - a), d = a + g * (b - a);
        if (elevation((time_t)c, o) > elevation((time_t)d, o)) b = d; else a = c;
      }
      cur.tmax = (time_t)((a + b) / 2);
      double azv;
      cur.max_el = (float)elevation(cur.tmax, o, &azv);
      cur.az_max = (float)azv;
      elevation(cur.aos, o, &azv); cur.az_aos = (float)azv;
      elevation(cur.los, o, &azv); cur.az_los = (float)azv;
      if (cur.max_el >= min_el) out[n++] = cur;
      in_pass = false;
    }
    e_prev = e;
  }
  return n;
}

int Satellite::ground_track(time_t from, time_t to, int step_s, float *lat, float *lon, int max) {
  int n = 0;
  for (time_t t = from; t <= to && n < max; t += step_s) {
    double r[3], e[3], la, lo, h;
    if (!eci(t, r)) break;
    teme_to_ecef(r, jd_of(t), e);
    ecef_to_geo(e, la, lo, h);
    lat[n] = (float)la;
    lon[n] = (float)lo;
    n++;
  }
  return n;
}

void Satellite::footprint(double lat, double lon, double alt_km, int n, float *plat, float *plon) {
  const double Re = 6371.0;
  double d = acos(Re / (Re + alt_km));   // Winkelradius bis zum Horizont
  double p1 = lat * DEG, l1 = lon * DEG;
  for (int i = 0; i < n; i++) {
    double th = 2 * M_PI * i / n;
    double p2 = asin(sin(p1) * cos(d) + cos(p1) * sin(d) * cos(th));
    double l2 = l1 + atan2(sin(th) * sin(d) * cos(p1), cos(d) - sin(p1) * sin(p2));
    plat[i] = (float)(p2 / DEG);
    double lo = fmod(l2 / DEG + 540.0, 360.0) - 180.0;
    plon[i] = (float)lo;
  }
}
