#pragma once
// Liste der Aktivierungen (POTA/WWFF) in der rechten Spalte
#include <lvgl.h>
#include <stddef.h>

struct Activation;
struct MapSpot;

void actpanel_create(lv_obj_t *parent, int w, double qth_lat, double qth_lon);
void actpanel_update();                               // aus einem UI-Timer aufrufen
bool actpanel_map_dirty();
int actpanel_map_spots(MapSpot *out, int max);        // Marker fuer die Karte (setzt dirty zurueck)
int actpanel_items(const Activation **out);           // aktuell angezeigte Aktivierungen
void actpanel_format(const Activation &a, char *buf, size_t n);   // zweizeilige Detailbeschreibung
