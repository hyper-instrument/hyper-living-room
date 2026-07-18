// Raw IR capture using the NEW RMT API (env:ircapture3, Arduino core 3.x).
// The legacy IDF4 RMT driver is broken on the StickS3 (TX emits nothing, RX
// floods errors); this uses the same driver + config as M5's official IR
// example. Prints each received frame as a rawData array (1 us units) for
// offline analysis with IRremoteESP8266's tools/auto_analyse_raw_data.py.
//
//   pio run -e ircapture3 -t upload && pio device monitor
//   Point the remote at the Stick's IR window (10-30 cm) and press buttons.

#include <M5Unified.h>
#include <driver/rmt_rx.h>

namespace {
constexpr gpio_num_t kRxPin = GPIO_NUM_42;

rmt_channel_handle_t g_chan = nullptr;
QueueHandle_t g_queue = nullptr;
rmt_symbol_word_t g_buf[256];
uint32_t g_frameNo = 0;

const rmt_receive_config_t kRxCfg = {
    .signal_range_min_ns = 1000,      // ignore <1 us glitches
    .signal_range_max_ns = 20000000,  // 20 ms silence ends the frame
};

bool IRAM_ATTR onRecvDone(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata,
                          void* uctx) {
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)uctx, edata, &hp);
    return hp == pdTRUE;
}
} // namespace

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = false; // speaker amp interferes with IR RX (M5 docs)
    cfg.internal_mic = false;
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Power.setExtOutput(true, m5::ext_none);
    M5.Speaker.end();

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 4);
    M5.Display.print("IR RAW REC v3\naim remote\npress keys");

    rmt_rx_channel_config_t ccfg = {};
    ccfg.gpio_num = kRxPin;
    ccfg.clk_src = RMT_CLK_SRC_DEFAULT;
    ccfg.resolution_hz = 1000000; // 1 us per tick
    ccfg.mem_block_symbols = 128;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&ccfg, &g_chan));

    g_queue = xQueueCreate(4, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done = onRecvDone;
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(g_chan, &cbs, g_queue));
    ESP_ERROR_CHECK(rmt_enable(g_chan));
    ESP_ERROR_CHECK(rmt_receive(g_chan, g_buf, sizeof(g_buf), &kRxCfg));

    ledcAttach(46, 38000, 8); // TX carrier for the loopback burst
    Serial.println("\n=== IR RAW CAPTURE v3 READY (new RMT API, pin 42) ===");
}

// Diagnostics: sample the RX pin level (a demodulating receiver idles HIGH;
// stuck LOW = unpowered/wrong pin) and emit an IR burst from the Stick's own
// TX LED (pin 46) every ~3 s as a loopback test — hold a white card over the
// top of the Stick to bounce it into the receiver.
namespace {
constexpr uint8_t kTxPin = 46;
uint32_t g_lastDiag = 0;
uint32_t g_lastBurst = 0;

void diagPinLevel() {
    uint32_t hi = 0, total = 20000;
    for (uint32_t i = 0; i < total; i++) hi += gpio_get_level(kRxPin);
    Serial.printf("pin42 level: high %.1f%%\n", 100.0 * hi / total);
}

void txBurst() {
    // Crude NEC-ish pattern at 38 kHz so the receiver has real edges to chew on.
    auto mark = [](uint32_t us) { ledcWrite(kTxPin, 128); delayMicroseconds(us); };
    auto space = [](uint32_t us) { ledcWrite(kTxPin, 0); delayMicroseconds(us); };
    mark(9000); space(4500);
    for (int i = 0; i < 16; i++) { mark(560); space(i % 2 ? 1690 : 560); }
    mark(560); space(100);
    ledcWrite(kTxPin, 0);
    Serial.println("(tx burst sent)");
}
} // namespace

void loop() {
    M5.update();

    if (millis() - g_lastDiag > 2000) { g_lastDiag = millis(); diagPinLevel(); }
    if (millis() - g_lastBurst > 3000) { g_lastBurst = millis(); txBurst(); }

    rmt_rx_done_event_data_t ev;
    if (!xQueueReceive(g_queue, &ev, pdMS_TO_TICKS(200))) {
        // Keep the receiver armed: if a previous receive aborted (overflow /
        // noise), no event fires and we'd otherwise stay dead. Re-arming while
        // already armed just returns ESP_ERR_INVALID_STATE — ignore it.
        rmt_receive(g_chan, g_buf, sizeof(g_buf), &kRxCfg);
        return;
    }

    size_t n = ev.num_symbols;
    if (n > 0 && n < 8) {
        // Noise blip — show it so we know the receiver hardware is alive.
        Serial.printf("(noise: %u symbols)\n", (unsigned)n);
    }
    if (n >= 8) {
        g_frameNo++;
        Serial.printf("\n--- frame %u: %u symbols ---\n", g_frameNo, (unsigned)n);
        Serial.printf("uint16_t rawData%u[] = {", g_frameNo);
        bool first = true;
        for (size_t i = 0; i < n; i++) {
            const rmt_symbol_word_t& s = ev.received_symbols[i];
            if (s.duration0) {
                Serial.printf("%s%u", first ? "" : ", ", s.duration0);
                first = false;
            }
            if (s.duration1) Serial.printf(", %u", s.duration1);
        }
        Serial.println("};");

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(4, 4);
        M5.Display.printf("frame #%u\n%u symbols\ncaptured", g_frameNo, (unsigned)n);
    }
    // Re-arm for the next frame.
    rmt_receive(g_chan, g_buf, sizeof(g_buf), &kRxCfg);
}
