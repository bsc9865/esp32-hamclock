#pragma once
// Satelliten-Reiter: naechste Ueberfluege, Auswahl der Favoriten, Kartenmarker
#include <lvgl.h>
#include <stddef.h>

struct MapSpot;
struct MapOverlays;
struct SatRow;

void satpanel_create(lv_obj_t *parent, int w);
void satpanel_update();                         // aus einem UI-Timer
bool satpanel_map_dirty();
int satpanel_map_spots(MapSpot *out, int max);  // Satelliten-Marker (setzt dirty zurueck)
void satpanel_overlays(MapOverlays &ov);        // Bodenspur/Ausleuchtzone des ausgewaehlten Satelliten
int satpanel_rows(const SatRow **out);
void satpanel_format(const SatRow &r, char *buf, size_t n);
