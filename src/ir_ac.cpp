#include "ir_ac.h"

#include <IRremoteESP8266.h>
#include <IRac.h>

#include "ble_sensor.h"
#include "state.h"

namespace {
constexpr uint16_t kIrLed = 46; // StickS3 IR transmitter

// Sent through IRac::sendAc — the exact code path that made the AC respond
// during the protocol-identification sweep. It fills the complete remote state
// (every field) instead of poking a raw IRDaikin2 object.
IRac g_irac(kIrLed);

// The desired remote state, kept across commands so partial updates (e.g. only
// temp) preserve everything else.
stdAc::state_t g_st;

// Practical temperature bounds (the protocol allows 10..32, but a room AC
// realistically runs 16..30).
constexpr int kMinTemp = 16;
constexpr int kMaxTemp = 30;

// Frames per command.
constexpr int kIrSendFrames = 2;

void sendState() {
    // Quiet the radios' host-side work and stop the scheduler from preempting
    // us mid-frame: delayMicroseconds timing is only accurate if this task
    // keeps the core (short ISRs are fine, task preemption is not).
    ble_sensor_pause();
    delay(20);
    UBaseType_t prev = uxTaskPriorityGet(nullptr);
    vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 1);
    for (int i = 0; i < kIrSendFrames; i++) g_irac.sendAc(g_st);
    vTaskPrioritySet(nullptr, prev);
    ble_sensor_resume();
}
} // namespace

void ir_ac_init() {
    // Same baseline the successful identification sweep used, plus our
    // defaults. Not transmitted until the first real command.
    g_st = {};
    g_st.protocol = decode_type_t::DAIKIN152; // confirmed: AC beeps for #7 in the sweep

    g_st.model = -1;
    g_st.power = false;
    g_st.mode = stdAc::opmode_t::kCool;
    g_st.degrees = 26;
    g_st.celsius = true;
    g_st.fanspeed = stdAc::fanspeed_t::kAuto;
    g_st.swingv = stdAc::swingv_t::kOff;
    g_st.swingh = stdAc::swingh_t::kOff;
    g_st.quiet = false;
    g_st.turbo = false;
    g_st.econo = false;
    g_st.light = false;
    g_st.filter = false;
    g_st.clean = false;
    g_st.beep = false;
    g_st.sleep = -1;
    g_st.clock = -1;
    Serial.printf("IR: Daikin2 via IRac on pin %u\n", kIrLed);
}

// Long-press Button A: replicate the successful identification sweep — send
// "power ON, cool, 24C, swing" continuously for ~10 s while the user aims at
// the AC. Doubles as a camera emission check. Updates the tracked state.
void ir_ac_blast_test() {
    Serial.println("IR: continuous ON blast (~10 s)");
    g_st.power = true;
    g_st.mode = stdAc::opmode_t::kCool;
    g_st.degrees = 24;
    g_st.swingv = stdAc::swingv_t::kAuto;
    uint32_t start = millis();
    while (millis() - start < 10000) {
        sendState();
        delay(150);
    }

    AcState s;
    s.known = true;
    s.power = true;
    s.mode = (uint8_t)g_st.mode;
    s.temp = (uint8_t)g_st.degrees;
    s.fan = (uint8_t)g_st.fanspeed;
    s.swing = true;
    s.updatedMs = millis();
    g_state.setAcState(s);
}

void ir_ac_service() {
    AcCommand c = g_state.takeAcCommand();
    if (!c.pending) return;

    if (c.has_mode) g_st.mode = (stdAc::opmode_t)c.mode;
    if (c.has_temp) g_st.degrees = c.temp;
    if (c.has_fan) g_st.fanspeed = (stdAc::fanspeed_t)c.fan;
    if (c.has_swing) g_st.swingv = c.swing ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
    if (c.has_power) g_st.power = c.power;

    sendState();

    AcState s;
    s.known = true;
    s.power = g_st.power;
    s.mode = (uint8_t)g_st.mode;
    s.temp = (uint8_t)g_st.degrees;
    s.fan = (uint8_t)g_st.fanspeed;
    s.swing = g_st.swingv != stdAc::swingv_t::kOff;
    s.updatedMs = millis();
    g_state.setAcState(s);

    Serial.printf("IR: sent Daikin152 power=%d mode=%s temp=%u fan=%s swing=%d\n",
                  s.power, ir_ac_mode_to_string(s.mode), s.temp,
                  ir_ac_fan_to_string(s.fan), s.swing);
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
