#include "web_ui.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "app_config.h"
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

} // namespace

void web_ui_start() {
    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", WEB_UI_HTML);
    });
    g_server.on("/api/status", HTTP_GET, handleStatus);
    g_server.on("/api/plug", HTTP_POST, handlePlug);
    g_server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "not found");
    });
    g_server.begin();
    Serial.println("WEB: server started on port 80");
}
