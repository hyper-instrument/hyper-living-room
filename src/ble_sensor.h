#pragma once

// Starts a continuous passive BLE scan that decodes Xiaomi MiBeacon
// advertisements (service data UUID 0xFE95) from a LYWSDCGQ round
// temperature/humidity sensor and publishes readings into g_state.
void ble_sensor_start();

// Temporarily stop / restart scanning. Used to quiet the radio while the IR
// transmitter bit-bangs a frame (BLE interrupts would corrupt the timing).
void ble_sensor_pause();
void ble_sensor_resume();

// Call periodically from loop(): restarts the scan if it silently stalled.
void ble_sensor_watchdog();
