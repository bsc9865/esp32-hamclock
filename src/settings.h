#pragma once
#include <stdint.h>
#include <stddef.h>
// Dauerhafte Einstellungen im NVS (bleiben bei Firmware-Updates erhalten)

struct Settings {
  char ssid[33] = "";
  char pass[65] = "";
  char call[13] = "";
  char loc[7] = "";
  char cluster[48] = "";      // "host:port"
  uint16_t band_mask = 0;     // Bit je Band (siehe spots.h)
};

extern Settings g_settings;

void settings_load();               // beim Start aufrufen
bool settings_save(const Settings &s);
bool settings_wifi_configured();
bool settings_save_band_mask(uint16_t mask);   // ohne Neustart

// Zerlegt "host:port" (Standard-Port 7300)
bool settings_cluster_hostport(char *host, size_t n, uint16_t &port);

// Normalisiert und prueft einen Locator (z.B. "jo50UH" -> "JO50uh").
bool settings_normalize_locator(const char *in, char out[7]);
