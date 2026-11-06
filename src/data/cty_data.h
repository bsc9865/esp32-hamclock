// Automatisch erzeugt von scripts/gen_cty.py - nicht von Hand bearbeiten
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
