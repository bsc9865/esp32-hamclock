#pragma once
#include <lvgl.h>
#include <time.h>

// Weltkarte (equirektangulaer) mit Tag/Nacht-Ueberblendung und Overlays.
void worldmap_create(lv_obj_t *parent, int x, int y, double qth_lat, double qth_lon);
void worldmap_render(time_t t);   // neu berechnen (ca. 1x pro Minute)
