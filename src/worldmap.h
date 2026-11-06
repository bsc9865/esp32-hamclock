#pragma once
#include <lvgl.h>
#include <time.h>

struct MapSpot;

// Weltkarte (equirektangulaer) mit Tag/Nacht-Ueberblendung und Overlays.
void worldmap_create(lv_obj_t *parent, int x, int y, double qth_lat, double qth_lon);
const MapSpot *worldmap_spots(int &n);                  // aktuelle Spots (fuer die Vollbild-Karte)
void worldmap_render(time_t t);   // neu berechnen (ca. 1x pro Minute oder bei neuen Spots)

void worldmap_set_spots(const MapSpot *spots, int n);   // werden beim naechsten Render gezeichnet
void worldmap_show_info(const char *text);              // Infobox unten auf der Karte (8 s)
