#!/usr/bin/env python3
"""Erzeugt assets/detail.bin: Detailkarte Deutschland/Mitteleuropa aus Natural Earth 1:10 Mio. (gemeinfrei).

Quellen (https://github.com/nvkelso/natural-earth-vector/tree/master/geojson):
  ne_10m_land, ne_10m_lakes, ne_10m_lakes_europe, ne_10m_admin_0_boundary_lines_land,
  ne_10m_admin_1_states_provinces_lines, ne_10m_populated_places_simple

Aufruf (benoetigt Pillow):
  python3 scripts/gen_detail.py VERZEICHNIS_MIT_GEOJSON [--preview DIR]

Format (little endian):
  "DET1"
  int32 lat_top, lon_left, dlat, dlon   (1e-5 Grad; Raster beginnt oben links)
  uint16 w, h                            (Rasterbreite muss gerade sein)
  uint32 off_raster, off_borders, off_states, off_cities   (ab Dateianfang)
  Raster: w*h/2 Bytes, 4 Bit Landanteil wie assets/map.bin (gleiche Paletten)
  Linien (Grenzen, Bundeslaender): uint16 Anzahl, je Linie uint16 n, int16 min_lat, min_lon, max_lat, max_lon,
    dann n x (int16 lat, int16 lon); Kodierung lat = (Grad - 40) * 1000, lon = Grad * 1000
  Staedte: uint16 Anzahl, je Stadt int16 lat, lon (wie oben), uint8 Rang (0 = gross .. 2 = klein), Name (ASCII, NUL)
"""
import argparse
import json
import os
import struct
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Raster: Deutschland mit Rand, ca. 1,1 km pro Pixel in beiden Richtungen (bei 51 Grad Breite)
LAT_TOP, LON_LEFT, DLAT, DLON = 56.0, 1.5, 0.01, 0.016
RW, RH, SS = 1188, 1050, 4
# Linien: alles, was in der DE-Ansicht sichtbar werden kann
LBOX = (42.5, -6.5, 60.0, 27.5)   # min_lat, min_lon, max_lat, max_lon
TOL = 0.002                        # Douglas-Peucker-Toleranz in Grad

NAMES = {"Munich": "Muenchen", "Cologne": "Koeln", "Hanover": "Hannover", "Nuremberg": "Nuernberg"}


def ascii_de(s):
    s = NAMES.get(s, s)
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("Ä", "Ae"), ("Ö", "Oe"), ("Ü", "Ue"), ("ß", "ss")):
        s = s.replace(a, b)
    return s.encode("ascii", "ignore").decode()


def simplify(pts, tol):
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


def load(d, name):
    return json.load(open(os.path.join(d, name + ".geojson")))["features"]


def polygons(features):
    for ft in features:
        g = ft["geometry"]
        if not g:
            continue
        for poly in ([g["coordinates"]] if g["type"] == "Polygon" else g["coordinates"]):
            yield poly


def lines(features):
    for ft in features:
        g = ft["geometry"]
        if not g:
            continue
        for ln in ([g["coordinates"]] if g["type"] == "LineString" else g["coordinates"]):
            yield [(lat, lon) for lon, lat in ln]


def inside(p):
    return LBOX[0] <= p[0] <= LBOX[2] and LBOX[1] <= p[1] <= LBOX[3]


def clip(ln):
    """Teilt eine Linie in Stuecke innerhalb von LBOX (mit je einem Punkt Ueberstand)."""
    out, cur = [], []
    for i, p in enumerate(ln):
        if inside(p):
            if not cur and i > 0:
                cur.append(ln[i - 1])
            cur.append(p)
        elif cur:
            cur.append(p)
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return [c for c in out if len(c) >= 2]


def enc(lat, lon):
    return round((lat - 40.0) * 1000), round(lon * 1000)


def pack_lines(lns):
    out = bytearray(struct.pack("<H", len(lns)))
    for pts in lns:
        e = [enc(*p) for p in pts]
        la = [p[0] for p in e]
        lo = [p[1] for p in e]
        out += struct.pack("<H4h", len(e), min(la), min(lo), max(la), max(lo))
        for p in e:
            out += struct.pack("<hh", *p)
    return out


def raster(d):
    big = Image.new("L", (RW * SS, RH * SS), 0)
    dr = ImageDraw.Draw(big)
    lat_bot, lon_right = LAT_TOP - RH * DLAT, LON_LEFT + RW * DLON

    def draw(features, fill):
        for poly in polygons(features):
            lons = [c[0] for c in poly[0]]
            lats = [c[1] for c in poly[0]]
            if max(lons) < LON_LEFT or min(lons) > lon_right or max(lats) < lat_bot or min(lats) > LAT_TOP:
                continue
            for k, ring in enumerate(poly):
                pts = [((lon - LON_LEFT) / DLON * SS, (LAT_TOP - lat) / DLAT * SS) for lon, lat in ring]
                dr.polygon(pts, fill=fill if k == 0 else 255 - fill)

    draw(load(d, "ne_10m_land"), 255)
    draw(load(d, "ne_10m_lakes"), 0)
    draw(load(d, "ne_10m_lakes_europe"), 0)
    cov = big.resize((RW, RH), Image.BOX)
    return bytes((v * 15 + 127) // 255 for v in cov.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--preview")
    a = ap.parse_args()

    idx = raster(a.dir)
    ras = bytes(idx[i] | (idx[i + 1] << 4) for i in range(0, len(idx), 2))

    borders = [simplify(c, TOL) for ln in lines(load(a.dir, "ne_10m_admin_0_boundary_lines_land")) for c in clip(ln)]
    states = [simplify(c, TOL) for ln in lines(f for f in load(a.dir, "ne_10m_admin_1_states_provinces_lines")
                                            if f["properties"].get("ADM0_A3") == "DEU") for c in clip(ln)]

    cities = bytearray()
    cs = [f["properties"] for f in load(a.dir, "ne_10m_populated_places_simple")
          if f["properties"].get("adm0_a3") == "DEU"]
    cs.sort(key=lambda p: -p["pop_max"])
    cities += struct.pack("<H", len(cs))
    for p in cs:
        rank = 0 if p["pop_max"] >= 500000 else (1 if p["pop_max"] >= 150000 else 2)
        cities += struct.pack("<hhB", *enc(p["latitude"], p["longitude"]), rank) + ascii_de(p["name"]).encode() + b"\0"

    hdr_len = 4 + 16 + 4 + 16
    b_lin = pack_lines(borders)
    s_lin = pack_lines(states)
    off_r = hdr_len
    off_b = off_r + len(ras)
    off_s = off_b + len(b_lin)
    off_c = off_s + len(s_lin)
    out = bytearray(b"DET1")
    out += struct.pack("<4i", round(LAT_TOP * 1e5), round(LON_LEFT * 1e5), round(DLAT * 1e5), round(DLON * 1e5))
    out += struct.pack("<HH", RW, RH)
    out += struct.pack("<4I", off_r, off_b, off_s, off_c)
    out += ras + b_lin + s_lin + cities
    path = os.path.join(ROOT, "assets", "detail.bin")
    open(path, "wb").write(out)
    print(f"{path}: Raster {RW}x{RH} ({len(ras)} B), Grenzen {len(borders)} Linien/{sum(map(len, borders))} Punkte, "
          f"Bundeslaender {len(states)}/{sum(map(len, states))}, {len(cs)} Staedte, gesamt {len(out)} Bytes")
    if a.preview:
        Image.frombytes("L", (RW, RH), bytes(v * 17 for v in idx)).save(os.path.join(a.preview, "detail.png"))


if __name__ == "__main__":
    main()
