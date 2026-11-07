#!/usr/bin/env python3
"""Erzeugt assets/zones.bin aus den CQ- und ITU-Zonen von HB9HIL (MIT-Lizenz).

Quelle: https://github.com/HB9HIL/hamradio-zones-geojson (cqzones.geojson, ituzones.geojson)
Aufruf: python3 scripts/gen_zones.py cqzones.geojson ituzones.geojson [--tol 0.1]

Die Umrisse werden mit Douglas-Peucker vereinfacht (Toleranz in Grad).
Format (little endian):
  "ZON1", uint16 Anzahl Zonen
  je Zone: uint8 Typ (0 = CQ, 1 = ITU), uint8 Nummer, int16 Label-Breite, Label-Laenge,
           uint16 n, int16 min_lat, min_lon, max_lat, max_lon, dann n x (int16 lat, int16 lon)
  Koordinaten in 1/100 Grad.
"""
import argparse
import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def simplify(pts, tol):
    """Douglas-Peucker (iterativ) fuer eine Liste von (lat, lon)."""
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        (y1, x1), (y2, x2) = pts[a], pts[b]
        dx, dy = x2 - x1, y2 - y1
        norm = (dx * dx + dy * dy) ** 0.5
        best, idx = -1.0, -1
        for i in range(a + 1, b):
            y0, x0 = pts[i]
            d = abs(dy * x0 - dx * y0 + x2 * y1 - y2 * x1) / norm if norm else ((x0 - x1) ** 2 + (y0 - y1) ** 2) ** 0.5
            if d > best:
                best, idx = d, i
        if best > tol:
            keep[idx] = True
            stack += [(a, idx), (idx, b)]
    return [p for p, k in zip(pts, keep) if k]


def rings(feature):
    g = feature["geometry"]
    polys = [g["coordinates"]] if g["type"] == "Polygon" else g["coordinates"]
    for poly in polys:
        for ring in poly:
            yield [(lat, lon) for lon, lat in ring]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cq")
    ap.add_argument("itu")
    ap.add_argument("--tol", type=float, default=0.1)
    a = ap.parse_args()
    zones = []
    for typ, path, key in ((0, a.cq, "cq_zone"), (1, a.itu, "itu_zone")):
        for ft in json.load(open(path))["features"]:
            p = ft["properties"]
            num = int(p[key + "_number"])
            lab = p.get(key + "_name_loc") or [0, 0]
            for ring in rings(ft):
                s = simplify(ring, a.tol)
                pts = [(round(la * 100), round(lo * 100)) for la, lo in s]
                if len(pts) >= 3:
                    zones.append((typ, num, round(lab[0] * 100), round(lab[1] * 100), pts))
    out = bytearray(b"ZON1") + struct.pack("<H", len(zones))
    total = 0
    for typ, num, llat, llon, pts in zones:
        lats = [p[0] for p in pts]
        lons = [p[1] for p in pts]
        out += struct.pack("<BBhhH4h", typ, num, llat, llon, len(pts), min(lats), min(lons), max(lats), max(lons))
        for la, lo in pts:
            out += struct.pack("<hh", la, lo)
        total += len(pts)
    path = os.path.join(ROOT, "assets", "zones.bin")
    open(path, "wb").write(out)
    print(f"{path}: {len(zones)} Umrisse, {total} Punkte, {len(out)} Bytes (Toleranz {a.tol} Grad)")


if __name__ == "__main__":
    main()
