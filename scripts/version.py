# PlatformIO pre-script: setzt FW_VERSION aus "git describe" (z.B. v0.2.0-3-gabc1234-dirty)
import subprocess
Import("env")

def git_version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unbekannt"

ver = git_version()
print("Firmware-Version:", ver)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(ver))])
