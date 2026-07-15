#include "web_ui.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "app_config.h"
#include "ir_ac.h"
#include "mcp_server.h"
#include "state.h"
#include "web_ui_html.h"

namespace {

AsyncWebServer g_server(80);

void handleStatus(AsyncWebServerRequest* request) {
    SensorData s = g_state.sensor();
    PlugData p = g_state.plug();

    JsonDocument doc;
    if (!isnan(s.tempC)) doc["tempC"] = serialized(String(s.tempC, 1));
    else doc["tempC"] = nullptr;
    if (!isnan(s.humPct)) doc["humPct"] = serialized(String(s.humPct, 1));
    else doc["humPct"] = nullptr;
    if (s.battPct >= 0) doc["sensorBatt"] = s.battPct;
    if (s.updatedMs > 0) doc["sensorAgeSec"] = (millis() - s.updatedMs) / 1000;
    else doc["sensorAgeSec"] = nullptr;

    doc["plugKnown"] = p.known;
    doc["plugOn"] = p.on;
    if (!isnan(p.powerW)) doc["powerW"] = serialized(String(p.powerW, 1));
    else doc["powerW"] = nullptr;
    if (!isnan(p.todayKWh)) doc["todayKWh"] = serialized(String(p.todayKWh, 3));
    else doc["todayKWh"] = nullptr;
    doc["todayRuntimeMin"] = p.todayRuntimeMin;

    AcState ac = g_state.acState();
    doc["acKnown"] = ac.known;
    if (ac.known) {
        doc["acOn"] = ac.power;
        doc["acMode"] = ir_ac_mode_to_string(ac.mode);
        doc["acTemp"] = ac.temp;
        doc["acFan"] = ir_ac_fan_to_string(ac.fan);
        doc["acSwing"] = ac.swing;
        doc["acStreamer"] = ac.streamer;
    }

    doc["rssi"] = WiFi.RSSI();
    doc["host"] = APP_HOSTNAME;
    doc["uptimeSec"] = millis() / 1000;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handlePlug(AsyncWebServerRequest* request) {
    if (!request->hasParam("on")) {
        request->send(400, "application/json", "{\"error\":\"missing 'on' param\"}");
        return;
    }
    bool on = request->getParam("on")->value().toInt() != 0;
    g_state.requestPlugCommand(on ? PLUG_CMD_ON : PLUG_CMD_OFF);
    request->send(200, "application/json", "{\"ok\":true}");
}

// Controls the Daikin AC over IR. Any of: power, mode, temp, fan, swing, streamer.
void handleAc(AsyncWebServerRequest* request) {
    AcCommand cmd;
    if (request->hasParam("power")) {
        cmd.has_power = true;
        cmd.power = request->getParam("power")->value().toInt() != 0;
    }
    if (request->hasParam("temp")) {
        cmd.has_temp = true;
        cmd.temp = ir_ac_clamp_temp(request->getParam("temp")->value().toInt());
    }
    if (request->hasParam("mode")) {
        uint8_t m;
        if (ir_ac_mode_from_string(request->getParam("mode")->value().c_str(), &m)) {
            cmd.has_mode = true;
            cmd.mode = m;
        }
    }
    if (request->hasParam("fan")) {
        uint8_t f;
        if (ir_ac_fan_from_string(request->getParam("fan")->value().c_str(), &f)) {
            cmd.has_fan = true;
            cmd.fan = f;
        }
    }
    if (request->hasParam("swing")) {
        cmd.has_swing = true;
        cmd.swing = request->getParam("swing")->value().toInt() != 0;
    }
    if (request->hasParam("streamer")) {
        cmd.has_streamer = true;
        cmd.streamer = request->getParam("streamer")->value().toInt() != 0;
    }
    g_state.requestAcCommand(cmd);
    request->send(200, "application/json", "{\"ok\":true}");
}

} // namespace

void web_ui_start() {
    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", WEB_UI_HTML);
    });
    g_server.on("/api/status", HTTP_GET, handleStatus);
    g_server.on("/api/plug", HTTP_POST, handlePlug);
    g_server.on("/api/ac", HTTP_POST, handleAc);
    mcp_register(g_server);
    g_server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "not found");
    });
    g_server.begin();
    Serial.println("WEB: server started on port 80");
}
