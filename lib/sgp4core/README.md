# sgp4core

SGP4/SDP4-Bahnmodell (David Vallado, "Revisiting Spacetrack Report #3"), entnommen aus
[Hopperpop/Sgp4-Library](https://github.com/Hopperpop/Sgp4-Library) v1.0.3 (MIT-Lizenz, siehe LICENSE).

Übernommen sind nur die Arduino-unabhängigen Dateien `sgp4unit`, `sgp4io` und `sgp4ext`,
damit der Code auch in den Host-Tests auf dem PC läuft. Positionsberechnung, Blickwinkel und
Überflugvorhersage stehen in `src/satellite.cpp`.
