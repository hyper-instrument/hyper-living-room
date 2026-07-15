// Standalone IR TX identifier (env:irtxtest only — excluded from the main build).
// The StickS3 IR receiver can't be decoded by this library (needs RMT), so we
// identify the AC's protocol by transmitting: cycle through the Daikin variants
// sending a distinctive command (power ON, COOL, 30C). The AC beeps / turns on
// for the matching protocol; read the name shown on the Stick screen.
//
//   pio run -e irtxtest -t upload && pio device monitor
//
//   Button B = send next variant   Button A = resend current variant

#include <M5Unified.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>

namespace {
constexpr uint16_t kIrLed = 46; // StickS3 IR transmitter

IRac irac(kIrLed);

struct Candidate {
    decode_type_t proto;
    const char* name;
};

const Candidate kCandidates[] = {
    {DAIKIN, "DAIKIN"},       {DAIKIN2, "DAIKIN2"},   {DAIKIN216, "DAIKIN216"},
    {DAIKIN160, "DAIKIN160"}, {DAIKIN176, "DAIKIN176"}, {DAIKIN128, "DAIKIN128"},
    {DAIKIN152, "DAIKIN152"}, {DAIKIN64, "DAIKIN64"}, {DAIKIN200, "DAIKIN200"},
    {DAIKIN312, "DAIKIN312"},
};
const size_t kNum = sizeof(kCandidates) / sizeof(kCandidates[0]);
size_t idx = 0;

void drawScreen() {
    const Candidate& c = kCandidates[idx];
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(6, 8);
    M5.Display.setTextSize(2);
    M5.Display.printf("%u/%u\n", (unsigned)(idx + 1), (unsigned)kNum);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(6, 44);
    M5.Display.println(c.name);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(6, 100);
    M5.Display.print("ON COOL 30");
}

void sendCurrent() {
    const Candidate& c = kCandidates[idx];
    Serial.printf("[%u/%u] %s -> power ON, COOL, 30C\n",
                  (unsigned)(idx + 1), (unsigned)kNum, c.name);
    irac.sendAc(c.proto, -1, /*power=*/true, stdAc::opmode_t::kCool, /*degrees=*/30.0f,
                /*celsius=*/true,
                stdAc::fanspeed_t::kAuto, stdAc::swingv_t::kOff, stdAc::swingh_t::kOff,
                /*quiet=*/false, /*turbo=*/false, /*econo=*/false, /*light=*/false,
                /*filter=*/false, /*clean=*/false, /*beep=*/false, /*sleep=*/-1,
                /*clock=*/-1);
}
} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Power.setExtOutput(true, m5::ext_none); // power the IR transmitter rail
    M5.Speaker.end();
    M5.Display.setRotation(1);
    delay(400);

    Serial.println("\n=== IR TX identify (Daikin variants) ===");
    Serial.println("Point the Stick at the AC (front IR window). Button B = next variant,");
    Serial.println("Button A = resend. Watch the AC: the name on screen when it beeps/");
    Serial.println("turns on to 30C COOL is your protocol.");
    drawScreen();
    sendCurrent(); // send #1 immediately
}

void loop() {
    M5.update();

    // Button B advances to the next Daikin variant.
    if (M5.BtnB.wasClicked()) {
        idx = (idx + 1) % kNum;
        drawScreen();
        sendCurrent();
        return;
    }

    // Auto-repeat the current variant ~2x/sec so a phone camera sees continuous
    // flashing (emission test) and aiming at the AC is easy.
    static uint32_t last = 0;
    if (millis() - last > 450) {
        last = millis();
        sendCurrent();
    }
    delay(10);
}
