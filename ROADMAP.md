# Roadmap: OpenHamClock-Funktionen auf dem ESP32-S3

Ziel: Den Funktionsumfang von [OpenHamClock](https://github.com/accius/OpenHamClock) so weit wie machbar
nativ auf dem Waveshare ESP32-S3-Touch-LCD-7 (800×480) nachbauen, mit eigenem Design. Das Gerät arbeitet
eigenständig: Es holt alle Daten selbst über WLAN, ohne Server dazwischen.

Legende: ✅ fertig · 🔨 in Arbeit · ⬜ geplant · 🟡 vereinfacht machbar · ❌ auf dem ESP32 nicht sinnvoll

## Phase 0: Basis
- ✅ Display, Touch, LVGL, Build mit PlatformIO
- ✅ Git-Versionierung, GitHub Actions (Firmware-Releases)

## Phase 1: Uhr und Weltraumwetter
- ⬜ WLAN, NTP, UTC und Ortszeit (MEZ/MESZ), Datum
- ⬜ Solardaten: SFI, SSN, A, K, Sonnenwind, Bz, X-Ray (hamqsl.com / NOAA SWPC)
- ⬜ Bandbedingungen HF Tag/Nacht, VHF (N0NBH / hamqsl.com)
- ⬜ Sonnenauf- und -untergang am QTH, Mondphase

## Phase 2: Weltkarte
- ⬜ Flache Karte (äquidistant-zylindrisch) mit Greyline (Tag/Nacht-Terminator)
- ⬜ QTH-Marker, Subsolarpunkt, Maidenhead-Gitter, CQ/ITU-Zonen
- 🟡 Azimutale Karte zentriert auf QTH (vorberechnete Projektion)
- ❌ 3D-Globus (WebGL)

## Phase 3: DX-Cluster
- ⬜ Telnet-DX-Cluster (DXSpider), Spot-Liste, Spots auf der Karte, Großkreis-Pfade
- ⬜ Filter: Band, Mode, Watchlist
- ⬜ Rufzeichen-Präfix → Land/Entfernung/Richtung (cty.dat, verkleinert)
- 🟡 RBN-Spots

## Phase 4: Aktivierungen
- ⬜ POTA, SOTA, WWFF (WWBOTA) als Liste und Kartenmarker

## Phase 5: Weltraumwetter erweitert
- ⬜ Kp-, SFI- und X-Ray-Verlaufsdiagramme, SWPC-Alarme
- 🟡 Aurora-Oval auf der Karte, D-RAP
- 🟡 Sonnenbilder (SDO, als verkleinertes JPEG)

## Phase 6: Satelliten
- ⬜ SGP4-Tracking (TLE von CelesTrak/AMSAT), Bahn und Footprint auf der Karte, nächste Überflüge

## Phase 7: Ausbreitung
- 🟡 MUF-Karte / Ionosonden-Daten (KC2G)
- 🟡 Vereinfachte Punkt-zu-Punkt-Prognose
- ❌ Volle ITU-R-P.533-Berechnung (WebAssembly, zu rechenintensiv)

## Phase 8: Station
- ⬜ Einstellungen am Touchscreen (Rufzeichen, Locator, WLAN, Themes), gespeichert im NVS
- ⬜ Web-Konfigurationsseite und OTA-Update über WLAN
- ⬜ WSJT-X-Decodes über UDP im LAN
- 🟡 PSKReporter (MQTT)
- 🟡 Logbuch (auf SD-Karte, ADIF-Export), Worked-Before
- 🟡 Rig-Steuerung (flrig/rigctld im Netz, oder CAT über RS485/UART)
- ⬜ Kontest-Kalender, DXpeditionen

## Phase 9: Extras
- 🟡 APRS-IS-Stationen auf der Karte
- 🟡 Wetter am QTH (Open-Meteo)
- ❌ EmComm-Dashboard (FEMA/NWS, US-spezifisch), ADS-B-Flugzeuge, Mesh-Netze (vorerst)
