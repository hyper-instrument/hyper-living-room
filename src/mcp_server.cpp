#include "mcp_server.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#include "app_config.h"
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
        "Get the current indoor environment and air-conditioner state: "
        "temperature (C), humidity (%), sensor battery, and the Tapo P110M "
        "smart-plug state (connected, on/off, current power in watts, energy "
        "used today in kWh, runtime today in minutes).";
    JsonObject s1 = t1["inputSchema"].to<JsonObject>();
    s1["type"] = "object";
    s1["properties"].to<JsonObject>();

    JsonObject t2 = tools.add<JsonObject>();
    t2["name"] = "set_ac_power";
    t2["description"] =
        "Turn the air conditioner on or off by switching its Tapo P110M smart "
        "plug. Takes effect within about a second; call get_status to confirm.";
    JsonObject s2 = t2["inputSchema"].to<JsonObject>();
    s2["type"] = "object";
    JsonObject props = s2["properties"].to<JsonObject>();
    JsonObject on = props["on"].to<JsonObject>();
    on["type"] = "boolean";
    on["description"] = "true = power ON, false = power OFF";
    JsonArray req = s2["required"].to<JsonArray>();
    req.add("on");
}

// Dispatches tools/call. Returns false if the tool name is unknown (caller
// still sends a result with isError=true).
void handleToolCall(JsonVariant params, JsonObject result) {
    const char* name = params["name"] | "";

    if (strcmp(name, "get_status") == 0) {
        addTextContent(result, buildStatusText());
        return;
    }
    if (strcmp(name, "set_ac_power") == 0) {
        if (params["arguments"]["on"].isNull()) {
            result["isError"] = true;
            addTextContent(result, "Missing required boolean argument 'on'.");
            return;
        }
        bool on = params["arguments"]["on"].as<bool>();
        g_state.requestPlugCommand(on ? PLUG_CMD_ON : PLUG_CMD_OFF);
        addTextContent(result, String("AC power command accepted: ") +
                                   (on ? "ON" : "OFF") +
                                   ". Applies within ~1s; call get_status to confirm.");
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
            "Indoor climate hub. Use get_status to read temperature/humidity "
            "and AC state; use set_ac_power to switch the air conditioner.";
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
