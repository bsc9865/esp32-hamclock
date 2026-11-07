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
  uint8_t layers = 0;         // Kartenebenen (LAYER_*)
  uint8_t aurora_pct = 100;   // Deckkraft der Aurora-Ebene in Prozent (20..200)
  char ota_pw[33] = "";       // Passwort fuer Updates ueber das Netzwerk (leer = aus)
  char watch[256] = "";       // DX-Watchlist (siehe watch.h)
  uint8_t mode_mask = 0x0F;   // Mode-Filter (MODE_* aus watch.h)
};

// LAYER_HIDE_BORDERS ist umgekehrt gespeichert, damit Grenzen auch bei vorhandenen Einstellungen sichtbar sind
enum : uint8_t { LAYER_AURORA = 1, LAYER_DRAP = 2, LAYER_MUF = 4, LAYER_APRS = 8, LAYER_HIDE_BORDERS = 16 };
#define LAYERS_DEFAULT LAYER_AURORA

extern Settings g_settings;

void settings_load();               // beim Start aufrufen
bool settings_save(const Settings &s);
bool settings_wifi_configured();
bool settings_save_band_mask(uint16_t mask);   // ohne Neustart
bool settings_save_layers(uint8_t layers);     // ohne Neustart
bool settings_save_aurora_pct(uint8_t pct);    // ohne Neustart

// Aenderungen aus einem anderen Task (Webserver) vormerken; settings_apply_pending() in loop()
// uebernimmt sie. restart = Geraet danach neu starten (WLAN, Rufzeichen, Locator, Cluster).
void settings_request(const Settings &s, bool restart);
void settings_apply_pending();
uint32_t settings_seq();          // aendert sich bei jeder Uebernahme

// Zerlegt "host:port" (Standard-Port 7300)
bool settings_cluster_hostport(char *host, size_t n, uint16_t &port);

// Normalisiert und prueft einen Locator (z.B. "jo50UH" -> "JO50uh").
bool settings_normalize_locator(const char *in, char out[7]);
