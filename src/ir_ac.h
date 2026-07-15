#pragma once

#include <Arduino.h>

// Controls the Daikin AC over IR (protocol DAIKIN2, remote ARC478A33) using the
// StickS3 IR transmitter on GPIO 46. IR is one-way; we track only what we last
// commanded. All sending happens in the main loop (blocking), never the async
// task.

void ir_ac_init();

// Applies a pending AcCommand from g_state and transmits it. Call from loop().
void ir_ac_service();

// String <-> Daikin enum helpers, shared by the web + MCP layers.
// Modes: auto, cool, heat, dry, fan. Fans: auto, quiet, low, medium, high.
bool ir_ac_mode_from_string(const char* s, uint8_t* out);
const char* ir_ac_mode_to_string(uint8_t mode);
bool ir_ac_fan_from_string(const char* s, uint8_t* out);
const char* ir_ac_fan_to_string(uint8_t fan);

// Clamps to the AC's supported range (16..30 C practical subset).
uint8_t ir_ac_clamp_temp(int temp);
