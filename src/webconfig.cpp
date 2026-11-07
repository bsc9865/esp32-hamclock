#include "webconfig.h"
#include <Arduino.h>
#include <WebServer.h>
#include "config.h"
#include "settings.h"
#include "spots.h"
#include "watch.h"
#include "dxcluster.h"
#include "cty.h"

static WebServer *srv;

static String esc(const char *s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += *s;
    }
  }
  return o;
}

static bool authorized() {
  if (!g_settings.ota_pw[0]) return true;   // ohne Passwort offen (Hinweis auf der Seite)
  if (srv->authenticate("admin", g_settings.ota_pw)) return true;
  srv->requestAuthentication(BASIC_AUTH, "ESP32-HamClock", "Kennwort = OTA-Passwort");
  return false;
}

// Eintrag an die Watchlist anhaengen (falls noch nicht vorhanden)
static bool watch_add(Settings &s, const String &entry) {
  String list = s.watch;
  String e = entry;
  e.trim();
  if (!e.length()) return false;
  // schon enthalten? (Eintraege durch Komma/Zeilenumbruch getrennt, ohne Gross-/Kleinschreibung)
  String low = "," + list + ",";
  low.replace("\n", ",");
  low.replace(" ", "");
  low.toUpperCase();
  String key = e;
  key.replace(" ", "");
  key.toUpperCase();
  if (low.indexOf("," + key + ",") >= 0) return false;
  if (list.length() && !list.endsWith(",") && !list.endsWith("\n")) list += ", ";
  list += e;
  if (list.length() >= sizeof s.watch) return false;
  strncpy(s.watch, list.c_str(), sizeof s.watch - 1);
  s.watch[sizeof s.watch - 1] = 0;
  return true;
}

static String spots_table() {
  static Spot *sp = (Spot *)heap_caps_malloc(DX_MAX_SPOTS * sizeof(Spot), MALLOC_CAP_SPIRAM);
  int n = dx_snapshot(sp, 60, 0xFFFF);
  String h;
  h.reserve(n * 330 + 600);
  h += F("<fieldset id=spots><legend>Aktuelle DX-Spots</legend>");
  if (!n) {
    h += F("<p class=hint>Noch keine Spots empfangen.</p></fieldset>");
    return h;
  }
  h += F("<table><tr><th>UTC</th><th>kHz</th><th>Rufzeichen</th><th>Land</th><th>Mode</th><th>de</th><th></th></tr>");
  for (int i = 0; i < n; i++) {
    const Spot &x = sp[i];
    const CtyEntity *ent = x.ent >= 0 ? &cty_entities[x.ent] : nullptr;
    bool hit = g_settings.watch[0] && watch_match(g_settings.watch, x.dx, ent);
    struct tm u;
    gmtime_r(&x.t, &u);
    char t[8], f[16];
    snprintf(t, sizeof t, "%02d%02d", u.tm_hour, u.tm_min);
    snprintf(f, sizeof f, "%.1f", x.khz);
    h += hit ? "<tr class=hit>" : "<tr>";
    h += "<td>" + String(t) + "</td><td>" + f + "</td><td><b>" + esc(x.dx) + "</b></td><td>" +
         (ent ? esc(ent->name) : String("?")) + "</td><td>" + esc(x.mode) + "</td><td class=hint>" + esc(x.spotter) + "</td><td>";
    h += "<form method=post action=/watch class=inl><input type=hidden name=e value=\"" + esc(x.dx) +
         "\"><button class=sm>+ Ruf</button></form>";
    if (ent)
      h += "<form method=post action=/watch class=inl><input type=hidden name=e value=\"@" + esc(ent->prefix) +
           "\"><button class=sm title=\"" + esc(ent->name) + "\">+ Land</button></form>";
    h += "</td></tr>";
  }
  h += F("</table><p class=hint>Neueste zuerst, alle B&auml;nder und Modes. Watchlist-Treffer sind hervorgehoben.</p></fieldset>");
  return h;
}

static String checkbox(const char *name, const char *label, bool on) {
  return String("<label class=cb><input type=checkbox name=") + name + (on ? " checked" : "") + "> " + label + "</label>";
}

static void page(const char *msg = nullptr, bool ok = true) {
  const Settings &s = g_settings;
  String h;
  h.reserve(9000);
  h += F("<!doctype html><html lang=de><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
         "<title>ESP32-HamClock Einstellungen</title><style>"
         "body{font-family:system-ui,sans-serif;background:#0a0e14;color:#e6edf3;margin:16px;max-width:760px}"
         "h1{color:#38bdf8;font-size:22px}fieldset{border:1px solid #263142;border-radius:6px;margin:14px 0;padding:10px 14px}"
         "legend{color:#38bdf8;padding:0 6px}label{display:block;margin:6px 0}"
         "input[type=text],input[type=password],input[type=number],textarea{width:100%;box-sizing:border-box;background:#131a24;"
         "color:#e6edf3;border:1px solid #263142;border-radius:4px;padding:7px;font-size:15px}"
         "textarea{height:90px;font-family:ui-monospace,monospace}.cb{display:inline-block;margin:4px 14px 4px 0}"
         ".hint{color:#7d8b9c;font-size:13px}button{background:#15803d;color:#fff;border:0;border-radius:6px;padding:10px 22px;"
         "font-size:16px;margin-top:8px}.msg{padding:10px;border-radius:6px;margin:10px 0}.ok{background:#14532d}.bad{background:#7f1d1d}"
         "a{color:#38bdf8}table{border-collapse:collapse;width:100%;font-size:14px}td,th{padding:3px 6px;border-bottom:1px solid #263142;"
         "text-align:left;white-space:nowrap}th{color:#7d8b9c;font-weight:500}tr.hit td{background:#4a1d4f}"
         ".inl{display:inline;margin:0}button.sm{font-size:12px;padding:3px 8px;margin:0 2px;background:#1e40af}"
         "#spots{overflow-x:auto}</style></head><body>");
  h += "<h1>Einstellungen " + esc(s.call) + "</h1>";
  if (msg) h += String("<div class='msg ") + (ok ? "ok" : "bad") + "'>" + msg + "</div>";
  if (!s.ota_pw[0])
    h += F("<div class='msg bad'>Diese Seite ist nicht gesch&uuml;tzt. Setze ein OTA-Passwort, dann wird es als Kennwort verlangt (Benutzer &bdquo;admin&ldquo;).</div>");
  h += F("<form method=post action=/config>");

  h += F("<fieldset><legend>DX-Watchlist</legend><textarea name=watch>");
  h += esc(s.watch);
  h += F("</textarea><div class=hint>Getrennt durch Komma oder Zeilenumbruch: <b>3Y0J</b> = genau dieses Rufzeichen, "
         "<b>VP8*</b> = Rufzeichen-Anfang, <b>@Bouvet</b> oder <b>@3Y/b</b> = DXCC-Gebiet (Name oder Hauptpr&auml;fix). "
         "Treffer werden immer angezeigt und gemeldet, auch wenn Band oder Mode ausgefiltert sind.</div></fieldset>");

  h += F("<fieldset><legend>B&auml;nder</legend>");
  for (int b = 0; b < B_OTHER; b++) {
    char n[6];
    snprintf(n, sizeof n, "b%d", b);
    String lbl = String(BAND_NAMES[b]) + (b == B_70CM ? "" : " m");
    h += checkbox(n, lbl.c_str(), s.band_mask & (1u << b));
  }
  h += F("</fieldset><fieldset><legend>Modes</legend>");
  h += checkbox("m1", "CW", s.mode_mask & MODE_CW);
  h += checkbox("m2", "SSB/AM/FM", s.mode_mask & MODE_SSB);
  h += checkbox("m4", "Digi (FT8, RTTY, ...)", s.mode_mask & MODE_DIGI);
  h += checkbox("m8", "sonstige", s.mode_mask & MODE_OTHER);
  h += F("</fieldset><fieldset><legend>Kartenebenen</legend>");
  h += checkbox("l1", "Aurora", s.layers & LAYER_AURORA);
  h += checkbox("l2", "D-RAP", s.layers & LAYER_DRAP);
  h += checkbox("l4", "MUF", s.layers & LAYER_MUF);
  h += checkbox("l8", "APRS", s.layers & LAYER_APRS);
  h += checkbox("l16", "L&auml;ndergrenzen", !(s.layers & LAYER_HIDE_BORDERS));
  h += checkbox("l32", "CQ-Zonen", s.layers & LAYER_CQ);
  h += checkbox("l64", "ITU-Zonen", s.layers & LAYER_ITU);
  h += "<label>Aurora-Deckkraft (20&ndash;200 %)<input type=number name=aurpct min=20 max=200 value=" + String(s.aurora_pct) + "></label>";
  h += F("</fieldset>");

  h += F("<fieldset><legend>Station (Neustart)</legend>");
  h += "<label>Rufzeichen<input type=text name=call maxlength=12 value=\"" + esc(s.call) + "\"></label>";
  h += "<label>Locator<input type=text name=loc maxlength=6 value=\"" + esc(s.loc) + "\"></label>";
  h += "<label>DX-Cluster (host:port)<input type=text name=cluster maxlength=47 value=\"" + esc(s.cluster) + "\"></label>";
  h += F("</fieldset><fieldset><legend>WLAN und Updates (Neustart)</legend>");
  h += "<label>WLAN-Name<input type=text name=ssid maxlength=32 value=\"" + esc(s.ssid) + "\"></label>";
  h += F("<label>WLAN-Passwort<input type=password name=pass maxlength=64 placeholder='unver&auml;ndert lassen'></label>"
         "<label>OTA-Passwort (auch Kennwort dieser Seite)<input type=password name=otapw maxlength=32 placeholder='unver&auml;ndert lassen'></label>"
         "<div class=hint>Leere Passwortfelder lassen das bisherige Passwort unver&auml;ndert.</div></fieldset>");

  h += F("<button type=submit>Speichern</button></form>");
  h += spots_table();
  h += F("<p class=hint><a href=/>Status</a> &middot; Firmware " FW_VERSION "</p></body></html>");
  srv->send(200, "text/html; charset=utf-8", h);
}

static void handle_post() {
  Settings s = g_settings;
  bool restart = false;
  auto take = [&](const char *name, char *dst, size_t n, bool needs_restart) {
    if (!srv->hasArg(name)) return;
    String v = srv->arg(name);
    v.trim();
    if (strcmp(v.c_str(), dst) != 0) {
      strncpy(dst, v.c_str(), n - 1);
      dst[n - 1] = 0;
      if (needs_restart) restart = true;
    }
  };
  // Pruefen
  String call = srv->arg("call");
  call.trim();
  call.toUpperCase();
  char loc[7];
  if (call.length() < 3) return page("Rufzeichen ung&uuml;ltig", false);
  if (!settings_normalize_locator(srv->arg("loc").c_str(), loc)) return page("Locator ung&uuml;ltig (z.B. JO50uh)", false);
  String cl = srv->arg("cluster");
  cl.trim();
  if (!cl.length() || cl.indexOf('.') < 0) return page("DX-Cluster ung&uuml;ltig (host:port)", false);
  if (!srv->arg("ssid").length()) return page("WLAN-Name fehlt", false);

  // Uebernehmen
  if (strcmp(call.c_str(), s.call) != 0) { strncpy(s.call, call.c_str(), sizeof s.call - 1); restart = true; }
  if (strcmp(loc, s.loc) != 0) { strcpy(s.loc, loc); restart = true; }
  take("cluster", s.cluster, sizeof s.cluster, true);
  take("ssid", s.ssid, sizeof s.ssid, true);
  if (srv->arg("pass").length()) take("pass", s.pass, sizeof s.pass, true);
  if (srv->arg("otapw").length()) take("otapw", s.ota_pw, sizeof s.ota_pw, true);
  take("watch", s.watch, sizeof s.watch, false);
  uint16_t bands = 0;
  for (int b = 0; b < B_OTHER; b++) {
    char n[6];
    snprintf(n, sizeof n, "b%d", b);
    if (srv->hasArg(n)) bands |= 1u << b;
  }
  s.band_mask = bands;
  s.mode_mask = (srv->hasArg("m1") ? MODE_CW : 0) | (srv->hasArg("m2") ? MODE_SSB : 0) |
                (srv->hasArg("m4") ? MODE_DIGI : 0) | (srv->hasArg("m8") ? MODE_OTHER : 0);
  s.layers = (srv->hasArg("l1") ? LAYER_AURORA : 0) | (srv->hasArg("l2") ? LAYER_DRAP : 0) |
             (srv->hasArg("l4") ? LAYER_MUF : 0) | (srv->hasArg("l8") ? LAYER_APRS : 0) |
             (srv->hasArg("l16") ? 0 : LAYER_HIDE_BORDERS) | (srv->hasArg("l32") ? LAYER_CQ : 0) |
             (srv->hasArg("l64") ? LAYER_ITU : 0);
  int pct = srv->arg("aurpct").toInt();
  if (pct >= 20 && pct <= 200) s.aurora_pct = (uint8_t)pct;

  settings_request(s, restart);
  if (restart) {
    srv->send(200, "text/html; charset=utf-8",
              "<!doctype html><meta charset=utf-8><meta http-equiv=refresh content='20;url=/config'>"
              "<body style='font-family:system-ui;background:#0a0e14;color:#e6edf3;margin:16px'>"
              "<h2>Gespeichert &ndash; das Ger&auml;t startet neu.</h2><p>Die Seite l&auml;dt in 20 s neu.</p></body>");
  } else {
    delay(150);   // Uebernahme in der Hauptschleife abwarten
    page("Gespeichert.");
  }
}

static void handle_watch_add() {
  Settings s = g_settings;
  String e = srv->arg("e");
  bool added = watch_add(s, e);
  if (added) {
    settings_request(s, false);
    delay(150);   // Uebernahme in der Hauptschleife abwarten
  }
  String msg = added ? "&bdquo;" + esc(e.c_str()) + "&ldquo; zur Watchlist hinzugef&uuml;gt."
                     : "&bdquo;" + esc(e.c_str()) + "&ldquo; ist schon in der Watchlist (oder sie ist voll).";
  page(msg.c_str(), added);
}

void webconfig_register(WebServer &server) {
  srv = &server;
  server.on("/watch", HTTP_POST, [] { if (authorized()) handle_watch_add(); });
  server.on("/config", HTTP_GET, [] { if (authorized()) page(); });
  server.on("/config", HTTP_POST, [] { if (authorized()) handle_post(); });
}
