#include "ir_ac.h"

#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRsend.h>

#include "ble_sensor.h"
#include "sharp_raw.h"
#include "state.h"

namespace {
constexpr uint16_t kIrLed = 46; // StickS3 IR transmitter

// The Sharp AY-T22DG's 2025 protocol matches no library encoder, so we replay
// waveforms captured from its own remote (sharp_raw.h). Only power on/off is
// implemented for now; on = 冷房 with the remote's memorized settings.
IRsend g_irsend(kIrLed);

// Nominal state for display purposes (IR is one-way; the ON frame carries the
// remote's memorized cool settings).
stdAc::state_t g_st;

// Practical temperature bounds (the protocol allows 10..32, but a room AC
// realistically runs 16..30).
constexpr int kMinTemp = 16;
constexpr int kMaxTemp = 30;

void sendRawCmd(const uint16_t* buf, size_t len) {
    // Quiet the radios' host-side work and stop the scheduler from preempting
    // us mid-frame: delayMicroseconds timing is only accurate if this task
    // keeps the core (short ISRs are fine, task preemption is not).
    ble_sensor_pause();
    delay(20);
    UBaseType_t prev = uxTaskPriorityGet(nullptr);
    vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 1);
    g_irsend.sendRaw(buf, len, 38); // 38 kHz carrier
    vTaskPrioritySet(nullptr, prev);
    ble_sensor_resume();
}

void sendPower(bool on) {
    if (on) sendRawCmd(kSharpRawOn, kSharpRawOnLen);
    else sendRawCmd(kSharpRawOff, kSharpRawOffLen);
}
} // namespace

void ir_ac_init() {
    g_irsend.begin();
    g_st = {};
    g_st.power = false;
    g_st.mode = stdAc::opmode_t::kCool;
    g_st.degrees = 25;
    g_st.fanspeed = stdAc::fanspeed_t::kAuto;
    Serial.printf("IR: Sharp AY-T22DG raw replay on pin %u (power on/off only)\n", kIrLed);
}

// Long-press Button A: send the captured power-ON frame repeatedly for ~8 s
// (aiming aid + camera emission check).
void ir_ac_blast_test() {
    Serial.println("IR: continuous ON blast (~8 s)");
    uint32_t start = millis();
    while (millis() - start < 8000) {
        sendPower(true);
        delay(400);
    }
    AcState s;
    s.known = true;
    s.power = true;
    s.mode = (uint8_t)stdAc::opmode_t::kCool;
    s.temp = 25;
    s.fan = (uint8_t)stdAc::fanspeed_t::kAuto;
    s.updatedMs = millis();
    g_state.setAcState(s);
}

void ir_ac_service() {
    AcCommand c = g_state.takeAcCommand();
    if (!c.pending) return;

    if (!c.has_power) {
        // Only power on/off is captured for the Sharp so far; other fields
        // (temp/mode/fan/swing) have no waveform yet.
        Serial.println("IR: ignored command without power field (unsupported for Sharp yet)");
        return;
    }

    sendPower(c.power);

    AcState s = g_state.acState();
    s.known = true;
    s.power = c.power;
    s.mode = (uint8_t)stdAc::opmode_t::kCool; // ON frame = 冷房 with remote-memory settings
    s.temp = 25;                              // nominal; actual = remote memory
    s.fan = (uint8_t)stdAc::fanspeed_t::kAuto;
    s.updatedMs = millis();
    g_state.setAcState(s);

    Serial.printf("IR: sent Sharp raw power=%d\n", s.power);
}

// The uint8_t mode/fan values passed around the app are stdAc enums.
bool ir_ac_mode_from_string(const char* s, uint8_t* out) {
    if (!s) return false;
    if (!strcasecmp(s, "auto")) *out = (uint8_t)stdAc::opmode_t::kAuto;
    else if (!strcasecmp(s, "cool")) *out = (uint8_t)stdAc::opmode_t::kCool;
    else if (!strcasecmp(s, "heat")) *out = (uint8_t)stdAc::opmode_t::kHeat;
    else if (!strcasecmp(s, "dry")) *out = (uint8_t)stdAc::opmode_t::kDry;
    else if (!strcasecmp(s, "fan")) *out = (uint8_t)stdAc::opmode_t::kFan;
    else return false;
    return true;
}

const char* ir_ac_mode_to_string(uint8_t mode) {
    switch ((stdAc::opmode_t)mode) {
    case stdAc::opmode_t::kAuto: return "auto";
    case stdAc::opmode_t::kCool: return "cool";
    case stdAc::opmode_t::kHeat: return "heat";
    case stdAc::opmode_t::kDry: return "dry";
    case stdAc::opmode_t::kFan: return "fan";
    default: return "unknown";
    }
}

bool ir_ac_fan_from_string(const char* s, uint8_t* out) {
    if (!s) return false;
    if (!strcasecmp(s, "auto")) *out = (uint8_t)stdAc::fanspeed_t::kAuto;
    else if (!strcasecmp(s, "quiet")) *out = (uint8_t)stdAc::fanspeed_t::kMin;
    else if (!strcasecmp(s, "low") || !strcasecmp(s, "min")) *out = (uint8_t)stdAc::fanspeed_t::kLow;
    else if (!strcasecmp(s, "medium") || !strcasecmp(s, "med")) *out = (uint8_t)stdAc::fanspeed_t::kMedium;
    else if (!strcasecmp(s, "high") || !strcasecmp(s, "max")) *out = (uint8_t)stdAc::fanspeed_t::kHigh;
    else return false;
    return true;
}

const char* ir_ac_fan_to_string(uint8_t fan) {
    switch ((stdAc::fanspeed_t)fan) {
    case stdAc::fanspeed_t::kAuto: return "auto";
    case stdAc::fanspeed_t::kMin: return "quiet";
    case stdAc::fanspeed_t::kLow: return "low";
    case stdAc::fanspeed_t::kMedium: return "medium";
    case stdAc::fanspeed_t::kHigh: return "high";
    case stdAc::fanspeed_t::kMax: return "high";
    default: return "unknown";
    }
}

uint8_t ir_ac_clamp_temp(int temp) {
    if (temp < kMinTemp) temp = kMinTemp;
    if (temp > kMaxTemp) temp = kMaxTemp;
    return (uint8_t)temp;
}

AcCommand ir_ac_button_toggle() {
    AcState ac = g_state.acState();
    AcCommand cmd;
    cmd.has_power = true;
    if (ac.known && ac.power) {
        cmd.power = false; // currently on -> turn off
    } else {
        // Summer default: cool, 24 C, vertical swing.
        cmd.power = true;
        cmd.has_mode = true;
        cmd.mode = (uint8_t)stdAc::opmode_t::kCool;
        cmd.has_temp = true;
        cmd.temp = 24;
        cmd.has_swing = true;
        cmd.swing = true;
    }
    return cmd;
}
