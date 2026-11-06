#pragma once
// DX-Spot-Liste (rechte Spalte) mit Bandfilter
#include <lvgl.h>

void dxpanel_create(lv_obj_t *parent, int x, int y, int w, int h, double qth_lat, double qth_lon);
void dxpanel_update();          // aus einem UI-Timer aufrufen
bool dxpanel_map_dirty();       // true, wenn sich die Spots fuer die Karte geaendert haben
struct MapSpot;
int dxpanel_map_spots(MapSpot *out, int max);   // gefilterte Spots fuer die Karte (setzt dirty zurueck)

struct Spot;
int dxpanel_spots(const Spot **out);                       // aktuell angezeigte (gefilterte) Spots
void dxpanel_format_spot(const Spot &s, char *buf, size_t n);   // zweizeilige Detailbeschreibung
