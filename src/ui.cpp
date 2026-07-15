#include "ui.h"

#include <M5Unified.h>
#include <WiFi.h>

#include "app_config.h"
#include "state.h"

namespace {

enum Screen { SCREEN_MAIN, SCREEN_ENERGY, SCREEN_NET, SCREEN_COUNT };

M5Canvas g_canvas(&M5.Display);
int g_screen = SCREEN_MAIN;

constexpr int W = 240;
constexpr int H = 135;

constexpr uint16_t COL_BG = 0x0861;      // near-black blue
constexpr uint16_t COL_TEXT = 0xEF7D;    // off-white
constexpr uint16_t COL_DIM = 0x8C71;     // gray
constexpr uint16_t COL_ACCENT = 0x057F;  // blue
constexpr uint16_t COL_ON = 0x2E8B;      // green
constexpr uint16_t COL_WARN = 0xFB44;    // orange

void drawTopBar(bool tapoReady) {
    g_canvas.setFont(&fonts::efontCN_12);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.setTextDatum(top_left);
    static const char* kTitles[SCREEN_COUNT] = {"室内环境", "用电统计", "网络状态"};
    g_canvas.drawString(kTitles[g_screen], 4, 3);

    g_canvas.setTextDatum(top_right);
    String right;
    right += WiFi.status() == WL_CONNECTED ? "WiFi" : "无WiFi";
    right += tapoReady ? " · 插座" : " · 无插座";
    int batt = M5.Power.getBatteryLevel();
    if (batt >= 0) right += " · " + String(batt) + "%";
    g_canvas.drawString(right, W - 4, 3);
    g_canvas.drawFastHLine(0, 17, W, COL_DIM);
}

void drawMain() {
    SensorData s = g_state.sensor();
    PlugData p = g_state.plug();
    bool stale = s.updatedMs == 0 || millis() - s.updatedMs > SENSOR_STALE_MS;
    uint16_t valCol = stale ? COL_DIM : COL_TEXT;

    g_canvas.setTextDatum(top_left);
    g_canvas.setFont(&fonts::efontCN_14);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("温度", 10, 24);
    g_canvas.drawString("湿度", 150, 24);

    g_canvas.setFont(&fonts::Font7); // 48px seven-segment digits
    g_canvas.setTextColor(valCol);
    String t = (stale || isnan(s.tempC)) ? "--.-" : String(s.tempC, 1);
    String h = (stale || isnan(s.humPct)) ? "--" : String((int)roundf(s.humPct));
    int tw = g_canvas.textWidth(t);
    int hw = g_canvas.textWidth(h);
    g_canvas.drawString(t, 10, 42);
    g_canvas.drawString(h, 150, 42);

    g_canvas.setFont(&fonts::efontCN_14);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("°C", 14 + tw, 74);
    g_canvas.drawString("%", 154 + hw, 74);

    // AC status line
    g_canvas.setFont(&fonts::efontCN_16);
    if (p.known) {
        g_canvas.setTextColor(p.on ? COL_ON : COL_DIM);
        String line = String("空调: ") + (p.on ? "开" : "关");
        if (p.on && !isnan(p.powerW)) line += "  " + String(p.powerW, 0) + "W";
        if (!isnan(p.todayKWh)) line += "  今日 " + String(p.todayKWh, 2) + "kWh";
        g_canvas.drawString(line, 10, 96);
    } else {
        g_canvas.setTextColor(COL_WARN);
        g_canvas.drawString("空调插座未连接", 10, 96);
    }

    g_canvas.setFont(&fonts::efontCN_12);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("A:开关空调  B:切换页面", 10, 120);
}

void drawEnergy() {
    PlugData p = g_state.plug();
    g_canvas.setTextDatum(top_left);
    g_canvas.setFont(&fonts::efontCN_16);

    struct Row { String label, value; };
    Row rows[3] = {
        {"当前功率", p.known && !isnan(p.powerW) ? String(p.powerW, 1) + " W" : "--"},
        {"今日用电", p.known && !isnan(p.todayKWh) ? String(p.todayKWh, 2) + " kWh" : "--"},
        {"今日运行", p.known && p.todayRuntimeMin >= 0
                         ? String(p.todayRuntimeMin / 60) + "时" + String(p.todayRuntimeMin % 60) + "分"
                         : "--"},
    };
    for (int i = 0; i < 3; i++) {
        int y = 30 + i * 30;
        g_canvas.setTextColor(COL_DIM);
        g_canvas.drawString(rows[i].label, 10, y);
        g_canvas.setTextColor(COL_TEXT);
        g_canvas.drawString(rows[i].value, 110, y);
    }
    g_canvas.setFont(&fonts::efontCN_12);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("B:切换页面", 10, 120);
}

void drawNet() {
    SensorData s = g_state.sensor();
    g_canvas.setTextDatum(top_left);
    g_canvas.setFont(&fonts::efontCN_12);

    String rows[6] = {
        String("IP: ") + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "未连接"),
        String("网址: http://") + APP_HOSTNAME + ".local",
        String("信号: ") + String(WiFi.RSSI()) + " dBm",
        String("传感器: ") + (s.mac[0] ? s.mac : "搜索中..."),
        String("插座: ") + TAPO_IP,
        String("运行: ") + String(millis() / 60000) + " 分钟  内存: " + String(ESP.getFreeHeap() / 1024) + "K",
    };
    for (int i = 0; i < 6; i++) {
        g_canvas.setTextColor(i == 1 ? COL_ACCENT : COL_TEXT);
        g_canvas.drawString(rows[i], 6, 24 + i * 18);
    }
}

} // namespace

void ui_init() {
    M5.Display.setRotation(1); // landscape, USB on the left
    M5.Display.setBrightness(120);
    g_canvas.setColorDepth(8); // 240*135 = 32KB sprite, easy on heap
    g_canvas.createSprite(W, H);
}

void ui_splash(const char* msg) {
    g_canvas.fillSprite(COL_BG);
    g_canvas.setFont(&fonts::efontCN_16);
    g_canvas.setTextColor(COL_TEXT);
    g_canvas.setTextDatum(middle_center);
    g_canvas.drawString(msg, W / 2, H / 2);
    g_canvas.pushSprite(0, 0);
}

void ui_next_screen() {
    g_screen = (g_screen + 1) % SCREEN_COUNT;
}

void ui_draw(bool tapoReady) {
    g_canvas.fillSprite(COL_BG);
    drawTopBar(tapoReady);
    switch (g_screen) {
    case SCREEN_MAIN: drawMain(); break;
    case SCREEN_ENERGY: drawEnergy(); break;
    case SCREEN_NET: drawNet(); break;
    }
    g_canvas.pushSprite(0, 0);
}
