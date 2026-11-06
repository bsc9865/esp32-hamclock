# ESP32-HamClock

Ein eigenständiges Amateurfunk-Dashboard nach dem Vorbild von
[OpenHamClock](https://github.com/accius/OpenHamClock) für das
**Waveshare ESP32-S3-Touch-LCD-7** (7", 800×480, kapazitiver Touch).

Station: **DJ0SB** · Locator **JO50uh**

Den geplanten Funktionsumfang und den aktuellen Stand zeigt die [ROADMAP.md](ROADMAP.md).

## Hardware

| Komponente | Details |
|---|---|
| MCU | ESP32-S3, 16 MB Flash, 8 MB Octal-PSRAM |
| Display | 7" 800×480, RGB565 parallel |
| Touch | GT911 (I²C, GPIO8/9, INT GPIO4) |
| IO-Expander | CH422G: Backlight, LCD/Touch-Reset, SD-CS, USB/CAN-Umschaltung |

## Bauen und Flashen

```bash
brew install platformio
cp include/secrets.h.example include/secrets.h   # WLAN-Daten eintragen
pio run -t upload        # bauen und flashen
pio device monitor       # serielle Ausgabe
```

### Fertige Firmware ohne Entwicklungsumgebung

Jedes getaggte Release (`vX.Y.Z`) wird von GitHub Actions gebaut. Unter *Releases* liegt dann:
- `esp32-hamclock-factory.bin`: komplettes Image für Adresse `0x0`
- `esp32-hamclock-app.bin`: nur die App für Adresse `0x10000`

```bash
esptool.py --chip esp32s3 write_flash 0x0 esp32-hamclock-factory.bin
```

## Projektstruktur

```
include/board_pins.h    Pinbelegung
include/board_io.h      CH422G- und GT911-Treiber
include/lgfx_config.h   Display-Treiber (LovyanGFX)
include/lv_conf.h       LVGL-Konfiguration
include/config.h        Station und Einstellungen
src/                    Anwendung
scripts/version.py      Versionsnummer aus git describe
```

## Versionierung

Semantische Versionierung über Git-Tags. Die Firmware-Version (`FW_VERSION`) kommt automatisch aus
`git describe` und wird auf dem Gerät angezeigt. Änderungen stehen in der [CHANGELOG.md](CHANGELOG.md).

## Lizenz

MIT
