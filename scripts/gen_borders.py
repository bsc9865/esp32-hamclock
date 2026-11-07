#!/usr/bin/env python3
"""Erzeugt assets/borders.bin aus Natural-Earth-Landgrenzen (gemeinfrei).

Quelle: https://www.naturalearthdata.com/ (ne_50m_admin_0_boundary_lines_land), z.B.
  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_admin_0_boundary_lines_land.geojson

Aufruf: python3 scripts/gen_borders.py ne_50m_admin_0_boundary_lines_land.geojson

Format (little endian):
  "BRD1", uint16 Anzahl Linien
  je Linie: uint16 n, int16 min_lat, min_lon, max_lat, max_lon, dann n x (int16 lat, int16 lon)
  Koordinaten in 1/100 Grad.
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    d = json.load(open(sys.argv[1]))
    lines = []
    for ft in d["features"]:
        g = ft["geometry"]
        parts = [g["coordinates"]] if g["type"] == "LineString" else g["coordinates"]
        for p in parts:
            pts = [(round(lat * 100), round(lon * 100)) for lon, lat in p]
            # aufeinanderfolgende Duplikate entfernen
            dedup = [pts[0]] + [q for a, q in zip(pts, pts[1:]) if q != a]
            if len(dedup) >= 2:
                lines.append(dedup)
    out = bytearray(b"BRD1")
    out += struct.pack("<H", len(lines))
    total = 0
    for pts in lines:
        lats = [p[0] for p in pts]
        lons = [p[1] for p in pts]
        out += struct.pack("<H4h", len(pts), min(lats), min(lons), max(lats), max(lons))
        for la, lo in pts:
            out += struct.pack("<hh", la, lo)
        total += len(pts)
    path = os.path.join(ROOT, "assets", "borders.bin")
    open(path, "wb").write(out)
    print(f"{path}: {len(lines)} Linien, {total} Punkte, {len(out)} Bytes")


if __name__ == "__main__":
    main()
