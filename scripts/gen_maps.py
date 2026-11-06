#!/usr/bin/env python3
"""Erzeugt die Kartenbilder fuer die Firmware (256-Farben-Palette, 1 Byte pro Pixel).

Quellen (gemeinfrei, NASA), z.B. von Wikimedia Commons:
  Tag:   https://commons.wikimedia.org/wiki/File:Whole_world_-_land_and_oceans.jpg  (Blue Marble)
  Nacht: https://commons.wikimedia.org/wiki/File:The_earth_at_night.jpg             (Black Marble)

Aufruf (benoetigt Pillow):
  python3 scripts/gen_maps.py tag.jpg nacht.jpg [--width 1600] [--preview DIR]

Ausgabe: assets/map_day.bin, assets/map_night.bin
Format:  256 x uint16 Palette (RGB565, little endian), danach W*H Palettenindizes (zeilenweise).
Die Bilder muessen equirektangulaer sein (2:1, -180..180 Grad Laenge, 90..-90 Grad Breite).
Die Groesse steht in src/maps/maps.h (MAP_SRC_W / MAP_SRC_H).
"""
import argparse
import os
import struct
from PIL import Image, ImageEnhance

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets")


def load(path, w, h, brightness=1.0, contrast=1.0, saturation=1.0):
    img = Image.open(path).convert("RGB").resize((w, h), Image.LANCZOS)
    if brightness != 1.0:
        img = ImageEnhance.Brightness(img).enhance(brightness)
    if contrast != 1.0:
        img = ImageEnhance.Contrast(img).enhance(contrast)
    if saturation != 1.0:
        img = ImageEnhance.Color(img).enhance(saturation)
    return img


def write_bin(name, img):
    q = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:256 * 3]
    pal += [0] * (256 * 3 - len(pal))
    with open(os.path.join(OUT, f"{name}.bin"), "wb") as f:
        for i in range(256):
            r, g, b = pal[3 * i:3 * i + 3]
            f.write(struct.pack("<H", ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))
        f.write(q.tobytes())
    print(f"geschrieben: assets/{name}.bin ({img.size[0]}x{img.size[1]})")
    return q


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("day")
    ap.add_argument("night")
    ap.add_argument("--width", type=int, default=1600)
    ap.add_argument("--preview", help="Verzeichnis fuer PNG-Vorschauen")
    a = ap.parse_args()
    w, h = a.width, a.width // 2
    os.makedirs(OUT, exist_ok=True)
    day = load(a.day, w, h, brightness=1.05, saturation=1.15)
    night = load(a.night, w, h, brightness=1.25, contrast=1.1)
    qd = write_bin("map_day", day)
    qn = write_bin("map_night", night)
    if a.preview:
        qd.convert("RGB").save(os.path.join(a.preview, "day8.png"))
        qn.convert("RGB").save(os.path.join(a.preview, "night8.png"))


if __name__ == "__main__":
    main()
