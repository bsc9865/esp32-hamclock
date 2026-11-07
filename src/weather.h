#pragma once
// Wetter am QTH von Open-Meteo (CC BY 4.0, frei fuer nicht-kommerzielle Nutzung)
#include <stdint.h>
#include <time.h>

struct WxHour { time_t t; float temp; uint8_t code; uint8_t pop; };   // pop = Regenwahrscheinlichkeit %
struct WxDay { time_t t; float tmin, tmax; uint8_t code; uint8_t pop; };

struct Weather {
  bool valid;
  time_t fetched;
  float temp, feels, wind, gust, pressure, precip;
  uint8_t rh, clouds, code;
  uint16_t wind_dir;
  WxHour hours[12]; int n_hours;
  WxDay days[3];    int n_days;
  bool thunder;      // Gewitter jetzt oder in den naechsten 3 Stunden
};

void weather_begin(double lat, double lon);
bool weather_fetch();          // aus dem Daten-Task
uint32_t weather_seq();
Weather weather_get();

const char *wx_code_text(uint8_t code);     // z.B. "bedeckt", "Gewitter"
const char *wx_wind_dir(uint16_t deg);      // z.B. "SO"
