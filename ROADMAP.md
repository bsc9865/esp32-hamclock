# Roadmap: OpenHamClock-Funktionen auf dem ESP32-S3

Ziel: Den Funktionsumfang von [OpenHamClock](https://github.com/accius/OpenHamClock) so weit wie machbar
nativ auf dem Waveshare ESP32-S3-Touch-LCD-7 (800×480) nachbauen, mit eigenem Design. Das Gerät arbeitet
eigenständig: Es holt alle Daten selbst über WLAN, ohne Server dazwischen.

Legende: ✅ fertig · 🔨 in Arbeit · ⬜ geplant · 🟡 vereinfacht machbar · ❌ auf dem ESP32 nicht sinnvoll

## Phase 0: Basis
- ✅ Display, Touch, LVGL, Build mit PlatformIO
- ✅ Git-Versionierung, GitHub Actions (Firmware-Releases)

## Phase 1: Uhr und Weltraumwetter
- ✅ WLAN, NTP, UTC und Ortszeit (MEZ/MESZ), Datum
- ✅ Solardaten: SFI, SSN, A, K, Sonnenwind, Bz, X-Ray (hamqsl.com / NOAA SWPC)
- ✅ Bandbedingungen HF Tag/Nacht, VHF (N0NBH / hamqsl.com)
- ✅ Sonnenauf- und -untergang am QTH, Mondphase

## Phase 2: Weltkarte
- ✅ Flache Karte (äquidistant-zylindrisch) mit Greyline (Tag/Nacht-Terminator)
- ✅ QTH-Marker, Subsolar-/Sublunarpunkt, Maidenhead-Gitter, Antippen → Locator/Entfernung/Richtung
- ✅ Ländergrenzen (Natural Earth)
- ⬜ CQ/ITU-Zonen
- ✅ Vollbild-Karte mit Zoom (1–8×), Verschieben, Spot-Beschriftung
- 🟡 Azimutale Karte zentriert auf QTH (vorberechnete Projektion)
- ❌ 3D-Globus (WebGL)

## Phase 3: DX-Cluster
- ✅ Telnet-DX-Cluster (DXSpider), Spot-Liste, Spots auf der Karte, Großkreis-Pfade
- ✅ Bandfilter
- ✅ Mode-Filter und Watchlist mit Hinweis
- ✅ Rufzeichen-Präfix → Land/Entfernung/Richtung (cty.dat)
- 🟡 RBN-Spots

## Phase 4: Aktivierungen
- ✅ POTA und WWFF als Liste und Kartenmarker
- ⏸ SOTA: API-Nutzung für KI-generierte Software nur mit Genehmigung (SOTA-Reflector, Gruppe „API-consumers“)
- ⬜ WWBOTA

## Phase 5: Weltraumwetter erweitert
- ✅ Kp-, SFI- und X-Ray-Verlaufsdiagramme, SWPC-Meldungen
- ✅ Aurora-Oval und D-RAP als Kartenebenen
- 🟡 Sonnenbilder (SDO, als verkleinertes JPEG)

## Phase 6: Satelliten
- ✅ SGP4-Tracking (TLE von CelesTrak), Bodenspur und Ausleuchtzone auf der Karte, nächste Überflüge
- ⬜ Doppler-Korrektur und Frequenzen je Satellit, Polardiagramm des Überflugs

## Phase 7: Ausbreitung
- ✅ MUF-Karte aus Ionosonden-Daten (KC2G/GIRO), vereinfachtes Modell
- ✅ Vereinfachte Bandprognose in acht Zielgebiete (jetzt bis +12 h)
- ❌ Volle ITU-R-P.533-Berechnung (WebAssembly, zu rechenintensiv)

## Phase 8: Station
- ✅ Einstellungen am Touchscreen (WLAN-Suche, Passwort, Rufzeichen, Locator), gespeichert im NVS
- ⬜ Weitere Einstellungen: Themes, Einheiten, Zeitzone, DX-Cluster-Server
- ✅ OTA-Update über WLAN (ArduinoOTA) und aus den GitHub-Releases
- ✅ Statusseite und Web-Konfigurationsseite
- ⬜ WSJT-X-Decodes über UDP im LAN
- 🟡 PSKReporter (MQTT)
- 🟡 Logbuch (auf SD-Karte, ADIF-Export), Worked-Before
- 🟡 Rig-Steuerung (flrig/rigctld im Netz, oder CAT über RS485/UART)
- ⬜ Kontest-Kalender, DXpeditionen

## Phase 9: Extras
- ✅ APRS-IS-Stationen auf der Karte (Umkreis 150 km)
- ✅ Wetter am QTH (Open-Meteo) mit Gewitterwarnung
- ❌ EmComm-Dashboard (FEMA/NWS, US-spezifisch), ADS-B-Flugzeuge, Mesh-Netze (vorerst)
