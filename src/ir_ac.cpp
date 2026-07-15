#include "ir_ac.h"

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Daikin.h>

#include "ble_sensor.h"
#include "state.h"

namespace {
constexpr uint16_t kIrLed = 46; // StickS3 IR transmitter

// Bit-banged send via IRremoteESP8266 — the only path camera-verified to emit
// light on this board (the legacy RMT driver produced no output on the S3).
// Timing is protected in sendFrames() by pausing BLE and boosting priority.
IRDaikin2 g_ac(kIrLed);

// Tracked locally: the IRDaikin2 vertical-swing/purify getters are awkward to
// read back, so we remember what we last set.
bool g_swing = false;
bool g_streamer = false;

// Practical temperature bounds (the protocol allows 10..32, but a room AC
// realistically runs 16..30).
constexpr int kMinTemp = 16;
constexpr int kMaxTemp = 30;

// Extra repeats per command (total frames = 1 + this).
constexpr uint16_t kIrSendRepeats = 4;

void sendFrames(uint16_t repeats) {
    // Quiet the radios' host-side work and stop the scheduler from preempting
    // us mid-frame: delayMicroseconds timing is only accurate if this task
    // keeps the core (short ISRs are fine, task preemption is not).
    ble_sensor_pause();
    delay(20);
    UBaseType_t prev = uxTaskPriorityGet(nullptr);
    vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 1);
    g_ac.send(repeats);
    vTaskPrioritySet(nullptr, prev);
    ble_sensor_resume();
}
} // namespace

void ir_ac_init() {
    g_ac.begin();
    // Sensible defaults; not sent until the first real command.
    g_ac.setPower(false);
    g_ac.setMode(kDaikinCool);
    g_ac.setTemp(26);
    g_ac.setFan(kDaikinFanAuto);
    Serial.printf("IR: Daikin2 bit-bang on pin %u\n", kIrLed);
}

// Camera-check helper: ~8 s of continuous IR so a phone selfie camera can
// verify emission from the full firmware. Triggered by long-pressing Button A.
void ir_ac_blast_test() {
    Serial.println("IR: blast test (10 frames)");
    sendFrames(9);
}

void ir_ac_service() {
    AcCommand c = g_state.takeAcCommand();
    if (!c.pending) return;

    if (c.has_mode) g_ac.setMode(c.mode);
    if (c.has_temp) g_ac.setTemp(c.temp);
    if (c.has_fan) g_ac.setFan(c.fan);
    if (c.has_swing) {
        g_swing = c.swing;
        g_ac.setSwingVertical((uint8_t)(c.swing ? kDaikin2SwingVAuto : kDaikin2SwingVOff));
    }
    if (c.has_streamer) {
        g_streamer = c.streamer;
        g_ac.setPurify(c.streamer);
    }
    if (c.has_power) g_ac.setPower(c.power);

    sendFrames(kIrSendRepeats);

    AcState s;
    s.known = true;
    s.power = g_ac.getPower();
    s.mode = g_ac.getMode();
    s.temp = g_ac.getTemp();
    s.fan = g_ac.getFan();
    s.swing = g_swing;
    s.streamer = g_streamer;
    s.updatedMs = millis();
    g_state.setAcState(s);

    Serial.printf("IR: sent Daikin2 power=%d mode=%s temp=%u fan=%s swing=%d streamer=%d\n",
                  s.power, ir_ac_mode_to_string(s.mode), s.temp,
                  ir_ac_fan_to_string(s.fan), s.swing, s.streamer);
}

bool ir_ac_mode_from_string(const char* s, uint8_t* out) {
    if (!s) return false;
    if (!strcasecmp(s, "auto")) *out = kDaikinAuto;
    else if (!strcasecmp(s, "cool")) *out = kDaikinCool;
    else if (!strcasecmp(s, "heat")) *out = kDaikinHeat;
    else if (!strcasecmp(s, "dry")) *out = kDaikinDry;
    else if (!strcasecmp(s, "fan")) *out = kDaikinFan;
    else return false;
    return true;
}

const char* ir_ac_mode_to_string(uint8_t mode) {
    switch (mode) {
    case kDaikinAuto: return "auto";
    case kDaikinCool: return "cool";
    case kDaikinHeat: return "heat";
    case kDaikinDry: return "dry";
    case kDaikinFan: return "fan";
    default: return "unknown";
    }
}

bool ir_ac_fan_from_string(const char* s, uint8_t* out) {
    if (!s) return false;
    if (!strcasecmp(s, "auto")) *out = kDaikinFanAuto;
    else if (!strcasecmp(s, "quiet")) *out = kDaikinFanQuiet;
    else if (!strcasecmp(s, "low") || !strcasecmp(s, "min")) *out = kDaikinFanMin;
    else if (!strcasecmp(s, "medium") || !strcasecmp(s, "med")) *out = kDaikinFanMed;
    else if (!strcasecmp(s, "high") || !strcasecmp(s, "max")) *out = kDaikinFanMax;
    else return false;
    return true;
}

const char* ir_ac_fan_to_string(uint8_t fan) {
    switch (fan) {
    case kDaikinFanAuto: return "auto";
    case kDaikinFanQuiet: return "quiet";
    case kDaikinFanMin: return "low";
    case kDaikinFanMed: return "medium";
    case kDaikinFanMax: return "high";
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
        cmd.mode = kDaikinCool;
        cmd.has_temp = true;
        cmd.temp = 24;
        cmd.has_swing = true;
        cmd.swing = true;
    }
    return cmd;
}
