#include "astro.h"
#include <math.h>
#include <ctype.h>
#include <string.h>

static constexpr double DEG = M_PI / 180.0;

static double norm360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }
static double norm180(double a) { a = norm360(a); return a > 180.0 ? a - 360.0 : a; }

bool maidenhead_to_latlon(const char *loc, LatLon &out) {
  size_t n = strlen(loc);
  if (n < 4) return false;
  int A = toupper(loc[0]) - 'A', B = toupper(loc[1]) - 'A';
  int c = loc[2] - '0', d = loc[3] - '0';
  if (A < 0 || A > 17 || B < 0 || B > 17 || c < 0 || c > 9 || d < 0 || d > 9) return false;
  double lon = A * 20.0 - 180.0 + c * 2.0;
  double lat = B * 10.0 - 90.0 + d * 1.0;
  if (n >= 6) {
    int e = toupper(loc[4]) - 'A', f = toupper(loc[5]) - 'A';
    if (e < 0 || e > 23 || f < 0 || f > 23) return false;
    lon += e * (5.0 / 60.0) + 2.5 / 60.0;
    lat += f * (2.5 / 60.0) + 1.25 / 60.0;
  } else {
    lon += 1.0;
    lat += 0.5;
  }
  out = {lat, lon};
  return true;
}

// Vereinfachter Sonnenstand (Astronomical Almanac, Genauigkeit ~0,01 Grad)
SunPos sun_position(time_t t) {
  double n = t / 86400.0 + 2440587.5 - 2451545.0;   // Tage seit J2000
  double L = norm360(280.460 + 0.9856474 * n);
  double g = norm360(357.528 + 0.9856003 * n) * DEG;
  double lambda = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * DEG;
  double eps = (23.439 - 0.0000004 * n) * DEG;

  SunPos s;
  s.decl = asin(sin(eps) * sin(lambda)) / DEG;
  s.ra = norm360(atan2(cos(eps) * sin(lambda), cos(lambda)) / DEG);
  s.gmst = norm360(280.46061837 + 360.98564736629 * n);
  s.subsolar_lon = norm180(s.ra - s.gmst);
  s.eot_min = 4.0 * norm180(L - s.ra);
  return s;
}

void sun_alt_az(time_t t, double lat, double lon, double &alt, double &az) {
  SunPos s = sun_position(t);
  double H = norm180(s.gmst + lon - s.ra) * DEG;
  double phi = lat * DEG, dec = s.decl * DEG;
  alt = asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H)) / DEG;
  az = norm360(atan2(-sin(H), tan(dec) * cos(phi) - sin(phi) * cos(H)) / DEG);
}

// Stundenwinkel bei Hoehe h0 (-0,833 Grad: Refraktion + Sonnenradius)
static bool hour_angle(double lat, double decl, double &H) {
  double h0 = -0.833 * DEG;
  double c = (sin(h0) - sin(lat * DEG) * sin(decl * DEG)) / (cos(lat * DEG) * cos(decl * DEG));
  if (c < -1 || c > 1) return false;
  H = acos(c) / DEG;
  return true;
}

bool sun_rise_set(time_t t, double lat, double lon, time_t &rise, time_t &set) {
  // Lokaler Mittag des Kalendertages -> UTC-Tagesbeginn
  struct tm lt;
  localtime_r(&t, &lt);
  lt.tm_hour = 12; lt.tm_min = 0; lt.tm_sec = 0;
  time_t noon_local = mktime(&lt);
  time_t day0 = noon_local - (noon_local % 86400);

  SunPos s = sun_position(noon_local);
  double noon_h = 12.0 - lon / 15.0 - s.eot_min / 60.0;  // wahrer Mittag in UTC-Stunden
  double H;
  if (!hour_angle(lat, s.decl, H)) return false;

  // Eine Iteration mit der Deklination zum jeweiligen Zeitpunkt
  time_t r0 = day0 + (time_t)((noon_h - H / 15.0) * 3600);
  time_t s0 = day0 + (time_t)((noon_h + H / 15.0) * 3600);
  double Hr, Hs;
  SunPos sr = sun_position(r0), ss = sun_position(s0);
  if (!hour_angle(lat, sr.decl, Hr) || !hour_angle(lat, ss.decl, Hs)) return false;
  rise = day0 + (time_t)((12.0 - lon / 15.0 - sr.eot_min / 60.0 - Hr / 15.0) * 3600);
  set  = day0 + (time_t)((12.0 - lon / 15.0 - ss.eot_min / 60.0 + Hs / 15.0) * 3600);
  return true;
}

void moon_phase(time_t t, double &age_days, double &illum) {
  const double synodic = 29.530588853;
  double jd = t / 86400.0 + 2440587.5;
  double phase = fmod((jd - 2451550.1) / synodic, 1.0);  // Referenz-Neumond 6.1.2000
  if (phase < 0) phase += 1.0;
  age_days = phase * synodic;
  illum = (1.0 - cos(2 * M_PI * phase)) / 2.0;
}

const char *moon_phase_name(double age) {
  static const char *names[] = {
    "Neumond", "zun. Sichel", "Erstes Viertel", "zun. Mond",
    "Vollmond", "abn. Mond", "Letztes Viertel", "abn. Sichel"};
  int i = (int)floor(age / 29.530588853 * 8.0 + 0.5) % 8;
  return names[i];
}

MoonPos moon_position(time_t t) {
  double n = t / 86400.0 + 2440587.5 - 2451545.0;
  double L = norm360(218.316 + 13.176396 * n);
  double M = norm360(134.963 + 13.064993 * n) * DEG;
  double F = norm360(93.272 + 13.229350 * n) * DEG;
  double lam = (L + 6.289 * sin(M)) * DEG;   // ekliptikale Laenge
  double bet = 5.128 * sin(F) * DEG;          // ekliptikale Breite
  double eps = (23.439 - 0.0000004 * n) * DEG;
  double ra = atan2(sin(lam) * cos(eps) - tan(bet) * sin(eps), cos(lam));
  double dec = asin(sin(bet) * cos(eps) + cos(bet) * sin(eps) * sin(lam));
  double gmst = norm360(280.46061837 + 360.98564736629 * n);
  return {dec / DEG, norm180(ra / DEG - gmst)};
}

void latlon_to_maidenhead(double lat, double lon, char out[7]) {
  double x = norm360(lon + 180.0), y = lat + 90.0;
  if (y < 0) y = 0;
  if (y >= 180) y = 179.9999;
  out[0] = 'A' + (int)(x / 20);
  out[1] = 'A' + (int)(y / 10);
  out[2] = '0' + (int)(fmod(x, 20) / 2);
  out[3] = '0' + (int)fmod(y, 10);
  out[4] = 'a' + (int)(fmod(x, 2) * 12);
  out[5] = 'a' + (int)(fmod(y, 1) * 24);
  out[6] = 0;
}

double gc_distance_km(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * DEG, p2 = lat2 * DEG, dl = (lon2 - lon1) * DEG;
  double c = sin(p1) * sin(p2) + cos(p1) * cos(p2) * cos(dl);
  if (c > 1) c = 1;
  if (c < -1) c = -1;
  return acos(c) * 6371.0;
}

double gc_bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * DEG, p2 = lat2 * DEG, dl = (lon2 - lon1) * DEG;
  return norm360(atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) / DEG);
}
