#pragma once
// Dauerhafte Einstellungen im NVS (bleiben bei Firmware-Updates erhalten)

struct Settings {
  char ssid[33] = "";
  char pass[65] = "";
  char call[13] = "";
  char loc[7] = "";
};

extern Settings g_settings;

void settings_load();               // beim Start aufrufen
bool settings_save(const Settings &s);
bool settings_wifi_configured();

// Normalisiert und prueft einen Locator (z.B. "jo50UH" -> "JO50uh").
bool settings_normalize_locator(const char *in, char out[7]);
