// Host-Test fuer Satellitenfrequenzen: Amateurfunk-Bandfilter und Doppler
//   c++ -std=c++17 -Isrc test/host/test_satfreq.cpp -o /tmp/tsf && /tmp/tsf
#include "satfreq_util.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FEHLER: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
  // Baender
  CHECK(satfreq_amateur_hz(145800000ULL), "145,800 MHz");
  CHECK(satfreq_amateur_hz(437800000ULL), "437,800 MHz");
  CHECK(satfreq_amateur_hz(29400000ULL), "29,400 MHz (AO-7 Mode A)");
  CHECK(satfreq_amateur_hz(2400150000ULL), "2400,150 MHz (QO-100 Uplink)");
  CHECK(satfreq_amateur_hz(10489750000ULL), "10489,750 MHz (QO-100 Downlink)");
  CHECK(!satfreq_amateur_hz(121750000ULL), "121,750 MHz (Flugfunk) ausgeschlossen");
  CHECK(!satfreq_amateur_hz(2265000000ULL), "2265 MHz (ISS-Daten) ausgeschlossen");
  CHECK(!satfreq_amateur_hz(149025000ULL), "149,025 MHz ausgeschlossen");

  // Doppler: Satellit naehert sich mit 7 km/s -> Empfang hoeher, Sendung tiefer
  double rx = satfreq_rx(437800000.0, -7.0), tx = satfreq_tx(145990000.0, -7.0);
  CHECK(fabs((rx - 437800000.0) - 10222.4) < 1.0, "RX-Verschiebung %.1f Hz", rx - 437800000.0);
  CHECK(tx < 145990000.0 && fabs((145990000.0 - tx) - 3408.6) < 1.0, "TX-Verschiebung %.1f Hz", tx - 145990000.0);
  // Hin und zurueck: Satellit empfaengt genau die Nennfrequenz
  double at_sat = tx * (1.0 + 7.0 / SPEED_OF_LIGHT_KMS);
  CHECK(fabs(at_sat - 145990000.0) < 0.01, "Uplink am Satelliten %.3f", at_sat);
  CHECK(satfreq_rx(145800000.0, 0) == 145800000.0, "ohne Bewegung keine Verschiebung");

  printf(fails ? "%d Fehler\n" : "Satellitenfrequenzen: alle Tests bestanden\n", fails);
  return fails != 0;
}
