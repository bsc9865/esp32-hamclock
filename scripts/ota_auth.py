# PlatformIO post-script: Passwort fuer Netzwerk-Updates (espota) aus include/secrets.h
# (OTA_PASSWORD) oder der Umgebungsvariable HAMCLOCK_OTA_PASSWORD uebergeben.
# Aufruf: pio run -t upload --upload-port esp32-hamclock.local
import os
import re
Import("env")

port = env.get("UPLOAD_PORT", "") or ""
if re.match(r"\"?((([0-9]{1,3}\.){3}[0-9]{1,3})|[^\\/]+\.local)\"?$", port):
    pw = os.environ.get("HAMCLOCK_OTA_PASSWORD", "")
    secrets = os.path.join(env.subst("$PROJECT_DIR"), "include", "secrets.h")
    if not pw and os.path.exists(secrets):
        m = re.search(r'^\s*#define\s+OTA_PASSWORD\s+"([^"]*)"', open(secrets).read(), re.M)
        if m:
            pw = m.group(1)
    if pw:
        env.Append(UPLOADERFLAGS=["--auth=" + pw])
    else:
        print("Hinweis: kein OTA_PASSWORD in include/secrets.h gefunden")
