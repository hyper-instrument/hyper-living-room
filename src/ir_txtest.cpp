// Standalone IR protocol-identification tool (env:irtxtest only).
// Target: Sharp AY-T22DG (remote B198JB). Cycles ready-made Sharp encoders
// from two independent libraries — the AC beeps for the right one.
//
//   pio run -e irtxtest -t upload
//
//   Auto-repeats every 500 ms. Button B = next candidate, A = force resend.

#include <M5Unified.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <SharpHeatpumpIR.h>
#include <IRSender.h>

namespace {
constexpr uint16_t kIrLed = 46;

IRac g_irac(kIrLed);
IRSenderESP32 g_hpSender(kIrLed, 0); // pin, ledc channel
SharpHeatpumpIR g_hpSharp;

void sendIrremote(sharp_ac_remote_model_t model) {
    g_irac.sendAc(decode_type_t::SHARP_AC, (int16_t)model, true,
                  stdAc::opmode_t::kCool, 25.0f, true, stdAc::fanspeed_t::kAuto,
                  stdAc::swingv_t::kOff, stdAc::swingh_t::kOff,
                  false, false, false, false, false, false, true, -1, -1);
}

struct Candidate {
    const char* name;
    void (*send)();
};
const Candidate kCandidates[] = {
    {"HeatpumpIR", []() {
         g_hpSharp.send(g_hpSender, POWER_ON, MODE_COOL, FAN_AUTO, 25, VDIR_AUTO, HDIR_AUTO);
     }},
    {"IRr A907", []() { sendIrremote(sharp_ac_remote_model_t::A907); }},
    {"IRr A705", []() { sendIrremote(sharp_ac_remote_model_t::A705); }},
    {"IRr A903", []() { sendIrremote(sharp_ac_remote_model_t::A903); }},
};
const size_t kNum = sizeof(kCandidates) / sizeof(kCandidates[0]);
size_t idx = 0;

void sendOnce() {
    kCandidates[idx].send();
    Serial.printf("[%u/%u] %s -> ON COOL 25\n", (unsigned)(idx + 1), (unsigned)kNum,
                  kCandidates[idx].name);
}

void drawScreen() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(6, 8);
    M5.Display.setTextSize(2);
    M5.Display.printf("%u/%u\n", (unsigned)(idx + 1), (unsigned)kNum);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(6, 44);
    M5.Display.println(kCandidates[idx].name);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(6, 100);
    M5.Display.print("ON COOL 25");
}
} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Power.setExtOutput(true, m5::ext_none);
    M5.Speaker.end();
    M5.Display.setRotation(1);
    delay(300);

    drawScreen();
    sendOnce();
}

void loop() {
    M5.update();
    static uint32_t last = 0;
    if (M5.BtnB.wasClicked()) {
        idx = (idx + 1) % kNum;
        drawScreen();
        sendOnce();
        return;
    }
    if (M5.BtnA.wasClicked()) sendOnce();
    if (millis() - last > 500) {
        last = millis();
        sendOnce();
    }
    delay(10);
}
