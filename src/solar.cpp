#include "solar.h"
#include <Arduino.h>
#include <math.h>
#include "net.h"
#include "config.h"

const char *const HF_BAND_NAMES[4] = {"80-40m", "30-20m", "17-15m", "12-10m"};

// Inhalt zwischen <tag> und </tag> (ohne Attribute im Start-Tag)
static String xml_tag(const String &s, const char *tag) {
  String open = String("<") + tag + ">";
  String close = String("</") + tag + ">";
  int a = s.indexOf(open);
  if (a < 0) return "";
  a += open.length();
  int b = s.indexOf(close, a);
  if (b < 0) return "";
  String v = s.substring(a, b);
  v.trim();
  return v;
}

// Inhalt eines Elements, dessen Start-Tag den Attribut-Text attr enthaelt,
// z.B. attr = "name=\"80m-40m\" time=\"day\""
static String xml_attr_elem(const String &s, const char *attr) {
  int a = s.indexOf(attr);
  if (a < 0) return "";
  a = s.indexOf('>', a);
  if (a < 0) return "";
  int b = s.indexOf('<', a + 1);
  if (b < 0) return "";
  String v = s.substring(a + 1, b);
  v.trim();
  return v;
}

static int to_int(const String &v) { return v.length() ? v.toInt() : -1; }
static float to_float(const String &v) {
  if (!v.length() || !(isdigit((unsigned char)v[0]) || v[0] == '-' || v[0] == '.')) return NAN;
  return v.toFloat();
}

static Cond to_cond(const String &v) {
  if (v.equalsIgnoreCase("Good")) return COND_GOOD;
  if (v.equalsIgnoreCase("Fair")) return COND_FAIR;
  if (v.equalsIgnoreCase("Poor")) return COND_POOR;
  return COND_UNKNOWN;
}

static void copy(char *dst, size_t n, const String &v) {
  strncpy(dst, v.c_str(), n - 1);
  dst[n - 1] = 0;
}

bool solar_fetch(SolarData &d) {
  String x;
  if (!http_get(URL_HAMQSL_SOLAR, x)) return false;
  if (x.indexOf("<solardata>") < 0) {
    Serial.println("hamqsl: unerwartetes Format");
    return false;
  }
  SolarData r;
  copy(r.updated, sizeof r.updated, xml_tag(x, "updated"));
  r.sfi = to_int(xml_tag(x, "solarflux"));
  r.ssn = to_int(xml_tag(x, "sunspots"));
  r.a = to_int(xml_tag(x, "aindex"));
  r.k = to_int(xml_tag(x, "kindex"));
  copy(r.xray, sizeof r.xray, xml_tag(x, "xray"));
  r.solarwind = to_float(xml_tag(x, "solarwind"));
  r.bz = to_float(xml_tag(x, "magneticfield"));
  r.protonflux = to_float(xml_tag(x, "protonflux"));
  r.muf = to_float(xml_tag(x, "muf"));
  copy(r.geomag, sizeof r.geomag, xml_tag(x, "geomagfield"));
  copy(r.noise, sizeof r.noise, xml_tag(x, "signalnoise"));

  static const char *bands[4] = {"80m-40m", "30m-20m", "17m-15m", "12m-10m"};
  for (int i = 0; i < 4; i++) {
    char q[48];
    snprintf(q, sizeof q, "name=\"%s\" time=\"day\"", bands[i]);
    r.hf[i][0] = to_cond(xml_attr_elem(x, q));
    snprintf(q, sizeof q, "name=\"%s\" time=\"night\"", bands[i]);
    r.hf[i][1] = to_cond(xml_attr_elem(x, q));
  }
  copy(r.vhf_aurora, sizeof r.vhf_aurora,
       xml_attr_elem(x, "name=\"vhf-aurora\" location=\"northern_hemi\""));
  copy(r.es_eu_6m, sizeof r.es_eu_6m, xml_attr_elem(x, "name=\"E-Skip\" location=\"europe_6m\""));
  copy(r.es_eu_4m, sizeof r.es_eu_4m, xml_attr_elem(x, "name=\"E-Skip\" location=\"europe_4m\""));
  copy(r.es_eu_2m, sizeof r.es_eu_2m, xml_attr_elem(x, "name=\"E-Skip\" location=\"europe\""));

  r.valid = r.sfi >= 0;
  r.fetched = time(nullptr);
  if (r.valid) d = r;
  Serial.printf("Solar: SFI %d SSN %d A %d K %d X %s\n", r.sfi, r.ssn, r.a, r.k, r.xray);
  return r.valid;
}
