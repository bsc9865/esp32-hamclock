# Changelog

Alle wichtigen Änderungen an diesem Projekt. Format nach [Keep a Changelog](https://keepachangelog.com/de/),
Versionierung nach [SemVer](https://semver.org/lang/de/).

## [Unreleased]
### Hinzugefügt
- Dashboard-Layout für 800×480: Kopfzeile, Uhr, Sonnendaten, HF-Bedingungen, Platz für die Karte, Infospalte
- WLAN-Verbindung (Zugangsdaten in `include/secrets.h`), NTP-Zeit, UTC und MEZ/MESZ
- Solardaten und Bandbedingungen von hamqsl.com (N0NBH): SFI, SSN, A, K, X-Ray, Bz, Sonnenwind, VHF/Es
- Sonnenauf- und -untergang, Sonnenhöhe und -azimut am QTH, Mondphase
- Abruf-Task auf Core 0 mit Wiederholung bei Fehlern
- Weltkarte 560×280: NASA Blue Marble (Tag) und Black Marble (Nacht), überblendet entlang der Tag/Nacht-Grenze mit Dämmerungszone (−12° bis +3°)
- Maidenhead-Feldgitter, Subsolarpunkt (Sonne), Sublunarpunkt (Mond) und QTH-Marker auf der Karte
- Antippen der Karte zeigt Locator, Koordinaten, Entfernung und Richtung vom QTH
- Host-Tests für Astronomie und Geo-Rechnungen, laufen in der CI mit
- `scripts/gen_maps.py` erzeugt die Kartenbilder aus den NASA-Vorlagen
### Geändert
- Code in Module aufgeteilt (display, net, astro, solar, datastore, ui), C++17

## [0.1.0] - 2026-11-06
### Hinzugefügt
- Grundgerüst: PlatformIO, Arduino, LovyanGFX, LVGL 8.3
- Treiber für das RGB-Display, den GT911-Touch und den CH422G-IO-Expander
- Demo-Oberfläche mit Zähler, Reglern und Systeminfo
- Versionsnummer aus `git describe`, GitHub Actions für Build und Releases
