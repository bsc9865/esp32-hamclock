# Changelog

Alle wichtigen Änderungen an diesem Projekt. Format nach [Keep a Changelog](https://keepachangelog.com/de/),
Versionierung nach [SemVer](https://semver.org/lang/de/).

## [Unreleased]

## [0.2.1] - 2026-11-06
### Geändert
- LVGL-Speicher (Widgets, Styles, Texte) kommt aus dem PSRAM statt aus dem internen RAM
- TLS (mbedTLS) und WLAN/lwIP-Puffer dürfen den PSRAM nutzen: intern frei nach dem Start 60 → 81 KB
- Anzeige von freiem RAM, Tiefststand und Laufzeit in der Infospalte, Statuszeile alle 5 Minuten auf Serial
### Behoben
- Absturz in lwIP („Required to lock TCPIP core“) durch einen Wettlauf zwischen SNTP-DNS-Anfrage und dem DNS-Cache-Leeren im Arduino-Core: SNTP startet jetzt erst nach der ersten eigenen Namensauflösung, Datenabrufe erst mit gültiger Uhrzeit

## [0.2.0] - 2026-11-06
> Hinweis: Für v0.2.0 gibt es kein Release (CI-Build fehlgeschlagen), erstes Release ist v0.2.1.

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
### Behoben
- Springendes/verrutschendes Bild: Display-Treiber auf ESP-IDF `esp_lcd` mit Bounce-Buffern umgestellt; WLAN schreibt nicht mehr in den Flash
- Pixelfehler am linken Bildrand: eigene ESP-IDF-Konfiguration (`custom_sdkconfig`): Code aus PSRAM (XIP), ISR-sicherer Display-Treiber, 64-KB-Daten-Cache mit 64-B-Zeilen, Bounce-Buffer 20 Zeilen
### Geändert
- Plattform auf pioarduino (Arduino-Core 3.3 / ESP-IDF 5.5) umgestellt, LovyanGFX entfernt
- Serielle Ausgabe über den UART-Port (CH343)
- Code in Module aufgeteilt (display, net, astro, solar, datastore, ui), C++17

## [0.1.0] - 2026-11-06
### Hinzugefügt
- Grundgerüst: PlatformIO, Arduino, LovyanGFX, LVGL 8.3
- Treiber für das RGB-Display, den GT911-Touch und den CH422G-IO-Expander
- Demo-Oberfläche mit Zähler, Reglern und Systeminfo
- Versionsnummer aus `git describe`, GitHub Actions für Build und Releases
