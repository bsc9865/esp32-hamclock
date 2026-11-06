// Reines Karten-Rendering ohne LVGL/Arduino (auch auf dem PC testbar)
#include "map_render.h"
#include <math.h>
#include <stdlib.h>
#include "astro.h"

static constexpr double DEG = M_PI / 180.0;
static uint16_t *fb;
static double qlat, qlon;

void worldmap_to_xy(double lat, double lon, int &x, int &y) {
  x = (int)floor((lon + 180.0) / 360.0 * MAP_W);
  y = (int)floor((90.0 - lat) / 180.0 * MAP_H);
  x = ((x % MAP_W) + MAP_W) % MAP_W;
  if (y < 0) y = 0;
  if (y >= MAP_H) y = MAP_H - 1;
}

void worldmap_to_latlon(int x, int y, double &lat, double &lon) {
  lon = -180.0 + (x + 0.5) * 360.0 / MAP_W;
  lat = 90.0 - (y + 0.5) * 180.0 / MAP_H;
}

// ---------- Pixel-Helfer ----------
// Mischt zwei RGB565-Farben, w = 0..32 (Anteil von a)
static inline uint16_t blend565(uint16_t a, uint16_t b, uint32_t w) {
  uint32_t A = (a | ((uint32_t)a << 16)) & 0x07E0F81F;
  uint32_t B = (b | ((uint32_t)b << 16)) & 0x07E0F81F;
  uint32_t r = ((A * w + B * (32 - w)) >> 5) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline void px(int x, int y, uint16_t c, uint32_t w = 32) {
  if (y < 0 || y >= MAP_H) return;
  x = ((x % MAP_W) + MAP_W) % MAP_W;   // horizontal umlaufend
  uint16_t &d = fb[y * MAP_W + x];
  d = w >= 32 ? c : blend565(c, d, w);
}

static void disc(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r) px(cx + dx, cy + dy, c);
}

static void ring(int cx, int cy, int r, int th, uint16_t c) {
  int ro = r * r, ri = (r - th) * (r - th);
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++) {
      int d = dx * dx + dy * dy;
      if (d <= ro && d > ri) px(cx + dx, cy + dy, c);
    }
}

// ---------- Rendering ----------
static void render_daynight(const SunPos &s) {
  // cos(Zenitwinkel) = sin(phi)sin(dec) + cos(phi)cos(dec)cos(lon - lon_s)
  static float coslon[MAP_W];
  for (int x = 0; x < MAP_W; x++) {
    double lon = -180.0 + (x + 0.5) * 360.0 / MAP_W;
    coslon[x] = cos((lon - s.subsolar_lon) * DEG);
  }
  const float sd = sin(s.decl * DEG), cd = cos(s.decl * DEG);
  // Uebergang von -12 Grad (nautische Daemmerung, volle Nacht) bis +3 Grad (voller Tag)
  const float lo = sinf(-12.0f * DEG), hi = sinf(3.0f * DEG);
  const float scale = 32.0f / (hi - lo);

  for (int y = 0; y < MAP_H; y++) {
    double lat = 90.0 - (y + 0.5) * 180.0 / MAP_H;
    float a = sin(lat * DEG) * sd, b = cos(lat * DEG) * cd;
    const uint16_t *dr = &map_day[y * MAP_W];
    const uint16_t *nr = &map_night[y * MAP_W];
    uint16_t *o = &fb[y * MAP_W];
    for (int x = 0; x < MAP_W; x++) {
      float cz = a + b * coslon[x];
      int w = (int)((cz - lo) * scale);
      if (w <= 0) o[x] = nr[x];
      else if (w >= 32) o[x] = dr[x];
      else o[x] = blend565(dr[x], nr[x], w);
    }
  }
}

static void render_grid() {
  const uint16_t c = rgb(200, 220, 255);
  // Maidenhead-Felder: 20 Grad Laenge x 10 Grad Breite
  for (int k = 0; k < 18; k++) {
    int x = k * MAP_W / 18;
    for (int y = 0; y < MAP_H; y++) px(x, y, c, (k == 9) ? 10 : 6);
  }
  for (int k = 1; k < 18; k++) {
    int y = k * MAP_H / 18;
    for (int x = 0; x < MAP_W; x++) px(x, y, c, (k == 9) ? 10 : 6);
  }
}

// Linie mit Alpha (w = 0..32), Bresenham; Segmente ueber die Datumsgrenze werden ausgelassen
static void line(int x0, int y0, int x1, int y1, uint16_t c, uint32_t w) {
  if (abs(x1 - x0) > MAP_W / 2) return;
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    px(x0, y0, c, w);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static inline uint16_t hex565(uint32_t c) { return rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

// Grosskreis vom QTH zum Spot (Interpolation auf der Kugel)
static void great_circle(double lat1, double lon1, double lat2, double lon2, uint16_t c) {
  double p1 = lat1 * DEG, l1 = lon1 * DEG, p2 = lat2 * DEG, l2 = lon2 * DEG;
  double x1 = cos(p1) * cos(l1), y1 = cos(p1) * sin(l1), z1 = sin(p1);
  double x2 = cos(p2) * cos(l2), y2 = cos(p2) * sin(l2), z2 = sin(p2);
  double d = acos(fmin(1.0, fmax(-1.0, x1 * x2 + y1 * y2 + z1 * z2)));
  if (d < 1e-3) return;
  int steps = 8 + (int)(d / M_PI * 120);
  int px0 = -1, py0 = -1;
  for (int i = 0; i <= steps; i++) {
    double f = (double)i / steps;
    double A = sin((1 - f) * d) / sin(d), B = sin(f * d) / sin(d);
    double x = A * x1 + B * x2, y = A * y1 + B * y2, z = A * z1 + B * z2;
    int sx, sy;
    worldmap_to_xy(atan2(z, sqrt(x * x + y * y)) / DEG, atan2(y, x) / DEG, sx, sy);
    if (px0 >= 0) line(px0, py0, sx, sy, c, 20);
    px0 = sx; py0 = sy;
  }
}

static void render_spots(const MapSpot *sp, int n) {
  for (int i = n - 1; i >= 0; i--)   // aelteste zuerst, neueste oben
    great_circle(qlat, qlon, sp[i].lat, sp[i].lon, hex565(sp[i].color));
  for (int i = n - 1; i >= 0; i--) {
    int x, y;
    worldmap_to_xy(sp[i].lat, sp[i].lon, x, y);
    disc(x, y, 4, rgb(0, 0, 0));
    disc(x, y, 3, hex565(sp[i].color));
  }
}

static void render_markers(time_t t, const SunPos &s) {
  int x, y;
  // Mond
  MoonPos m = moon_position(t);
  worldmap_to_xy(m.decl, m.sublunar_lon, x, y);
  disc(x, y, 6, rgb(30, 30, 30));
  disc(x, y, 5, rgb(210, 210, 220));
  // Sonne
  worldmap_to_xy(s.decl, s.subsolar_lon, x, y);
  disc(x, y, 8, rgb(60, 30, 0));
  disc(x, y, 7, rgb(255, 200, 40));
  disc(x, y, 4, rgb(255, 240, 150));
  // QTH
  worldmap_to_xy(qlat, qlon, x, y);
  ring(x, y, 7, 3, rgb(0, 0, 0));
  ring(x, y, 6, 2, rgb(255, 60, 60));
  disc(x, y, 1, rgb(255, 255, 255));
}

void map_render(uint16_t *buf, time_t t, double qth_lat, double qth_lon,
                const MapSpot *spots, int n_spots) {
  fb = buf;
  qlat = qth_lat;
  qlon = qth_lon;
  SunPos s = sun_position(t);
  render_daynight(s);
  render_grid();
  if (spots && n_spots > 0) render_spots(spots, n_spots);
  render_markers(t, s);
}
