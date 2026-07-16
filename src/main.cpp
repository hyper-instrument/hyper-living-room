#include <ESPmDNS.h>
#include <M5Unified.h>
#include <WiFi.h>

#include "app_config.h"
#include "ble_sensor.h"
#include "ir_ac.h"
#include "state.h"
#include "tapo_client.h"
#include "ui.h"
#include "web_ui.h"

AppState g_state;

namespace {

TapoClient g_tapo;

uint32_t g_lastTapoPoll = 0;
uint32_t g_lastTapoConnect = 0;
uint32_t g_lastDraw = 0;

void connectWifi() {
    ui_splash("连接 WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(APP_HOSTNAME);
    WiFi.setAutoReconnect(true);
    // NB: do NOT disable WiFi modem-sleep here — BLE coexistence on the ESP32-S3
    // requires it, and turning it off aborts in coex_enable (boot loop).
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WIFI: connected, IP=" + WiFi.localIP().toString());
    } else {
        Serial.println("WIFI: not connected yet, will keep retrying in background");
    }
}

void serviceTapo() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!g_tapo.ready()) {
        if (millis() - g_lastTapoConnect < TAPO_RECONNECT_MS) return;
        g_lastTapoConnect = millis();
        if (!g_tapo.begin(TAPO_IP, TAPO_USER, TAPO_PASS)) return;
        g_lastTapoPoll = 0; // poll immediately after (re)connect
    }

    PlugCommand cmd = g_state.takePendingPlugCommand();
    if (cmd != PLUG_CMD_NONE) {
        bool target = (cmd == PLUG_CMD_TOGGLE) ? !g_state.plug().on : (cmd == PLUG_CMD_ON);
        g_tapo.setOn(target);
        g_tapo.refresh();
        g_lastTapoPoll = millis();
        return;
    }

    if (g_lastTapoPoll == 0 || millis() - g_lastTapoPoll >= TAPO_POLL_MS) {
        g_lastTapoPoll = millis();
        g_tapo.refresh();
    }
}

} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    g_state.begin();
    ui_init();

    // The IR transmitter is on the external power rail; enable it. The speaker
    // amp is disabled: M5 notes it interferes with the IR circuitry, and the
    // firmware that verifiably reached the AC had it off.
    M5.Power.setExtOutput(true, m5::ext_none);
    M5.Speaker.end();
    ir_ac_init();

    connectWifi();
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
    if (MDNS.begin(APP_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
    }

    ble_sensor_start();
    web_ui_start();
    ui_splash("启动完成");
}

void loop() {
    M5.update();

    if (M5.BtnA.wasClicked()) g_state.requestAcCommand(ir_ac_button_toggle());
    if (M5.BtnA.wasHold()) ir_ac_blast_test(); // long-press: IR camera test
    if (M5.BtnB.wasClicked()) ui_next_screen();

    serviceTapo();
    ir_ac_service();

    if (millis() - g_lastDraw >= 1000) {
        g_lastDraw = millis();
        ui_draw(g_tapo.ready());
    }
    delay(10);
}
