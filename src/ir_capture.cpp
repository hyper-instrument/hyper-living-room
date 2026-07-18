// Raw IR capture via the RMT peripheral (env:ircapture only).
// The StickS3 IR receiver (GPIO 42) cannot be sampled by IRremoteESP8266's
// GPIO-interrupt receiver (floods with noise on this board — see README); the
// RMT peripheral is required. This records demodulated mark/space durations
// (1 us ticks) and prints them as a rawData array for offline analysis with
// IRremoteESP8266's tools/auto_analyse_raw_data.py.
//
//   pio run -e ircapture -t upload && pio device monitor
//   Point the remote at the Stick's IR window and press buttons.

#include <M5Unified.h>
#include <driver/rmt.h>

namespace {
constexpr gpio_num_t kRxPin = GPIO_NUM_42;
constexpr rmt_channel_t kCh = RMT_CHANNEL_4; // ESP32-S3: RX channels are 4..7

RingbufHandle_t g_rb = nullptr;
uint32_t g_frameNo = 0;
} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // Per M5 docs: IR needs the ext power rail; the speaker amp disturbs RX.
    M5.Power.setExtOutput(true, m5::ext_none);
    M5.Speaker.end();

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 4);
    M5.Display.print("IR RAW REC\naim remote\npress keys");

    rmt_config_t rcfg = RMT_DEFAULT_CONFIG_RX(kRxPin, kCh);
    rcfg.clk_div = 80;                       // 80 MHz / 80 -> 1 us per tick
    rcfg.mem_block_num = 4;                  // 4x48 items: fits ~190 marks/spaces
    rcfg.rx_config.idle_threshold = 12000;   // >12 ms silence = end of frame
    rcfg.rx_config.filter_en = true;
    rcfg.rx_config.filter_ticks_thresh = 100; // drop <100 us glitches
    ESP_ERROR_CHECK(rmt_config(&rcfg));
    ESP_ERROR_CHECK(rmt_driver_install(kCh, 8192, 0));
    rmt_get_ringbuf_handle(kCh, &g_rb);
    rmt_rx_start(kCh, true);

    Serial.println("\n=== IR RAW CAPTURE READY (RMT, pin 42) ===");
    Serial.println("Press a button on the remote. Each frame prints as rawData.");
}

void loop() {
    M5.update();
    size_t len = 0;
    rmt_item32_t* items = (rmt_item32_t*)xRingbufferReceive(g_rb, &len, pdMS_TO_TICKS(200));
    if (!items) return;

    size_t n = len / sizeof(rmt_item32_t);
    if (n >= 8) { // ignore stray blips
        g_frameNo++;
        // Durations alternate mark/space starting with the first item's
        // duration0. Zero durations mark the end-of-frame entry — skip them.
        Serial.printf("\n--- frame %u: %u items ---\n", g_frameNo, (unsigned)n);
        Serial.printf("uint16_t rawData%u[] = {", g_frameNo);
        bool first = true;
        for (size_t i = 0; i < n; i++) {
            if (items[i].duration0) {
                Serial.printf("%s%u", first ? "" : ", ", items[i].duration0);
                first = false;
            }
            if (items[i].duration1) {
                Serial.printf(", %u", items[i].duration1);
            }
        }
        Serial.println("};");

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(4, 4);
        M5.Display.setTextSize(2);
        M5.Display.printf("frame #%u\n%u items\ncaptured", g_frameNo, (unsigned)n);
    }
    vRingbufferReturnItem(g_rb, items);
}
