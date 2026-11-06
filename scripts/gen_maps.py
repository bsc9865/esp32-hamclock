#!/usr/bin/env python3
"""Erzeugt die Kartenbilder fuer die Firmware als RGB565-C-Arrays.

Quellen (gemeinfrei, NASA), z.B. von Wikimedia Commons:
  Tag:   https://commons.wikimedia.org/wiki/File:Whole_world_-_land_and_oceans.jpg  (Blue Marble)
  Nacht: https://commons.wikimedia.org/wiki/File:The_earth_at_night.jpg             (Black Marble)

Aufruf (benoetigt Pillow):
  python3 scripts/gen_maps.py tag.jpg nacht.jpg [--width 560]

Ausgabe: src/maps/map_day.c, src/maps/map_night.c, src/maps/maps.h
Die Bilder muessen equirektangulaer sein (2:1, -180..180 Grad Laenge, 90..-90 Grad Breite).
"""
import argparse
import os
from PIL import Image, ImageEnhance

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "maps")


def to_rgb565(img):
    px = img.convert("RGB").load()
    w, h = img.size
    out = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            out.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    return out


def write_c(name, data, w, h):
    path = os.path.join(OUT, f"{name}.c")
    with open(path, "w") as f:
        f.write("// Automatisch erzeugt von scripts/gen_maps.py - nicht von Hand bearbeiten\n")
        f.write('#include "maps.h"\n\n')
        f.write(f"const uint16_t {name}[{w * h}] = {{\n")
        for i in range(0, len(data), 16):
            f.write("  " + ",".join(f"0x{v:04X}" for v in data[i:i + 16]) + ",\n")
        f.write("};\n")
    print("geschrieben:", path, f"({w}x{h})")


def load(path, w, h, brightness=1.0, contrast=1.0, saturation=1.0):
    img = Image.open(path).convert("RGB").resize((w, h), Image.LANCZOS)
    if brightness != 1.0:
        img = ImageEnhance.Brightness(img).enhance(brightness)
    if contrast != 1.0:
        img = ImageEnhance.Contrast(img).enhance(contrast)
    if saturation != 1.0:
        img = ImageEnhance.Color(img).enhance(saturation)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("day")
    ap.add_argument("night")
    ap.add_argument("--width", type=int, default=560)
    ap.add_argument("--preview", help="Verzeichnis fuer PNG-Vorschauen")
    a = ap.parse_args()
    w, h = a.width, a.width // 2

    os.makedirs(OUT, exist_ok=True)
    day = load(a.day, w, h, brightness=1.05, saturation=1.15)
    night = load(a.night, w, h, brightness=1.25, contrast=1.1)
    if a.preview:
        day.save(os.path.join(a.preview, "day.png"))
        night.save(os.path.join(a.preview, "night.png"))

    write_c("map_day", to_rgb565(day), w, h)
    write_c("map_night", to_rgb565(night), w, h)
    with open(os.path.join(OUT, "maps.h"), "w") as f:
        f.write("// Automatisch erzeugt von scripts/gen_maps.py\n#pragma once\n#include <stdint.h>\n\n")
        f.write(f"#define MAP_W {w}\n#define MAP_H {h}\n\n")
        f.write('#ifdef __cplusplus\nextern "C" {\n#endif\n')
        f.write("extern const uint16_t map_day[MAP_W * MAP_H];\n")
        f.write("extern const uint16_t map_night[MAP_W * MAP_H];\n")
        f.write("#ifdef __cplusplus\n}\n#endif\n")


if __name__ == "__main__":
    main()
