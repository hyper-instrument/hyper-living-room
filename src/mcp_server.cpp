#include "mcp_server.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#include "app_config.h"
#include "ir_ac.h"
#include "state.h"

namespace {

// Advertised when the client doesn't pin one; we echo the client's version
// during initialize when present.
constexpr const char* kDefaultProtocol = "2025-06-18";
constexpr const char* kServerName = "m5stick-home-hub";
constexpr const char* kServerVersion = "1.0.0";

void addCors(AsyncWebServerResponse* res) {
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Access-Control-Allow-Headers", "Content-Type, Mcp-Session-Id, *");
    res->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
}

void sendJson(AsyncWebServerRequest* request, int code, const String& body) {
    AsyncWebServerResponse* res = request->beginResponse(code, "application/json", body);
    addCors(res);
    request->send(res);
}

// Snapshot of the current device state as a compact JSON string. Shared shape
// with the human web UI but named for an LLM consumer.
String buildStatusText() {
    SensorData s = g_state.sensor();
    PlugData p = g_state.plug();
    bool stale = s.updatedMs == 0 || millis() - s.updatedMs > SENSOR_STALE_MS;

    JsonDocument doc;
    if (!isnan(s.tempC)) doc["temperature_c"] = serialized(String(s.tempC, 1));
    else doc["temperature_c"] = nullptr;
    if (!isnan(s.humPct)) doc["humidity_pct"] = serialized(String(s.humPct, 1));
    else doc["humidity_pct"] = nullptr;
    if (s.battPct >= 0) doc["sensor_battery_pct"] = s.battPct;
    doc["sensor_stale"] = stale;

    doc["ac_plug_connected"] = p.known;
    doc["ac_on"] = p.on;
    if (!isnan(p.powerW)) doc["power_w"] = serialized(String(p.powerW, 1));
    else doc["power_w"] = nullptr;
    if (!isnan(p.todayKWh)) doc["today_kwh"] = serialized(String(p.todayKWh, 3));
    else doc["today_kwh"] = nullptr;
    doc["today_runtime_min"] = p.todayRuntimeMin;

    // IR AC state — what we last commanded over IR (one-way, not sensed).
    AcState ac = g_state.acState();
    doc["ac_ir_known"] = ac.known;
    if (ac.known) {
        doc["ac_ir_power"] = ac.power;
        doc["ac_ir_mode"] = ir_ac_mode_to_string(ac.mode);
        doc["ac_ir_temp_c"] = ac.temp;
        doc["ac_ir_fan"] = ir_ac_fan_to_string(ac.fan);
        doc["ac_ir_swing"] = ac.swing;
        doc["ac_ir_streamer"] = ac.streamer;
    }

    doc["wifi_rssi_dbm"] = WiFi.RSSI();

    String out;
    serializeJson(doc, out);
    return out;
}

void addTextContent(JsonObject result, const String& text) {
    JsonArray content = result["content"].to<JsonArray>();
    JsonObject c = content.add<JsonObject>();
    c["type"] = "text";
    c["text"] = text;
}

void fillToolsList(JsonObject result) {
    JsonArray tools = result["tools"].to<JsonArray>();

    JsonObject t1 = tools.add<JsonObject>();
    t1["name"] = "get_status";
    t1["description"] =
        "Get the current indoor environment and AC state: temperature (C), "
        "humidity (%), sensor battery; the Tapo P110M smart-plug state (mains "
        "power on/off, current watts, energy today in kWh, runtime); and the "
        "last AC settings sent over IR (ac_ir_power/mode/temp_c/fan).";
    JsonObject s1 = t1["inputSchema"].to<JsonObject>();
    s1["type"] = "object";
    s1["properties"].to<JsonObject>();

    // IR control of the actual Daikin AC (power/mode/temp/fan) — the primary way
    // to run the AC, like using its remote.
    JsonObject t2 = tools.add<JsonObject>();
    t2["name"] = "set_ac";
    t2["description"] =
        "Control the Daikin air conditioner over IR (like its remote). Set any "
        "of: power on/off, mode, temperature, fan speed. Only the fields you "
        "pass change. The Stick must have line-of-sight to the AC. Note: needs "
        "mains power — keep the plug on (see set_plug_power).";
    JsonObject s2 = t2["inputSchema"].to<JsonObject>();
    s2["type"] = "object";
    JsonObject p2 = s2["properties"].to<JsonObject>();
    JsonObject acPower = p2["power"].to<JsonObject>();
    acPower["type"] = "boolean";
    acPower["description"] = "true = turn AC on, false = turn AC off";
    JsonObject acMode = p2["mode"].to<JsonObject>();
    acMode["type"] = "string";
    JsonArray modeEnum = acMode["enum"].to<JsonArray>();
    modeEnum.add("auto"); modeEnum.add("cool"); modeEnum.add("heat");
    modeEnum.add("dry"); modeEnum.add("fan");
    JsonObject acTemp = p2["temp"].to<JsonObject>();
    acTemp["type"] = "integer";
    acTemp["description"] = "target temperature in Celsius (16-30)";
    JsonObject acFan = p2["fan"].to<JsonObject>();
    acFan["type"] = "string";
    JsonArray fanEnum = acFan["enum"].to<JsonArray>();
    fanEnum.add("auto"); fanEnum.add("quiet"); fanEnum.add("low");
    fanEnum.add("medium"); fanEnum.add("high");
    JsonObject acSwing = p2["swing"].to<JsonObject>();
    acSwing["type"] = "boolean";
    acSwing["description"] = "vertical swing (up/down airflow) on/off";
    JsonObject acStreamer = p2["streamer"].to<JsonObject>();
    acStreamer["type"] = "boolean";
    acStreamer["description"] = "Daikin Streamer air purification on/off";

    // Tapo smart-plug mains power (hard cutoff + energy metering).
    JsonObject t3 = tools.add<JsonObject>();
    t3["name"] = "set_plug_power";
    t3["description"] =
        "Switch the Tapo P110M smart plug's mains power (hard on/off). The plug "
        "is normally left ON purely as an energy meter — control the AC itself "
        "with set_ac (IR). Only use this if the user explicitly wants to cut or "
        "restore mains power. Effect within ~1s; confirm with get_status.";
    JsonObject s3 = t3["inputSchema"].to<JsonObject>();
    s3["type"] = "object";
    JsonObject on = s3["properties"].to<JsonObject>()["on"].to<JsonObject>();
    on["type"] = "boolean";
    on["description"] = "true = mains ON, false = mains OFF";
    s3["required"].to<JsonArray>().add("on");
}

// Dispatches tools/call. Returns false if the tool name is unknown (caller
// still sends a result with isError=true).
void handleToolCall(JsonVariant params, JsonObject result) {
    const char* name = params["name"] | "";

    if (strcmp(name, "get_status") == 0) {
        addTextContent(result, buildStatusText());
        return;
    }
    if (strcmp(name, "set_plug_power") == 0) {
        if (params["arguments"]["on"].isNull()) {
            result["isError"] = true;
            addTextContent(result, "Missing required boolean argument 'on'.");
            return;
        }
        bool on = params["arguments"]["on"].as<bool>();
        g_state.requestPlugCommand(on ? PLUG_CMD_ON : PLUG_CMD_OFF);
        addTextContent(result, String("Plug mains power command accepted: ") +
                                   (on ? "ON" : "OFF") +
                                   ". Applies within ~1s; call get_status to confirm.");
        return;
    }
    if (strcmp(name, "set_ac") == 0) {
        JsonVariant a = params["arguments"];
        AcCommand cmd;
        if (!a["power"].isNull()) { cmd.has_power = true; cmd.power = a["power"].as<bool>(); }
        if (!a["temp"].isNull()) { cmd.has_temp = true; cmd.temp = ir_ac_clamp_temp(a["temp"].as<int>()); }
        if (!a["mode"].isNull()) {
            uint8_t m;
            if (!ir_ac_mode_from_string(a["mode"].as<const char*>(), &m)) {
                result["isError"] = true;
                addTextContent(result, "Invalid mode. Use: auto, cool, heat, dry, fan.");
                return;
            }
            cmd.has_mode = true; cmd.mode = m;
        }
        if (!a["fan"].isNull()) {
            uint8_t f;
            if (!ir_ac_fan_from_string(a["fan"].as<const char*>(), &f)) {
                result["isError"] = true;
                addTextContent(result, "Invalid fan. Use: auto, quiet, low, medium, high.");
                return;
            }
            cmd.has_fan = true; cmd.fan = f;
        }
        if (!a["swing"].isNull()) { cmd.has_swing = true; cmd.swing = a["swing"].as<bool>(); }
        if (!a["streamer"].isNull()) { cmd.has_streamer = true; cmd.streamer = a["streamer"].as<bool>(); }

        if (!cmd.has_power && !cmd.has_temp && !cmd.has_mode && !cmd.has_fan &&
            !cmd.has_swing && !cmd.has_streamer) {
            result["isError"] = true;
            addTextContent(result, "Nothing to set. Pass at least one of: power, mode, temp, fan, swing, streamer.");
            return;
        }
        g_state.requestAcCommand(cmd);
        addTextContent(result, "AC IR command sent. Aim the Stick at the AC; call get_status to see the last-sent settings.");
        return;
    }

    result["isError"] = true;
    addTextContent(result, String("Unknown tool: ") + name);
}

void handleMcpBody(AsyncWebServerRequest* request, const char* body) {
    JsonDocument json;
    DeserializationError perr = deserializeJson(json, body);
    if (perr || !json.is<JsonObject>()) {
        sendJson(request, 400,
                 "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,"
                 "\"message\":\"Parse error\"}}");
        return;
    }

    const char* method = json["method"] | "";

    // Notifications (e.g. notifications/initialized) expect no response body.
    if (strncmp(method, "notifications/", 14) == 0) {
        AsyncWebServerResponse* res = request->beginResponse(202, "text/plain", "");
        addCors(res);
        request->send(res);
        return;
    }

    JsonDocument res;
    res["jsonrpc"] = "2.0";
    res["id"] = json["id"];

    if (strcmp(method, "initialize") == 0) {
        JsonObject result = res["result"].to<JsonObject>();
        result["protocolVersion"] = json["params"]["protocolVersion"] | kDefaultProtocol;
        JsonObject caps = result["capabilities"].to<JsonObject>();
        caps["tools"].to<JsonObject>();
        JsonObject info = result["serverInfo"].to<JsonObject>();
        info["name"] = kServerName;
        info["version"] = kServerVersion;
        result["instructions"] =
            "Indoor climate hub. Use get_status to read temperature/humidity and "
            "AC state; use set_ac to control the Daikin AC over IR (power/mode/"
            "temp/fan/swing); set_plug_power only cuts mains (plug is an energy "
            "meter, normally left on).";
    } else if (strcmp(method, "tools/list") == 0) {
        fillToolsList(res["result"].to<JsonObject>());
    } else if (strcmp(method, "tools/call") == 0) {
        handleToolCall(json["params"], res["result"].to<JsonObject>());
    } else if (strcmp(method, "ping") == 0) {
        res["result"].to<JsonObject>();
    } else if (strcmp(method, "resources/list") == 0) {
        res["result"]["resources"].to<JsonArray>();
    } else if (strcmp(method, "resources/templates/list") == 0) {
        res["result"]["resourceTemplates"].to<JsonArray>();
    } else if (strcmp(method, "prompts/list") == 0) {
        res["result"]["prompts"].to<JsonArray>();
    } else {
        JsonObject err = res["error"].to<JsonObject>();
        err["code"] = -32601;
        err["message"] = String("Method not found: ") + method;
    }

    String out;
    serializeJson(res, out);
    sendJson(request, 200, out);
}

// Custom handler for POST /mcp. We cannot use AsyncCallbackJsonWebHandler
// because its canHandle() rejects requests via isHTTP(), which is false
// whenever the Accept header contains "text/event-stream" — exactly what MCP
// Streamable-HTTP clients send. This matches on method+URL only and buffers
// the body itself (stored in request->_tempObject, freed by the library).
class McpHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        return request->method() == HTTP_POST && request->url() == "/mcp";
    }

    bool isRequestHandlerTrivial() const override { return false; }

    void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                    size_t index, size_t total) override {
        if (total == 0 || total > 8192) return; // ignore empty / oversized
        if (index == 0) {
            request->_tempObject = malloc(total + 1);
            if (request->_tempObject) static_cast<char*>(request->_tempObject)[total] = '\0';
        }
        if (request->_tempObject && index + len <= total) {
            memcpy(static_cast<char*>(request->_tempObject) + index, data, len);
        }
    }

    void handleRequest(AsyncWebServerRequest* request) override {
        const char* body = static_cast<const char*>(request->_tempObject);
        handleMcpBody(request, body ? body : "");
    }
};

} // namespace

void mcp_register(AsyncWebServer& server) {
    server.addHandler(new McpHandler());

    server.on("/mcp", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* res = request->beginResponse(204, "text/plain", "");
        addCors(res);
        request->send(res);
    });
    server.on("/mcp", HTTP_GET, [](AsyncWebServerRequest* request) {
        // We don't offer a server-initiated SSE stream.
        AsyncWebServerResponse* res = request->beginResponse(405, "text/plain", "Method Not Allowed");
        addCors(res);
        request->send(res);
    });
}
