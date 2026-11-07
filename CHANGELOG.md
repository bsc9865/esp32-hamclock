# Changelog

Alle wichtigen Änderungen an diesem Projekt. Format nach [Keep a Changelog](https://keepachangelog.com/de/),
Versionierung nach [SemVer](https://semver.org/lang/de/).

## [Unreleased]
### Hinzugefügt
- Firmware-Updates über WLAN: ArduinoOTA mit Passwort (`pio run -t upload --upload-port esp32-hamclock.local`)
- Update aus den GitHub-Releases am Gerät: Prüfung alle 6 h, Hinweis in der Kopfzeile, Installation in den Einstellungen mit Fortschrittsanzeige; HTTPS mit Zertifikatsprüfung (ESP-IDF-Zertifikatsbündel)
- OTA-Passwort in den Einstellungen; wird aus `secrets.h` (`OTA_PASSWORD`) einmalig übernommen
### Geändert
- Repository ist öffentlich; vom Hybrid-Build erzeugte Dateien wurden aus der Historie entfernt

## [0.8.0] - 2026-11-07
### Hinzugefügt
- Ausbreitungsmodell: MUF(3000) aus aktuellen Ionosonden (KC2G/GIRO, alle 15 min) mit Tagesgang (1 h Verzögerung, Nacht 45 %) und entfernungsgewichteter Interpolation; mittlerer Fehler in der Kreuzvalidierung ca. 4 MHz
- Seite „Ausbreitung“ (Antippen von „HF-Bedingungen“): Bandprognose 80–10 m in acht Zielgebiete mit Entfernung, Richtung, Pfad-MUF und LUF, für jetzt bis +12 h; nächste Ionosonden mit Messwerten
- Kartenebene „MUF“: Farbflächen und Konturlinien nach Bandgrenzen, Ionosonden als Punkte (Vollbild: mit Messwert)
### Behoben
- Absturz durch `%f` in `lv_label_set_text_fmt`; Gleitkomma im LVGL-printf jetzt eingeschaltet

## [0.7.0] - 2026-11-07
### Hinzugefügt
- Satelliten: Bahnberechnung mit SGP4 (Vallado-Kern, MIT), TLE-Daten von CelesTrak (Gruppe „amateur“), im Dateisystem zwischengespeichert und höchstens alle 12 h neu geladen
- Reiter „Sat“: sichtbare Satelliten mit Elevation/Azimut, nächste Überflüge über dem QTH mit Uhrzeit und maximaler Höhe; Antippen zeigt AOS/Max/LOS mit Richtungen und Dauer und wählt den Satelliten aus
- Auswahl der Favoriten (bis 16) aus allen Amateurfunk-Satelliten, im NVS gespeichert; Voreinstellung: ISS, SO-50, RS-44, AO-7, FO-29, AO-73, IO-117, AO-123, QO-100, CAS-9, LilacSat-2
- Karte: Satelliten als Ringe (grün = über dem Horizont), ausgewählter Satellit mit Bodenspur und Ausleuchtzone; Vollbild-Karte mit Namen, antippbar
- Überflugvorhersage gegen Skyfield geprüft (≤ 1 s, ≤ 0,1°), Host-Test in der CI
### Geändert
- Rechte Spalte mit vier Reitern: DX, Aktiv, Sat, Sonne

## [0.6.0] - 2026-11-07
### Hinzugefügt
- Seite „Weltraumwetter“ (Antippen des Sonnen-Panels): Kp-Index 7 Tage als Balken (grün/gelb/rot nach Sturmstufe), GOES-Röntgenfluss 6 h logarithmisch mit A/B/C/M/X-Klassen, Solarflux 30 Tage, SWPC-Meldungen mit Volltext
- Kartenebenen „Aurora“ (OVATION-Vorhersage) und „D-RAP“ (Absorption), in der Vollbild-Karte schaltbar, gespeichert; Aurora standardmäßig an
- Deckkraft der Aurora-Ebene per Regler einstellbar (20–200 %), gespeichert
- Abrufe von NOAA SWPC: Kp (15 min), Röntgen (5 min), Meldungen (15 min), D-RAP (15 min), Aurora (30 min, 920 KB als Stream geparst), SFI (3 h)
- Host-Tests für alle SWPC-Parser

## [0.5.0] - 2026-11-06
### Hinzugefügt
- Aktivierungen: POTA (api.pota.app) und WWFF (spots.wwff.co), Abruf alle 2 Minuten, Anzeige der letzten 60 Minuten
- Reiter „Aktiv“: Typ (P/W), Frequenz, Rufzeichen, Referenz; Antippen zeigt Park-/Gebietsname, Mode, Region, Entfernung, Richtung, Kommentar
- POTA/WWFF einzeln ein- und ausblendbar; der Bandfilter gilt auch für Aktivierungen
- Kartenmarker: POTA als grünes Quadrat, WWFF als türkise Raute (ohne Pfad); in der Vollbild-Karte mit Rufzeichen und antippbar
- JSON-Verarbeitung mit ArduinoJson (Filter, Speicher im PSRAM); Umlaute und Akzente werden für die Schrift in ASCII umgesetzt
### Geändert
- Rechte Spalte mit drei Reitern: DX, Aktiv, Sonne
### Nicht umgesetzt
- SOTA: Die SOTA-API erlaubt KI-generierter Software den Zugriff nur nach vorheriger Genehmigung

## [0.4.1] - 2026-11-06
### Behoben
- Kartenbilder (`assets/*.bin`) fehlten im Repository (durch `*.bin` in `.gitignore` ausgeschlossen); CI und Release-Build schlugen deshalb fehl

## [0.4.0] - 2026-11-06
> Hinweis: Für v0.4.0 gibt es kein Release (Kartenbilder fehlten im Repo), siehe v0.4.1.

### Hinzugefügt
- DX-Cluster-Client (Telnet, DXSpider): Standard dxspider.co.uk:7300, Login als RUFZEICHEN-7, automatische Neuverbindung mit Backoff
- Spot-Liste in der rechten Spalte (Reiter „DX-Spots“): Bandfarbe, Frequenz, Rufzeichen, Alter; Antippen zeigt Land, CQ-Zone, Entfernung, Richtung, Mode, Spotter und Kommentar
- Spots auf der Weltkarte: farbige Punkte nach Band mit Großkreis-Pfaden vom QTH
- Bandfilter (160 m bis 70 cm) als Fenster, im NVS gespeichert
- Präfix-Datenbank aus cty.dat (AD1C) für Land, Kontinent, CQ-Zone und Position
- Mode-Erkennung aus Kommentar (FT8, CW, SSB, …) bzw. Bandplan
- DX-Cluster-Server in den Einstellungen änderbar
- Vollbild-Karte (Button „Vollbild“ auf der Dashboard-Karte): Zoom 1×–8× per Regler und +/−, Verschieben mit dem Finger, QTH zentrieren, Rufzeichen-Beschriftung ab Zoom 2, Antippen zeigt Spot-Details bzw. Locator/Entfernung
- Maidenhead-Quadrate (2°×1°) im Gitter bei starkem Zoom
### Geändert
- Kartenbilder in 1600×800 mit 256-Farben-Palette (eingebettete Binärdateien statt C-Arrays), bilineare Glättung beim Vergrößern
- Renderer zeichnet beliebige Kartenausschnitte (Mittelpunkt + Maßstab)
- Rechte Spalte mit Reitern: „DX-Spots“ und „Sonne/Mond“

## [0.3.0] - 2026-11-06
### Hinzugefügt
- Einstellungsseite am Gerät (Zahnrad oben rechts): WLAN-Suche mit Netzliste, Passwort per Bildschirmtastatur, Rufzeichen und Locator
- Einstellungen im NVS: bleiben bei Firmware-Updates erhalten; Speichern startet das Gerät neu
- Ersteinrichtung: ohne WLAN öffnet sich die Einstellungsseite automatisch und sucht Netze
### Geändert
- `include/secrets.h` ist optional: falls vorhanden, werden die WLAN-Daten beim ersten Start einmalig ins NVS übernommen
- Release-Firmware ist damit ohne Entwicklungsumgebung nutzbar

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
