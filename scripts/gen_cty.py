#!/usr/bin/env python3
"""Erzeugt aus cty.dat (AD1C, https://www.country-files.com/) die Praefix-Tabellen fuer die Firmware.

Aufruf:  python3 scripts/gen_cty.py cty.dat
Ausgabe: src/data/cty_data.c (+ Header src/data/cty_data.h)

cty.dat-Format pro Entitaet:
  Name: CQ: ITU: Kontinent: Breite: Laenge(West positiv): UTC-Offset: Hauptpraefix:
      praefix1,praefix2(cq)[itu],=RUFZEICHEN,...;
WAE-Entitaeten (Hauptpraefix mit '*') werden fuer DXCC ignoriert.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "data")


def parse(path):
    text = open(path, encoding="latin-1").read()
    entities, prefixes, exact = [], {}, {}
    for block in text.split(";"):
        block = block.strip()
        if not block:
            continue
        head, _, body = block.partition("\n")
        f = [x.strip() for x in head.split(":")]
        if len(f) < 8:
            continue
        name, cq, itu, cont, lat, lon, _tz, prim = f[:8]
        if prim.startswith("*"):
            continue  # WAE-Entitaet
        idx = len(entities)
        entities.append(dict(name=name, cq=int(cq), itu=int(itu), cont=cont,
                             lat=float(lat), lon=-float(lon), prefix=prim))
        for tok in body.replace("\n", "").split(","):
            tok = tok.strip()
            if not tok:
                continue
            m = re.search(r"\((\d+)\)", tok)
            tcq = int(m.group(1)) if m else int(cq)
            core = re.sub(r"[\(\[<\{~].*$", "", tok)
            if core.startswith("="):
                exact[core[1:]] = (idx, tcq)
            else:
                prefixes[core] = (idx, tcq)
    return entities, prefixes, exact


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    entities, prefixes, exact = parse(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "cty_data.h"), "w") as h:
        h.write("""// Automatisch erzeugt von scripts/gen_cty.py - nicht von Hand bearbeiten
#pragma once
#include <stdint.h>

typedef struct {
  const char *name;
  char cont[3];
  uint8_t cq, itu;
  float lat, lon;      // Ost positiv
  const char *prefix;  // Hauptpraefix
} CtyEntity;

typedef struct {
  const char *key;     // Praefix oder exaktes Rufzeichen
  uint16_t ent;
  uint8_t cq;
} CtyKey;

#ifdef __cplusplus
extern "C" {
#endif
extern const CtyEntity cty_entities[];
extern const uint16_t cty_entity_count;
extern const CtyKey cty_prefixes[];   // sortiert (strcmp)
extern const uint16_t cty_prefix_count;
extern const CtyKey cty_exact[];      // sortiert (strcmp)
extern const uint16_t cty_exact_count;
#ifdef __cplusplus
}
#endif
""")
    with open(os.path.join(OUT, "cty_data.c"), "w") as c:
        c.write("// Automatisch erzeugt von scripts/gen_cty.py aus cty.dat (AD1C, country-files.com)\n")
        c.write('#include "cty_data.h"\n\n')
        c.write("const CtyEntity cty_entities[] = {\n")
        for e in entities:
            c.write(f"  {{{c_str(e['name'])}, {c_str(e['cont'])}, {e['cq']}, {e['itu']}, "
                    f"{e['lat']:.2f}f, {e['lon']:.2f}f, {c_str(e['prefix'])}}},\n")
        c.write("};\n")
        c.write(f"const uint16_t cty_entity_count = {len(entities)};\n\n")
        for name, cnt, table in (("cty_prefixes", "cty_prefix_count", prefixes),
                                 ("cty_exact", "cty_exact_count", exact)):
            c.write(f"const CtyKey {name}[] = {{\n")
            for k in sorted(table, key=lambda s: s.encode()):
                ent, cq = table[k]
                c.write(f"  {{{c_str(k)}, {ent}, {cq}}},\n")
            c.write("};\n")
            c.write(f"const uint16_t {cnt} = {len(table)};\n\n")
    print(f"{len(entities)} Entitaeten, {len(prefixes)} Praefixe, {len(exact)} exakte Rufzeichen")


if __name__ == "__main__":
    main()
