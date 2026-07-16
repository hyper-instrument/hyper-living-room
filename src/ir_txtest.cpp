// Standalone IR bisect tool (env:irtxtest only — excluded from the main build).
// Sends the EXACT command that made the AC respond (DAIKIN152, power ON, COOL,
// 30C, fan auto, no swing) on auto-repeat — but WITH WiFi connected, to test
// whether an active WiFi association breaks bit-banged IR on this board.
//
//   pio run -e irtxtest -t upload
//
//   Auto-repeats every 450 ms. Button A = force resend.
//   Screen shows WiFi state so the variable under test is visible.

#include <M5Unified.h>
#include <WiFi.h>
#include <IRremoteESP8266.h>
#include <IRac.h>

#include "app_config.h"

namespace {
constexpr uint16_t kIrLed = 46;
IRac irac(kIrLed);

void sendOnce() {
    // Byte-identical to the command the AC responded to in the sweep.
    irac.sendAc(decode_type_t::DAIKIN152, -1, /*power=*/true, stdAc::opmode_t::kCool,
                /*degrees=*/30.0f, /*celsius=*/true, stdAc::fanspeed_t::kAuto,
                stdAc::swingv_t::kOff, stdAc::swingh_t::kOff,
                /*quiet=*/false, /*turbo=*/false, /*econo=*/false, /*light=*/false,
                /*filter=*/false, /*clean=*/false, /*beep=*/false, /*sleep=*/-1,
                /*clock=*/-1);
    Serial.println("IR: sent DAIKIN152 ON COOL 30 (WiFi test)");
}

void drawScreen() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(6, 8);
    M5.Display.setTextSize(2);
    M5.Display.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "off");
    M5.Display.setTextSize(3);
    M5.Display.setCursor(6, 44);
    M5.Display.println("DAIKIN152");
    M5.Display.setTextSize(2);
    M5.Display.setCursor(6, 100);
    M5.Display.print("ON COOL 30");
}
} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Power.setExtOutput(true, m5::ext_none);
    M5.Speaker.end();
    M5.Display.setRotation(1);

    // The variable under test: a live WiFi association.
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(200);
    Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "NOT connected");

    drawScreen();
    sendOnce();
}

void loop() {
    M5.update();
    static uint32_t last = 0;
    static uint32_t lastDraw = 0;
    if (M5.BtnA.wasClicked()) sendOnce();
    if (millis() - last > 450) {
        last = millis();
        sendOnce();
    }
    if (millis() - lastDraw > 2000) {
        lastDraw = millis();
        drawScreen();
    }
    delay(10);
}
