// Reines Karten-Rendering ohne LVGL/Arduino (auch auf dem PC testbar)
#include "map_render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "astro.h"

static constexpr double DEG = M_PI / 180.0;
static constexpr double SRC_DPP = 360.0 / MAP_SRC_W;   // Grad pro Quellpixel

static double norm180(double a) {
  a = fmod(a + 180.0, 360.0);
  if (a < 0) a += 360.0;
  return a - 180.0;
}

// ---------- Ausschnitt ----------
MapView map_view_world(int w, int h) { return {w, h, 0.0, 0.0, 360.0 / w}; }

MapView map_view_zoom(int w, int h, double lat_c, double lon_c, double zoom) {
  MapView v{w, h, lon_c, lat_c, 360.0 / w / zoom};
  map_view_clamp(v);
  return v;
}

void map_view_clamp(MapView &v) {
  v.lon_c = norm180(v.lon_c);
  double half = v.h * v.dpp / 2;
  if (half >= 90) v.lat_c = 0;
  else if (v.lat_c > 90 - half) v.lat_c = 90 - half;
  else if (v.lat_c < -90 + half) v.lat_c = -90 + half;
}

void map_view_to_xy(const MapView &v, double lat, double lon, double &x, double &y) {
  x = norm180(lon - v.lon_c) / v.dpp + v.w / 2.0;
  y = (v.lat_c - lat) / v.dpp + v.h / 2.0;
}

void map_view_to_latlon(const MapView &v, double x, double y, double &lat, double &lon) {
  lon = norm180(v.lon_c + (x - v.w / 2.0) * v.dpp);
  lat = v.lat_c - (y - v.h / 2.0) * v.dpp;
}

// ---------- Pixel-Helfer ----------
static uint16_t *fb;
static int W, H;
static double qlat, qlon;
static MapView V;

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

static inline uint16_t hex565(uint32_t c) { return rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

static inline void px(int x, int y, uint16_t c, uint32_t w = 32) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  uint16_t &d = fb[y * W + x];
  d = w >= 32 ? c : blend565(c, d, w);
}

static void disc(int cx, int cy, int r, uint16_t c) {
  if (cx < -r || cy < -r || cx >= W + r || cy >= H + r) return;
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

// Linie mit Alpha (w = 0..32), Bresenham, mit einfachem Clipping
static void line(int x0, int y0, int x1, int y1, uint16_t c, uint32_t w) {
  if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) || (x0 >= W && x1 >= W) || (y0 >= H && y1 >= H)) return;
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

static void to_px(double lat, double lon, int &x, int &y) {
  double fx, fy;
  map_view_to_xy(V, lat, lon, fx, fy);
  x = (int)floor(fx);
  y = (int)floor(fy);
}

// ---------- Tag/Nacht ----------
static void render_daynight(const SunPos &s) {
  const MapImage day = maps_day(), night = maps_night();
  const bool bilinear = V.dpp < SRC_DPP * 0.9;   // Vergroesserung -> glaetten

  // Spalten-Tabellen
  static int16_t *sx = nullptr;
  static uint8_t *fx = nullptr;
  static float *coslon = nullptr;
  static int cap = 0;
  if (cap < W) {
    free(sx); free(fx); free(coslon);
    sx = (int16_t *)malloc(W * sizeof(int16_t));
    fx = (uint8_t *)malloc(W);
    coslon = (float *)malloc(W * sizeof(float));
    cap = W;
  }
  for (int x = 0; x < W; x++) {
    double lon = norm180(V.lon_c + (x + 0.5 - W / 2.0) * V.dpp);
    double u = (lon + 180.0) / SRC_DPP - (bilinear ? 0.5 : 0.0);
    if (u < 0) u += MAP_SRC_W;
    int iu = (int)u;
    sx[x] = (int16_t)(iu % MAP_SRC_W);
    fx[x] = (uint8_t)((u - iu) * 32);
    coslon[x] = cos((lon - s.subsolar_lon) * DEG);
  }

  const float sd = sin(s.decl * DEG), cd = cos(s.decl * DEG);
  // Uebergang von -12 Grad (nautische Daemmerung, volle Nacht) bis +3 Grad (voller Tag)
  const float lo = sinf(-12.0f * DEG), hi = sinf(3.0f * DEG);
  const float scale = 32.0f / (hi - lo);
  const uint16_t space = rgb(4, 6, 12);

  for (int y = 0; y < H; y++) {
    uint16_t *o = &fb[y * W];
    double lat = V.lat_c - (y + 0.5 - H / 2.0) * V.dpp;
    if (lat > 90 || lat < -90) {
      for (int x = 0; x < W; x++) o[x] = space;
      continue;
    }
    double v = (90.0 - lat) / SRC_DPP - (bilinear ? 0.5 : 0.0);
    if (v < 0) v = 0;
    int iv = (int)v;
    if (iv >= MAP_SRC_H) iv = MAP_SRC_H - 1;
    int iv2 = iv + 1 < MAP_SRC_H ? iv + 1 : iv;
    uint32_t fy = (uint32_t)((v - iv) * 32);
    const uint8_t *d0 = day.px + iv * MAP_SRC_W, *d1 = day.px + iv2 * MAP_SRC_W;
    const uint8_t *n0 = night.px + iv * MAP_SRC_W, *n1 = night.px + iv2 * MAP_SRC_W;
    float a = sin(lat * DEG) * sd, b = cos(lat * DEG) * cd;

    for (int x = 0; x < W; x++) {
      int u0 = sx[x];
      uint16_t dc, nc;
      if (bilinear) {
        int u1 = u0 + 1 < MAP_SRC_W ? u0 + 1 : 0;
        uint32_t f = fx[x];
        dc = blend565(blend565(day.pal[d1[u1]], day.pal[d1[u0]], f),
                      blend565(day.pal[d0[u1]], day.pal[d0[u0]], f), fy);
        nc = blend565(blend565(night.pal[n1[u1]], night.pal[n1[u0]], f),
                      blend565(night.pal[n0[u1]], night.pal[n0[u0]], f), fy);
      } else {
        dc = day.pal[d0[u0]];
        nc = night.pal[n0[u0]];
      }
      float cz = a + b * coslon[x];
      int w = (int)((cz - lo) * scale);
      o[x] = w <= 0 ? nc : (w >= 32 ? dc : blend565(dc, nc, w));
    }
  }
}

// ---------- Ebenen: Aurora und D-RAP ----------
// Raster mit Zellmitten auf ganzen Graden; Zuordnung pro Zeile/Spalte vorberechnet
static void render_grid_overlay(const uint8_t *grid, int gw, int gh, double lat0, double dlat,
                                double lon0, double dlon, uint16_t color, int vmin, int vfull, int pct = 100) {
  // Alpha-Tabelle je Rasterwert (0..32), mit Deckkraft skaliert
  uint8_t alpha[256];
  for (int v = 0; v < 256; v++) {
    int a = v < vmin ? 0 : (v >= vfull ? 22 : 4 + (v - vmin) * 18 / (vfull - vmin));
    a = a * pct / 100;
    alpha[v] = (uint8_t)(a > 32 ? 32 : a);
  }
  static int16_t *col = nullptr;
  static int cap = 0;
  if (cap < W) { free(col); col = (int16_t *)malloc(W * sizeof(int16_t)); cap = W; }
  for (int x = 0; x < W; x++) {
    double lon = norm180(V.lon_c + (x + 0.5 - W / 2.0) * V.dpp);
    int c = (int)lround((lon - lon0) / dlon);
    col[x] = (int16_t)(((c % gw) + gw) % gw);
  }
  for (int y = 0; y < H; y++) {
    double lat = V.lat_c - (y + 0.5 - H / 2.0) * V.dpp;
    if (lat > 90 || lat < -90) continue;
    int r = (int)lround((lat - lat0) / dlat);
    if (r < 0 || r >= gh) continue;
    const uint8_t *row = grid + r * gw;
    uint16_t *o = &fb[y * W];
    for (int x = 0; x < W; x++) {
      uint8_t a = alpha[row[col[x]]];
      if (a) o[x] = blend565(color, o[x], a);
    }
  }
}

// ---------- Gitter ----------
static void render_grid() {
  const uint16_t c = rgb(200, 220, 255);
  // Maidenhead-Felder (20 x 10 Grad); ab starkem Zoom zusaetzlich Quadrate (2 x 1 Grad)
  bool squares = V.dpp < 0.06;
  double step_lon = squares ? 2.0 : 20.0, step_lat = squares ? 1.0 : 10.0;
  double lon_min, lat_max, lon_max, lat_min;
  map_view_to_latlon(V, 0, 0, lat_max, lon_min);
  map_view_to_latlon(V, W, H, lat_min, lon_max);
  double span = W * V.dpp;
  // senkrechte Linien nur zwischen den Polen
  double fx, fy_top, fy_bot;
  map_view_to_xy(V, 90, V.lon_c, fx, fy_top);
  map_view_to_xy(V, -90, V.lon_c, fx, fy_bot);
  int y_top = fy_top < 0 ? 0 : (int)fy_top, y_bot = fy_bot > H ? H : (int)fy_bot;
  double start = floor((V.lon_c - span / 2) / step_lon) * step_lon;
  for (double lon = start; lon <= V.lon_c + span / 2 + 1e-9; lon += step_lon) {
    int x, y;
    to_px(0, lon, x, y);
    bool field = fmod(fabs(lon + 180.0), 20.0) < 1e-6;
    bool prime = fabs(norm180(lon)) < 1e-6;
    uint32_t w = prime ? 10 : (field ? 7 : 4);
    for (int yy = y_top; yy < y_bot; yy++) px(x, yy, c, w);
  }
  for (double lat = -90 + step_lat; lat < 90; lat += step_lat) {
    if (lat < lat_min - step_lat || lat > lat_max + step_lat) continue;
    int x, y;
    to_px(lat, V.lon_c, x, y);
    bool field = fmod(fabs(lat + 90.0), 10.0) < 1e-6;
    uint32_t w = fabs(lat) < 1e-6 ? 10 : (field ? 7 : 4);
    for (int xx = 0; xx < W; xx++) px(xx, y, c, w);
  }
}

// ---------- Spots ----------
// Grosskreis vom QTH zum Spot (Interpolation auf der Kugel)
static void great_circle(double lat1, double lon1, double lat2, double lon2, uint16_t c) {
  double p1 = lat1 * DEG, l1 = lon1 * DEG, p2 = lat2 * DEG, l2 = lon2 * DEG;
  double x1 = cos(p1) * cos(l1), y1 = cos(p1) * sin(l1), z1 = sin(p1);
  double x2 = cos(p2) * cos(l2), y2 = cos(p2) * sin(l2), z2 = sin(p2);
  double d = acos(fmin(1.0, fmax(-1.0, x1 * x2 + y1 * y2 + z1 * z2)));
  if (d < 1e-3) return;
  int steps = 8 + (int)(d / M_PI * 120);
  int px0 = 0, py0 = 0;
  for (int i = 0; i <= steps; i++) {
    double f = (double)i / steps;
    double A = sin((1 - f) * d) / sin(d), B = sin(f * d) / sin(d);
    double x = A * x1 + B * x2, y = A * y1 + B * y2, z = A * z1 + B * z2;
    int sx, sy;
    to_px(atan2(z, sqrt(x * x + y * y)) / DEG, atan2(y, x) / DEG, sx, sy);
    // Segmente ueber die "Rueckseite" des Ausschnitts (Sprung um ~360 Grad) auslassen
    if (i > 0 && abs(sx - px0) < (int)(180.0 / V.dpp)) line(px0, py0, sx, sy, c, 20);
    px0 = sx; py0 = sy;
  }
}

static void square(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++) px(cx + dx, cy + dy, c);
}

static void diamond(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (abs(dx) + abs(dy) <= r) px(cx + dx, cy + dy, c);
}

static void render_spots(const MapSpot *sp, int n) {
  int r = V.dpp < 0.2 ? 5 : 3;
  for (int i = n - 1; i >= 0; i--)   // aelteste zuerst, neueste oben
    if (sp[i].shape == SHAPE_DX) great_circle(qlat, qlon, sp[i].lat, sp[i].lon, hex565(sp[i].color));
  for (int i = n - 1; i >= 0; i--) {
    int x, y;
    to_px(sp[i].lat, sp[i].lon, x, y);
    uint16_t c = hex565(sp[i].color), k = rgb(0, 0, 0);
    switch (sp[i].shape) {
      case SHAPE_SQUARE:  square(x, y, r, k);  square(x, y, r - 1, c); break;
      case SHAPE_DIAMOND: diamond(x, y, r + 2, k); diamond(x, y, r + 1, c); break;
      default:            disc(x, y, r + 1, k); disc(x, y, r, c); break;
    }
  }
}

// ---------- Marker ----------
static void render_markers(time_t t, const SunPos &s) {
  int x, y;
  MoonPos m = moon_position(t);
  to_px(m.decl, m.sublunar_lon, x, y);
  disc(x, y, 6, rgb(30, 30, 30));
  disc(x, y, 5, rgb(210, 210, 220));
  to_px(s.decl, s.subsolar_lon, x, y);
  disc(x, y, 8, rgb(60, 30, 0));
  disc(x, y, 7, rgb(255, 200, 40));
  disc(x, y, 4, rgb(255, 240, 150));
  to_px(qlat, qlon, x, y);
  ring(x, y, 7, 3, rgb(0, 0, 0));
  ring(x, y, 6, 2, rgb(255, 60, 60));
  disc(x, y, 1, rgb(255, 255, 255));
}

void map_render(uint16_t *buf, const MapView &v, time_t t, double qth_lat, double qth_lon,
                const MapSpot *spots, int n_spots, const MapOverlays *ov) {
  fb = buf;
  W = v.w;
  H = v.h;
  V = v;
  qlat = qth_lat;
  qlon = qth_lon;
  SunPos s = sun_position(t);
  render_daynight(s);
  if (ov && ov->drap)    // ab 1 MHz beeintraechtigt, voll ab 15 MHz
    render_grid_overlay(ov->drap, 90, 90, 89, -2, -178, 4, rgb(255, 70, 30), 5, 75);
  if (ov && ov->aurora)  // ab 5 % Wahrscheinlichkeit, voll ab 50 %
    render_grid_overlay(ov->aurora, 360, 181, -90, 1, 0, 1, rgb(40, 255, 110), 5, 50, ov->aurora_pct);
  render_grid();
  if (spots && n_spots > 0) render_spots(spots, n_spots);
  render_markers(t, s);
}
