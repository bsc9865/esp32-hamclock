#include "weather.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "net.h"
#include "timeutil.h"

static SemaphoreHandle_t mtx;
static Weather data;
static volatile uint32_t seq = 0;
static char url[420];

uint32_t weather_seq() { return seq; }

Weather weather_get() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  Weather w = data;
  xSemaphoreGive(mtx);
  return w;
}

const char *wx_code_text(uint8_t c) {
  switch (c) {
    case 0: return "klar";
    case 1: return "meist klar";
    case 2: return "teils bewoelkt";
    case 3: return "bedeckt";
    case 45: case 48: return "Nebel";
    case 51: case 53: case 55: return "Niesel";
    case 56: case 57: return "gefr. Niesel";
    case 61: return "leichter Regen";
    case 63: return "Regen";
    case 65: return "starker Regen";
    case 66: case 67: return "gefr. Regen";
    case 71: return "leichter Schnee";
    case 73: return "Schnee";
    case 75: return "starker Schnee";
    case 77: return "Schneegriesel";
    case 80: case 81: return "Schauer";
    case 82: return "starke Schauer";
    case 85: case 86: return "Schneeschauer";
    case 95: return "Gewitter";
    case 96: case 99: return "Gewitter, Hagel";
    default: return "?";
  }
}

const char *wx_wind_dir(uint16_t d) {
  static const char *n[] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
  return n[((d + 22) % 360) / 45];
}

// Open-Meteo liefert Ortszeit (timezone=auto) ohne Offset; mit utc_offset_seconds umrechnen
static time_t local_iso(const char *s, long offset) { return s ? parse_iso_utc(s) - offset : 0; }

bool weather_fetch() {
  String body;
  if (!http_get(url, body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    Serial.println("Wetter: JSON-Fehler");
    return false;
  }
  long off = doc["utc_offset_seconds"] | 0L;
  Weather w{};
  JsonObject c = doc["current"];
  w.temp = c["temperature_2m"] | NAN;
  w.feels = c["apparent_temperature"] | NAN;
  w.rh = c["relative_humidity_2m"] | 0;
  w.code = c["weather_code"] | 0;
  w.wind = c["wind_speed_10m"] | 0.0f;
  w.wind_dir = c["wind_direction_10m"] | 0;
  w.gust = c["wind_gusts_10m"] | 0.0f;
  w.pressure = c["pressure_msl"] | 0.0f;
  w.clouds = c["cloud_cover"] | 0;
  w.precip = c["precipitation"] | 0.0f;
  w.thunder = w.code >= 95;

  JsonArray ht = doc["hourly"]["time"];
  for (size_t i = 0; i < ht.size() && w.n_hours < 12; i++) {
    WxHour &h = w.hours[w.n_hours++];
    h.t = local_iso(ht[i], off);
    h.temp = doc["hourly"]["temperature_2m"][i] | NAN;
    h.code = doc["hourly"]["weather_code"][i] | 0;
    h.pop = doc["hourly"]["precipitation_probability"][i] | 0;
    if (i < 3 && h.code >= 95) w.thunder = true;
  }
  JsonArray dt = doc["daily"]["time"];
  for (size_t i = 0; i < dt.size() && w.n_days < 3; i++) {
    WxDay &d = w.days[w.n_days++];
    d.t = local_iso(dt[i], off) + 12 * 3600;   // Tagesmitte
    d.tmin = doc["daily"]["temperature_2m_min"][i] | NAN;
    d.tmax = doc["daily"]["temperature_2m_max"][i] | NAN;
    d.code = doc["daily"]["weather_code"][i] | 0;
    d.pop = doc["daily"]["precipitation_probability_max"][i] | 0;
  }
  w.valid = !isnan(w.temp);
  w.fetched = time(nullptr);
  if (!w.valid) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  data = w;
  xSemaphoreGive(mtx);
  seq++;
  Serial.printf("Wetter: %.1f C, %s, Wind %s %.0f km/h%s\n", w.temp, wx_code_text(w.code), wx_wind_dir(w.wind_dir),
                w.wind, w.thunder ? "  GEWITTER" : "");
  return true;
}

void weather_begin(double lat, double lon) {
  mtx = xSemaphoreCreateMutex();
  snprintf(url, sizeof url,
           "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,"
           "wind_direction_10m,wind_gusts_10m,pressure_msl,cloud_cover,precipitation"
           "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=12"
           "&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max"
           "&forecast_days=3&timezone=auto",
           lat, lon);
}
