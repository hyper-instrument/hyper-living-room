#pragma once

// Credentials (WIFI_SSID, WIFI_PASS, TAPO_IP, TAPO_USER, TAPO_PASS,
// SENSOR_MAC, APP_HOSTNAME) are injected at build time from the gitignored
// .env file by scripts/load_env.py. Copy .env.example to .env to configure.
#ifndef WIFI_SSID
#error "Credentials not defined. Copy .env.example to .env and fill in your values."
#endif

// Sensor reading considered stale after this long without a BLE update.
#define SENSOR_STALE_MS (5 * 60 * 1000UL)

// How often to poll the Tapo plug for state/energy.
#define TAPO_POLL_MS 10000UL

// Retry interval for the Tapo KLAP handshake while disconnected.
#define TAPO_RECONNECT_MS 5000UL
