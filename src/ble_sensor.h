#pragma once

// Starts a continuous passive BLE scan that decodes Xiaomi MiBeacon
// advertisements (service data UUID 0xFE95) from a LYWSDCGQ round
// temperature/humidity sensor and publishes readings into g_state.
void ble_sensor_start();
