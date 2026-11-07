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
| Display | 7" 800×480, RGB565 parallel (esp_lcd mit Bounce-Buffern) |
| Touch | GT911 (I²C, GPIO8/9, INT GPIO4) |
| IO-Expander | CH422G: Backlight, LCD/Touch-Reset, SD-CS, USB/CAN-Umschaltung |

## Bauen und Flashen

```bash
# PlatformIO braucht Python 3.10–3.14 (pioarduino-Plattform)
brew install python@3.13
python3.13 -m venv ~/.local/share/pio-venv && ~/.local/share/pio-venv/bin/pip install platformio
ln -s ~/.local/share/pio-venv/bin/pio ~/.local/bin/pio
cp include/secrets.h.example include/secrets.h   # optional: WLAN vorbelegen
pio run -t upload        # bauen und flashen
pio device monitor       # serielle Ausgabe
```

### Updates über WLAN

- **Am Gerät:** Einstellungen (Zahnrad) → *Prüfen* bzw. *Installieren*. Das Gerät prüft alle 6 Stunden,
  ob es auf GitHub eine neue Release-Version gibt, und zeigt das in der Kopfzeile an. Die Firmware wird
  per HTTPS mit Zertifikatsprüfung geladen.
- **Vom Rechner (Entwicklung):** `OTA_PASSWORD` in `include/secrets.h` setzen (wird beim ersten Start ins
  Gerät übernommen, alternativ in den Einstellungen eintragen), dann
  ```bash
  pio run -t upload --upload-port esp32-hamclock.local
  ```

### Fertige Firmware ohne Entwicklungsumgebung

Jedes getaggte Release (`vX.Y.Z`) wird von GitHub Actions gebaut. Unter *Releases* liegt dann:
- `esp32-hamclock-factory.bin`: komplettes Image für Adresse `0x0`
- `esp32-hamclock-app.bin`: nur die App für Adresse `0x10000`

```bash
esptool --chip esp32s3 write-flash 0x0 esp32-hamclock-factory.bin
```

Beim ersten Start öffnet sich die Einstellungsseite: WLAN auswählen, Passwort, Rufzeichen und Locator eingeben, speichern. Später erreichst du sie über das Zahnrad oben rechts.

## Projektstruktur

```
include/board_pins.h    Pinbelegung
include/board_io.h      CH422G- und GT911-Treiber
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
