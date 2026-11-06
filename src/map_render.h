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
struct MapSpot {
  float lat, lon;
  uint32_t color;    // 0xRRGGBB
};

// Rendert Tag/Nacht-Karte, Gitter, Spots (Grosskreis-Pfade) und Marker in buf (v.w x v.h, RGB565)
void map_render(uint16_t *buf, const MapView &v, time_t t, double qth_lat, double qth_lon,
                const MapSpot *spots = nullptr, int n_spots = 0);
