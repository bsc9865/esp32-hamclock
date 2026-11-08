// Reines Karten-Rendering ohne LVGL/Arduino (auch auf dem PC testbar)
#include "map_render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "astro.h"

static constexpr double DEG = M_PI / 180.0;
static constexpr double SRC_DPP = 360.0 / MAP_SRC_W;   // Grad pro Quellpixel
static bool smoothing = true;
void map_set_smoothing(bool on) { smoothing = on; }

static double norm180(double a) {
  a = fmod(a + 180.0, 360.0);
  if (a < 0) a += 360.0;
  return a - 180.0;
}

// ---------- Ausschnitt ----------
MapView map_view_world(int w, int h) { return {w, h, 0.0, 0.0, 360.0 / w, 360.0 / w, false}; }

MapView map_view_zoom(int w, int h, double lat_c, double lon_c, double zoom) {
  double d = 360.0 / w / zoom;
  MapView v{w, h, lon_c, lat_c, d, d, false};
  map_view_clamp(v);
  return v;
}

// Deutschland: 47,2..55,1 Grad N, 5,8..15,1 Grad E
static constexpr double DE_LAT_MIN = 47.2, DE_LAT_MAX = 55.1, DE_LON_MIN = 5.8, DE_LON_MAX = 15.1;

MapView map_view_detail(int w, int h, double lat_c, double lon_c, double zoom) {
  double d = (DE_LAT_MAX - DE_LAT_MIN + 0.6) / h / zoom;   // zoom 1: Deutschland passt in die Hoehe
  MapView v{w, h, lon_c, lat_c, d, d / cos(51.0 * M_PI / 180.0), true};
  map_view_clamp(v);
  return v;
}

void map_view_clamp(MapView &v) {
  if (v.detail) {   // Mittelpunkt bleibt ueber Deutschland
    v.lat_c = fmin(DE_LAT_MAX, fmax(DE_LAT_MIN, v.lat_c));
    v.lon_c = fmin(DE_LON_MAX, fmax(DE_LON_MIN, v.lon_c));
    return;
  }
  v.lon_c = norm180(v.lon_c);
  double half = v.h * v.dpp / 2;
  if (half >= 90) v.lat_c = 0;
  else if (v.lat_c > 90 - half) v.lat_c = 90 - half;
  else if (v.lat_c < -90 + half) v.lat_c = -90 + half;
}

void map_view_to_xy(const MapView &v, double lat, double lon, double &x, double &y) {
  x = norm180(lon - v.lon_c) / v.dppx + v.w / 2.0;
  y = (v.lat_c - lat) / v.dpp + v.h / 2.0;
}

void map_view_to_latlon(const MapView &v, double x, double y, double &lat, double &lon) {
  lon = norm180(v.lon_c + (x - v.w / 2.0) * v.dppx);
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

// ---------- Detaildaten ----------
static inline int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static inline int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

struct Detail {
  bool ok = false;
  double lat_top, lon_left, dlat, dlon;
  int w, h;
  const uint8_t *px, *borders, *states, *cities;
};

static Detail detail() {
  Detail d;
  const uint8_t *b = maps_detail();
  if (!b || memcmp(b, "DET1", 4) != 0) return d;
  d.lat_top = rd32(b + 4) * 1e-5;
  d.lon_left = rd32(b + 8) * 1e-5;
  d.dlat = rd32(b + 12) * 1e-5;
  d.dlon = rd32(b + 16) * 1e-5;
  d.w = (uint16_t)rd16(b + 20);
  d.h = (uint16_t)rd16(b + 22);
  d.px = b + rd32(b + 24);
  d.borders = b + rd32(b + 28);
  d.states = b + rd32(b + 32);
  d.cities = b + rd32(b + 36);
  d.ok = true;
  return d;
}

// Staedte nacheinander lesen; p zeigt auf den naechsten Eintrag
static const uint8_t *next_city(const uint8_t *p, MapCity &c) {
  c.lat = 40.0f + rd16(p) / 1000.0f;
  c.lon = rd16(p + 2) / 1000.0f;
  c.rank = p[4];
  c.name = (const char *)(p + 5);
  return p + 5 + strlen(c.name) + 1;
}

int map_detail_cities(MapCity *out, int max) {
  Detail d = detail();
  if (!d.ok) return 0;
  int n = (uint16_t)rd16(d.cities), k = 0;
  const uint8_t *p = d.cities + 2;
  while (k < n && k < max) p = next_city(p, out[k++]);
  return k;
}

// ---------- Tag/Nacht ----------
// Quellraster fuer eine Spalte/Zeile: Index, Nachbarindex und Anteil (0..32)
struct Samp { int16_t i0, i1; uint8_t f; };

static void samp(double pos, int n, bool bilinear, bool wrap, Samp &s) {
  if (bilinear) pos -= 0.5;
  if (wrap) { if (pos < 0) pos += n; }
  else if (pos < 0) pos = 0;
  int i = (int)pos;
  if (wrap) i %= n;
  else if (i >= n) i = n - 1;
  int j = i + 1 < n ? i + 1 : (wrap ? 0 : i);
  s = {(int16_t)i, (int16_t)j, (uint8_t)((pos - floor(pos)) * 32)};
}

static void render_daynight(const SunPos &s) {
  const MapImage img = maps_image();
  const uint16_t *pd = img.pal_day, *pn = img.pal_night;
  const Detail det = V.detail ? detail() : Detail();
  // Vergroesserung -> glaetten
  const bool bil_w = smoothing && V.dpp < SRC_DPP * 0.9;
  const bool bil_d = det.ok && smoothing && V.dpp < det.dlat * 0.9;

  // Spalten-Tabellen (Weltkarte und Detailraster)
  static Samp *cw = nullptr, *cd = nullptr;
  static bool *cin = nullptr;
  static float *coslon = nullptr;
  static int cap = 0;
  if (cap < W) {
    free(cw); free(cd); free(cin); free(coslon);
    cw = (Samp *)malloc(W * sizeof(Samp));
    cd = (Samp *)malloc(W * sizeof(Samp));
    cin = (bool *)malloc(W);
    coslon = (float *)malloc(W * sizeof(float));
    cap = W;
  }
  for (int x = 0; x < W; x++) {
    double lon = norm180(V.lon_c + (x + 0.5 - W / 2.0) * V.dppx);
    samp((lon + 180.0) / SRC_DPP, MAP_SRC_W, bil_w, true, cw[x]);
    cin[x] = false;
    if (det.ok) {
      double u = (lon - det.lon_left) / det.dlon;
      cin[x] = u >= 0 && u < det.w;
      samp(u, det.w, bil_d, false, cd[x]);
    }
    coslon[x] = cos((lon - s.subsolar_lon) * DEG);
  }

  const float sd = sin(s.decl * DEG), cdc = cos(s.decl * DEG);
  // Uebergang von -12 Grad (nautische Daemmerung, volle Nacht) bis +3 Grad (voller Tag)
  const float lo = sinf(-12.0f * DEG), hi = sinf(3.0f * DEG);
  const float scale = 32.0f / (hi - lo);
  const uint16_t space = rgb(4, 6, 12);
  const int stride_w = MAP_SRC_W / 2, stride_d = det.w / 2;

  for (int y = 0; y < H; y++) {
    uint16_t *o = &fb[y * W];
    double lat = V.lat_c - (y + 0.5 - H / 2.0) * V.dpp;
    if (lat > 90 || lat < -90) {
      for (int x = 0; x < W; x++) o[x] = space;
      continue;
    }
    Samp rw, rd;
    samp((90.0 - lat) / SRC_DPP, MAP_SRC_H, bil_w, false, rw);
    const uint8_t *w0 = img.px + rw.i0 * stride_w, *w1 = img.px + rw.i1 * stride_w;
    bool row_in = false;
    const uint8_t *d0 = nullptr, *d1 = nullptr;
    if (det.ok) {
      double v = (det.lat_top - lat) / det.dlat;
      row_in = v >= 0 && v < det.h;
      samp(v, det.h, bil_d, false, rd);
      d0 = det.px + rd.i0 * stride_d;
      d1 = det.px + rd.i1 * stride_d;
    }
    float a = sin(lat * DEG) * sd, b = cos(lat * DEG) * cdc;

    for (int x = 0; x < W; x++) {
      // Tag/Nacht-Gewicht; Tag und Nacht teilen die Pixel, nur die Palette unterscheidet sich
      float cz = a + b * coslon[x];
      int w = (int)((cz - lo) * scale);
      const bool in = row_in && cin[x];
      const Samp &c = in ? cd[x] : cw[x];
      const Samp &r = in ? rd : rw;
      const uint8_t *r0 = in ? d0 : w0, *r1 = in ? d1 : w1;
      if (in ? bil_d : bil_w) {
        uint8_t i00 = map_px(r0, c.i0), i01 = map_px(r0, c.i1), i10 = map_px(r1, c.i0), i11 = map_px(r1, c.i1);
        uint16_t dc = 0, nc = 0;
        if (w > 0)
          dc = blend565(blend565(pd[i11], pd[i10], c.f), blend565(pd[i01], pd[i00], c.f), r.f);
        if (w < 32)
          nc = blend565(blend565(pn[i11], pn[i10], c.f), blend565(pn[i01], pn[i00], c.f), r.f);
        o[x] = w <= 0 ? nc : (w >= 32 ? dc : blend565(dc, nc, w));
      } else {
        uint8_t i = map_px(r0, c.i0);
        o[x] = w <= 0 ? pn[i] : (w >= 32 ? pd[i] : blend565(pd[i], pn[i], w));
      }
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
    double lon = norm180(V.lon_c + (x + 0.5 - W / 2.0) * V.dppx);
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

// ---------- Ebene: MUF ----------
// Baender: <7 | 7-10 | 10-14 | 14-18 | 18-21 | 21-24 | 24-28 | >=28 MHz
static const float MUF_EDGES[7] = {7, 10, 14, 18, 21, 24, 28};
static const uint32_t MUF_COLORS[8] = {0x3B4BDB, 0x22A6F0, 0x14C8B4, 0x3CD25A, 0xB4DC28, 0xF5C81E, 0xF57D1E, 0xE6283C};

static inline int muf_band(float mhz) {
  int b = 0;
  while (b < 7 && mhz >= MUF_EDGES[b]) b++;
  return b;
}

uint32_t map_muf_color(float mhz) { return MUF_COLORS[muf_band(mhz)]; }

static void render_muf(const uint8_t *g) {
  static int16_t *gx0 = nullptr;
  static uint8_t *gfx = nullptr;
  static int8_t *prev = nullptr;
  static int cap = 0;
  if (cap < W) {
    free(gx0); free(gfx); free(prev);
    gx0 = (int16_t *)malloc(W * sizeof(int16_t));
    gfx = (uint8_t *)malloc(W);
    prev = (int8_t *)malloc(W);
    cap = W;
  }
  uint16_t col[8];
  for (int i = 0; i < 8; i++) col[i] = hex565(MUF_COLORS[i]);
  for (int x = 0; x < W; x++) {
    double lon = norm180(V.lon_c + (x + 0.5 - W / 2.0) * V.dppx);
    double fx = (lon + 179.0) / 2.0;
    if (fx < 0) fx += 180.0;
    int i0 = (int)fx;
    gx0[x] = (int16_t)(i0 % 180);
    gfx[x] = (uint8_t)((fx - i0) * 16);
    prev[x] = -1;
  }
  for (int y = 0; y < H; y++) {
    double lat = V.lat_c - (y + 0.5 - H / 2.0) * V.dpp;
    if (lat > 90 || lat < -90) continue;
    double fy = (89.0 - lat) / 2.0;
    if (fy < 0) fy = 0;
    if (fy > 89) fy = 89;
    int r0 = (int)fy, r1 = r0 < 89 ? r0 + 1 : r0;
    int ty = (int)((fy - r0) * 16);
    const uint8_t *a = g + r0 * 180, *b = g + r1 * 180;
    uint16_t *o = &fb[y * W];
    int left = -1;
    for (int x = 0; x < W; x++) {
      int c0 = gx0[x], c1 = (c0 + 1) % 180, tx = gfx[x];
      int top = a[c0] * (16 - tx) + a[c1] * tx, bot = b[c0] * (16 - tx) + b[c1] * tx;
      float mhz = (top * (16 - ty) + bot * ty) / (256.0f * 4.0f);
      int band = muf_band(mhz);
      // Konturlinie an Bandgrenzen (links oder oben anderes Band), sonst leichte Flaechenfarbe
      bool edge = (left >= 0 && band != left) || (prev[x] >= 0 && band != prev[x]);
      o[x] = blend565(col[band], o[x], edge ? 26 : 9);
      left = band;
      prev[x] = (int8_t)band;
    }
  }
}

static void render_ionosondes(const MapOverlays &ov) {
  for (int i = 0; i < ov.iono_n; i++) {
    int x, y;
    to_px(ov.iono_lat[i], ov.iono_lon[i], x, y);
    disc(x, y, 5, rgb(255, 255, 255));
    disc(x, y, 4, hex565(map_muf_color(ov.iono_muf[i])));
  }
}

// ---------- Landgrenzen ----------
// Linienblock: uint16 Anzahl, je Linie uint16 n, int16-Bbox, n Punkte; Grad = lat_off + Wert * scale
static void render_lines(const uint8_t *p, double scale, double lat_off, uint16_t c, uint32_t alpha) {
  int nl = (uint16_t)rd16(p);
  p += 2;
  // sichtbarer Bereich (Grad) fuer das schnelle Aussortieren ganzer Linien
  double lat_top = V.lat_c + V.h / 2.0 * V.dpp, lat_bot = V.lat_c - V.h / 2.0 * V.dpp;
  double half_w = V.w / 2.0 * V.dppx;
  bool whole_world = half_w >= 180.0;
  int jump = (int)(180.0 / V.dppx);
  for (int i = 0; i < nl; i++) {
    int n = (uint16_t)rd16(p);
    double minlat = lat_off + rd16(p + 2) * scale, minlon = rd16(p + 4) * scale;
    double maxlat = lat_off + rd16(p + 6) * scale, maxlon = rd16(p + 8) * scale;
    const uint8_t *pts = p + 10;
    p = pts + n * 4;
    if (maxlat < lat_bot || minlat > lat_top) continue;
    if (!whole_world) {
      // Laengenabstand der Linienmitte zur Kartenmitte
      double mid = (minlon + maxlon) / 2.0, half = (maxlon - minlon) / 2.0;
      if (fabs(norm180(mid - V.lon_c)) > half_w + half) continue;
    }
    int x0 = 0, y0 = 0;
    for (int k = 0; k < n; k++) {
      int x1, y1;
      to_px(lat_off + rd16(pts + k * 4) * scale, rd16(pts + k * 4 + 2) * scale, x1, y1);
      if (k > 0 && abs(x1 - x0) < jump) line(x0, y0, x1, y1, c, alpha);
      x0 = x1;
      y0 = y1;
    }
  }
}

static void render_borders(const uint8_t *b) {
  const uint16_t c = rgb(255, 236, 190);
  if (V.detail) {   // genauere Grenzen und Bundeslaender
    Detail d = detail();
    if (d.ok) {
      render_lines(d.states, 0.001, 40.0, rgb(230, 230, 230), 13);
      render_lines(d.borders, 0.001, 40.0, c, 28);
      return;
    }
  }
  if (!b || memcmp(b, "BRD1", 4) != 0) return;
  render_lines(b + 4, 0.01, 0.0, c, V.dpp < 0.15 ? 26 : 20);
}

// Staedte der Detailansicht als kleine Punkte (Beschriftung uebernimmt die Oberflaeche)
static void render_cities() {
  Detail d = detail();
  if (!d.ok) return;
  int n = (uint16_t)rd16(d.cities);
  const uint8_t *p = d.cities + 2;
  for (int i = 0; i < n; i++) {
    MapCity c;
    p = next_city(p, c);
    int x, y;
    to_px(c.lat, c.lon, x, y);
    int r = c.rank == 0 ? 3 : 2;
    disc(x, y, r + 1, rgb(0, 0, 0));
    disc(x, y, r, rgb(235, 235, 235));
  }
}

// ---------- CQ-/ITU-Zonen ----------
const uint32_t ZONE_COLORS[2] = {0xFFA94D, 0x7DD3FC};

int map_zone_labels(uint8_t mask, ZoneLabel *out, int max) {
  const uint8_t *b = maps_zones();
  if (!b || memcmp(b, "ZON1", 4) != 0) return 0;
  int nz = (uint16_t)rd16(b + 4), k = 0;
  const uint8_t *p = b + 6;
  for (int i = 0; i < nz; i++) {
    uint8_t typ = p[0], num = p[1];
    int n = (uint16_t)rd16(p + 6);
    bool dup = false;   // Zonen aus mehreren Teilen nur einmal beschriften
    for (int j = 0; j < k; j++) if (out[j].type == typ && out[j].number == num) dup = true;
    if ((mask & (1 << typ)) && !dup && k < max) out[k++] = {typ, num, rd16(p + 2) / 100.0f, rd16(p + 4) / 100.0f};
    p += 16 + n * 4;
  }
  return k;
}

static void render_zones(uint8_t mask) {
  const uint8_t *b = maps_zones();
  if (!b || memcmp(b, "ZON1", 4) != 0) return;
  int nz = (uint16_t)rd16(b + 4);
  const uint8_t *p = b + 6;
  double lat_top = V.lat_c + V.h / 2.0 * V.dpp, lat_bot = V.lat_c - V.h / 2.0 * V.dpp;
  double half_w = V.w / 2.0 * V.dppx;
  bool whole_world = half_w >= 180.0;
  int jump = (int)(180.0 / V.dppx);
  uint32_t alpha = V.dpp < 0.15 ? 24 : 18;
  for (int i = 0; i < nz; i++) {
    uint8_t typ = p[0];
    int n = (uint16_t)rd16(p + 6);
    double minlat = rd16(p + 8) / 100.0, minlon = rd16(p + 10) / 100.0;
    double maxlat = rd16(p + 12) / 100.0, maxlon = rd16(p + 14) / 100.0;
    const uint8_t *pts = p + 16;
    p = pts + n * 4;
    if (!(mask & (1 << typ))) continue;
    if (maxlat < lat_bot || minlat > lat_top) continue;
    if (!whole_world) {
      double mid = (minlon + maxlon) / 2.0, half = (maxlon - minlon) / 2.0;
      if (fabs(norm180(mid - V.lon_c)) > half_w + half) continue;
    }
    uint16_t c = hex565(ZONE_COLORS[typ]);
    int x0 = 0, y0 = 0, xs = 0, ys = 0;
    for (int k = 0; k < n; k++) {
      int x1, y1;
      to_px(rd16(pts + k * 4) / 100.0, rd16(pts + k * 4 + 2) / 100.0, x1, y1);
      if (k == 0) { xs = x1; ys = y1; }
      else if (abs(x1 - x0) < jump) line(x0, y0, x1, y1, c, alpha);
      x0 = x1;
      y0 = y1;
    }
    if (abs(xs - x0) < jump) line(x0, y0, xs, ys, c, alpha);
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
  double span = W * V.dppx;
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
    if (i > 0 && abs(sx - px0) < (int)(180.0 / V.dppx)) line(px0, py0, sx, sy, c, 20);
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

// Linienzug aus Geo-Koordinaten (Spruenge ueber den Kartenrand werden ausgelassen)
static void polyline(const float *lat, const float *lon, int n, bool closed, uint16_t c, uint32_t w) {
  if (n < 2) return;
  int x0, y0, xs, ys;
  to_px(lat[0], lon[0], x0, y0);
  xs = x0; ys = y0;
  int jump = (int)(180.0 / V.dppx);
  for (int i = 1; i < n; i++) {
    int x1, y1;
    to_px(lat[i], lon[i], x1, y1);
    if (abs(x1 - x0) < jump) {
      line(x0, y0, x1, y1, c, w);
      line(x0, y0 + 1, x1, y1 + 1, c, w / 2);   // etwas kraeftiger
    }
    x0 = x1; y0 = y1;
  }
  if (closed && abs(xs - x0) < jump) line(x0, y0, xs, ys, c, w);
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
      case SHAPE_SAT:     ring(x, y, r + 4, 4, k); ring(x, y, r + 3, 2, c); disc(x, y, 1, c); break;
      case SHAPE_DOT:     disc(x, y, 4, k); disc(x, y, 3, c); break;
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
  if (ov && ov->muf) render_muf(ov->muf);
  render_grid();
  if (ov && ov->borders) render_borders(maps_borders());
  if (V.detail) render_cities();
  if (ov && (ov->cq_zones || ov->itu_zones)) render_zones((ov->cq_zones ? 1 : 0) | (ov->itu_zones ? 2 : 0));
  if (ov && ov->muf && ov->iono_n) render_ionosondes(*ov);
  if (ov && ov->fp_n > 2) polyline(ov->fp_lat, ov->fp_lon, ov->fp_n, true, rgb(255, 255, 255), 22);
  if (ov && ov->trk_n > 1) polyline(ov->trk_lat, ov->trk_lon, ov->trk_n, false, rgb(255, 220, 0), 26);
  if (spots && n_spots > 0) render_spots(spots, n_spots);
  render_markers(t, s);
}
