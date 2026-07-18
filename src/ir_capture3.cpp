// Raw IR capture using the NEW RMT API (env:ircapture3, Arduino core 3.x).
// The legacy IDF4 RMT driver is broken on the StickS3 (TX emits nothing, RX
// floods errors); this uses the same driver + config as M5's official IR
// example. Prints each received frame as a rawData array (1 us units) for
// offline analysis with IRremoteESP8266's tools/auto_analyse_raw_data.py.
//
//   pio run -e ircapture3 -t upload && pio device monitor
//   Point the remote at the Stick's IR window (10-30 cm) and press buttons.

#include <M5Unified.h>
#include <Preferences.h>
#include <driver/rmt_rx.h>

namespace {
constexpr gpio_num_t kRxPin = GPIO_NUM_42;

// Captured frames persist in NVS so the user can press the remote at leisure;
// we harvest later (opening the serial port resets the chip, which would wipe
// RAM). Long-press Button A clears the store.
Preferences g_prefs;

void storeFrame(const String& csv) {
    uint32_t count = g_prefs.getUInt("n", 0);
    g_prefs.putString(("f" + String(count)).c_str(), csv);
    g_prefs.putUInt("n", count + 1);
}

void dumpStored() {
    uint32_t count = g_prefs.getUInt("n", 0);
    Serial.printf("=== stored frames: %u ===\n", count);
    for (uint32_t i = 0; i < count; i++) {
        String v = g_prefs.getString(("f" + String(i)).c_str(), "");
        if (v.length()) Serial.printf("uint16_t stored%u[] = {%s};\n", i, v.c_str());
    }
    Serial.println("=== end stored ===");
}

rmt_channel_handle_t g_chan = nullptr;
QueueHandle_t g_queue = nullptr;
rmt_symbol_word_t g_buf[512];
uint32_t g_frameNo = 0;

const rmt_receive_config_t kRxCfg = {
    .signal_range_min_ns = 1000,      // ignore <1 us glitches
    .signal_range_max_ns = 30000000,  // 30 ms silence ends the frame
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

    g_prefs.begin("ircap");
    dumpStored(); // survives resets — harvested at next serial connect
    Serial.println("\n=== IR RAW CAPTURE v3 READY (new RMT API, pin 42) ===");
}

void loop() {
    M5.update();
    if (M5.BtnA.wasHold()) { // long-press A: clear stored frames
        g_prefs.clear();
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(4, 4);
        M5.Display.print("store cleared");
        Serial.println("(store cleared)");
    }

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
        String csv;
        for (size_t i = 0; i < n; i++) {
            const rmt_symbol_word_t& s = ev.received_symbols[i];
            if (s.duration0) {
                if (csv.length()) csv += ',';
                csv += String(s.duration0);
            }
            if (s.duration1) {
                csv += ',';
                csv += String(s.duration1);
            }
        }
        storeFrame(csv);
        Serial.printf("\n--- frame %u: %u symbols ---\n", g_frameNo, (unsigned)n);
        Serial.printf("uint16_t rawData%u[] = {%s};\n", g_frameNo, csv.c_str());

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(4, 4);
        M5.Display.printf("frame #%u\n%u symbols\ncaptured", g_frameNo, (unsigned)n);
    }
    // Re-arm for the next frame.
    rmt_receive(g_chan, g_buf, sizeof(g_buf), &kRxCfg);
}
