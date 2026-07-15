// Standalone IR-capture tool (env:ircapture only — excluded from the main build).
// Point an IR remote at the StickS3 receiver and press buttons; this prints the
// decoded protocol, bit count, raw state bytes, and (for A/C remotes) the
// human-readable settings. Used to identify the Daikin ARC478A33 protocol.
//
//   pio run -e ircapture -t upload && pio device monitor

#include <M5Unified.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <IRac.h>

namespace {
constexpr uint16_t kRecvPin = 42;          // StickS3 IR receiver
constexpr uint16_t kCaptureBufferSize = 1024; // Daikin messages are long
constexpr uint8_t kTimeoutMs = 50;         // gap that marks end of a message
constexpr uint16_t kMinUnknownSize = 12;

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeoutMs, true);
decode_results results;
} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // The IR block is on the external power rail; enable it. And the speaker amp
    // interferes with IR RX, so shut it down.
    M5.Power.setExtOutput(true, m5::ext_none);
    M5.Speaker.end();

    irrecv.setUnknownThreshold(kMinUnknownSize);
    irrecv.enableIRIn();

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 4);
    M5.Display.print("IR capture\naim remote");

    Serial.println("\n=== IR CAPTURE READY (pin 42) ===");
    Serial.println("Point the Daikin ARC478A33 remote at the Stick and press POWER.");
}

void loop() {
    if (irrecv.decode(&results)) {
        Serial.println("\n----------------------------------------");
        Serial.print("Protocol : ");
        Serial.println(typeToString(results.decode_type, results.repeat));
        Serial.print("Bits     : ");
        Serial.println(results.bits);

        if (hasACState(results.decode_type)) {
            Serial.print("State    : ");
            Serial.println(resultToHexidecimal(&results));
            String ac = IRAcUtils::resultAcToString(&results);
            if (ac.length()) {
                Serial.println("Settings :");
                Serial.println(ac);
            }
        } else {
            Serial.print("Value    : 0x");
            Serial.println((uint64_t)results.value, HEX);
        }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(4, 4);
        M5.Display.printf("%s\n%d bits",
                          typeToString(results.decode_type, results.repeat).c_str(),
                          results.bits);

        yield();
    }
    M5.update();
}
