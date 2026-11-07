#pragma once
// Reines Karten-Rendering ohne LVGL/Arduino (auch auf dem PC testbar)
#include <stdint.h>
#include <time.h>
#include "maps/maps.h"

// Kartenausschnitt (equirektangulaer)
struct MapView {
  int w, h;              // Groesse des Zielpuffers in Pixeln
  double lon_c, lat_c;   // Mittelpunkt
  double dpp;            // Grad pro Pixel
};

MapView map_view_world(int w, int h);                       // ganze Welt, Mitte 0/0
MapView map_view_zoom(int w, int h, double lat_c, double lon_c, double zoom);  // zoom 1 = ganze Welt
void map_view_clamp(MapView &v);                            // Mittelpunkt in gueltigen Bereich
void map_view_to_xy(const MapView &v, double lat, double lon, double &x, double &y);
void map_view_to_latlon(const MapView &v, double x, double y, double &lat, double &lon);

// DX-Station fuer die Karte
enum MapShape : uint8_t { SHAPE_DX = 0, SHAPE_SQUARE = 1, SHAPE_DIAMOND = 2 };

struct MapSpot {
  float lat, lon;
  uint32_t color;    // 0xRRGGBB
  uint8_t shape;     // SHAPE_DX: Punkt mit Grosskreis-Pfad; sonst Marker ohne Pfad
};

// Zusaetzliche Kartenebenen (nullptr = aus)
struct MapOverlays {
  const uint8_t *aurora = nullptr;   // 360 x 181, Wahrscheinlichkeit 0..100 (Zeile 0 = -90 Grad)
  const uint8_t *drap = nullptr;     // 90 x 90, MHz*5 (Zeile 0 = 89 Grad, Spalte 0 = -178 Grad)
};

// Rendert Tag/Nacht-Karte, Ebenen, Gitter, Spots (Grosskreis-Pfade) und Marker in buf (v.w x v.h, RGB565)
void map_render(uint16_t *buf, const MapView &v, time_t t, double qth_lat, double qth_lon,
                const MapSpot *spots = nullptr, int n_spots = 0, const MapOverlays *ov = nullptr);
