#!/usr/bin/env python3
"""Erzeugt assets/map.bin: schlichte Karte (Land/Wasser) aus Natural Earth (gemeinfrei).

Quelle: https://www.naturalearthdata.com/ (ne_50m_land, ne_50m_lakes), z.B.
  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_land.geojson
  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_lakes.geojson

Aufruf (benoetigt Pillow):
  python3 scripts/gen_map_simple.py ne_50m_land.geojson ne_50m_lakes.geojson [--preview DIR]

Die Kuesten werden 4-fach ueberabgetastet gezeichnet; der Index pro Pixel ist der Landanteil
(0 = Wasser ... 15 = Land), die Paletten blenden linear zwischen Wasser- und Landfarbe.
Tag und Nacht teilen sich die Pixel und unterscheiden sich nur in der Palette.

Format (little endian):
  "MAP4", 16 x uint16 Tag-Palette (RGB565), 16 x uint16 Nacht-Palette,
  danach W*H/2 Bytes, 2 Pixel pro Byte (gerades x im unteren Nibble), zeilenweise.
  Groesse W x H steht in src/maps/maps.h (MAP_SRC_W / MAP_SRC_H), equirektangulaer.
"""
import argparse
import json
import os
import struct
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, SS = 1600, 800, 4

# Gedeckte Farben, damit die Ebenen (Aurora, D-RAP, MUF, Zonen, Grenzen) gut hervortreten
DAY_WATER, DAY_LAND = (28, 48, 74), (92, 104, 86)
NIGHT_WATER, NIGHT_LAND = (8, 13, 24), (34, 38, 44)


def polygons(path):
    for ft in json.load(open(path))["features"]:
        g = ft["geometry"]
        polys = [g["coordinates"]] if g["type"] == "Polygon" else g["coordinates"]
        for poly in polys:
            yield poly   # [aussen, loch1, ...]


def draw(img, path, fill):
    d = ImageDraw.Draw(img)
    w, h = img.size
    for poly in polygons(path):
        for k, ring in enumerate(poly):
            pts = [((lon + 180.0) / 360.0 * w, (90.0 - lat) / 180.0 * h) for lon, lat in ring]
            d.polygon(pts, fill=fill if k == 0 else 255 - fill)


def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def palette(water, land):
    return [tuple(round(water[j] + (land[j] - water[j]) * i / 15) for j in range(3)) for i in range(16)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("land")
    ap.add_argument("lakes")
    ap.add_argument("--preview", help="Verzeichnis fuer PNG-Vorschauen")
    a = ap.parse_args()

    big = Image.new("L", (W * SS, H * SS), 0)
    draw(big, a.land, 255)
    draw(big, a.lakes, 0)
    cov = big.resize((W, H), Image.BOX)
    idx = bytes((v * 15 + 127) // 255 for v in cov.tobytes())

    pd, pn = palette(DAY_WATER, DAY_LAND), palette(NIGHT_WATER, NIGHT_LAND)
    out = bytearray(b"MAP4")
    for c in pd + pn:
        out += struct.pack("<H", rgb565(c))
    out += bytes(idx[i] | (idx[i + 1] << 4) for i in range(0, len(idx), 2))
    path = os.path.join(ROOT, "assets", "map.bin")
    open(path, "wb").write(out)
    print(f"{path}: {W}x{H}, {len(out)} Bytes")

    if a.preview:
        for name, pal in (("day", pd), ("night", pn)):
            im = Image.frombytes("P", (W, H), idx)
            im.putpalette([v for c in pal for v in c])
            im.convert("RGB").save(os.path.join(a.preview, f"simple_{name}.png"))


if __name__ == "__main__":
    main()
