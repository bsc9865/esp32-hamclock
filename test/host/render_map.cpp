// Rendert die Weltkarte auf dem PC (gleicher Code wie auf dem ESP32) als RGB565-Rohdaten.
//   c++ -O2 -std=c++17 -Isrc test/host/render_map.cpp src/map_render.cpp src/astro.cpp src/maps/*.c -o /tmp/rm
//   /tmp/rm <unix-zeit> karte.raw

#include "map_render.h"
#include "astro.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
static uint16_t buf[MAP_W*MAP_H];
int main(int argc,char**argv){
  LatLon q; maidenhead_to_latlon("JO50uh",q);
  time_t t = argc>1 ? atol(argv[1]) : time(nullptr);
  map_render(buf,t,q.lat,q.lon);
  FILE*f=fopen(argv[2],"wb"); fwrite(buf,2,MAP_W*MAP_H,f); fclose(f);
  char l[7]; latlon_to_maidenhead(q.lat,q.lon,l);
  printf("roundtrip locator %s, dist JO50uh->W1AW(41.71,-72.73) %.0f km brg %.0f\n",l,
     gc_distance_km(q.lat,q.lon,41.71,-72.73),gc_bearing_deg(q.lat,q.lon,41.71,-72.73));
}
