#pragma once
#include <stdint.h>
#include <time.h>
#include "maps/maps.h"

// DX-Station fuer die Karte
struct MapSpot {
  float lat, lon;
  uint32_t color;    // 0xRRGGBB
};

// Rendert Tag/Nacht-Karte, Gitter, Spots (Grosskreis-Pfade) und Marker in buf (MAP_W x MAP_H, RGB565)
void map_render(uint16_t *buf, time_t t, double qth_lat, double qth_lon,
                const MapSpot *spots = nullptr, int n_spots = 0);

// Koordinatenumrechnung Karte <-> Geo
void worldmap_to_xy(double lat, double lon, int &x, int &y);
void worldmap_to_latlon(int x, int y, double &lat, double &lon);
