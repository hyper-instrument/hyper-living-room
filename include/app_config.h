#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.h.example to include/secrets.h and fill in your credentials"
#endif

// Sensor reading considered stale after this long without a BLE update.
#define SENSOR_STALE_MS (5 * 60 * 1000UL)

// How often to poll the Tapo plug for state/energy.
#define TAPO_POLL_MS 10000UL

// Retry interval for the Tapo KLAP handshake while disconnected.
#define TAPO_RECONNECT_MS 5000UL
