# PlatformIO pre-script: loads KEY=VALUE pairs from .env (project root) and
# injects them as string macro definitions, e.g. WIFI_SSID -> -D WIFI_SSID="...".
# Fails the build with a clear message if .env is missing.

import os

Import("env")  # noqa: F821 (provided by SCons/PlatformIO)

ENV_KEYS = [
    "WIFI_SSID",
    "WIFI_PASS",
    "TAPO_IP",
    "TAPO_USER",
    "TAPO_PASS",
    "SENSOR_MAC",
    "APP_HOSTNAME",
    "WG_PRIVATE_KEY",
    "WG_LOCAL_IP",
    "WG_PEER_PUBLIC_KEY",
    "WG_ENDPOINT",
    "WG_PORT",
]

env_path = os.path.join(env["PROJECT_DIR"], ".env")
if not os.path.isfile(env_path):
    raise SystemExit(
        "\n.env not found. Copy .env.example to .env and fill in your credentials.\n"
    )

values = {}
with open(env_path) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        values[key.strip()] = value.strip()

missing = [k for k in ENV_KEYS if k not in values]
if missing:
    raise SystemExit(f"\n.env is missing keys: {', '.join(missing)} (see .env.example)\n")

for key in ENV_KEYS:
    escaped = values[key].replace("\\", "\\\\").replace('"', '\\"')
    env.Append(CPPDEFINES=[(key, f'\\"{escaped}\\"')])
