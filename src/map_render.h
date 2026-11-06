#pragma once
#include <stdint.h>
#include <time.h>
#include "maps/maps.h"

// Rendert Tag/Nacht-Karte, Gitter und Marker in buf (MAP_W x MAP_H, RGB565)
void map_render(uint16_t *buf, time_t t, double qth_lat, double qth_lon);

// Koordinatenumrechnung Karte <-> Geo
void worldmap_to_xy(double lat, double lon, int &x, int &y);
void worldmap_to_latlon(int x, int y, double &lat, double &lon);
