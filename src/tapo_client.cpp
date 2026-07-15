#include "tapo_client.h"

#include <ArduinoJson.h>
#include <time.h>

#include "state.h"

namespace {

constexpr int kMaxFailsBeforeReset = 3;

uint64_t requestTimeMillis() {
    time_t sec = time(nullptr);
    if (sec <= 100000) return (uint64_t)millis(); // NTP not synced yet
    return (uint64_t)sec * 1000ULL + (uint64_t)(millis() % 1000);
}

} // namespace

bool TapoClient::begin(const String& ip, const String& user, const String& pass) {
    ip_ = ip;
    user_ = user;
    pass_ = pass;
    failCount_ = 0;
    ready_ = protocol_.handshake(ip, user, pass);
    if (ready_) {
        Serial.println("TAPO: connected to " + ip);
    }
    return ready_;
}

String TapoClient::send(const String& method, const String& params) {
    if (!ready_) return "";

    String msg = "{\"method\":\"" + method + "\",\"params\":" + params +
                 ",\"requestTimeMilis\":" + String(requestTimeMillis()) +
                 ",\"terminalUUID\":\"00-00-00-00-00-00\"}";

    for (int attempt = 0; attempt < 2; attempt++) {
        String resp = protocol_.send_message(msg);
        if (resp.length()) return resp;
        protocol_.rehandshake(); // session likely expired
    }
    return "";
}

bool TapoClient::setOn(bool on) {
    String params = String("{\"device_on\":") + (on ? "true" : "false") + "}";
    String resp = send("set_device_info", params);
    bool ok = resp.indexOf("\"error_code\":0") != -1;
    Serial.printf("TAPO: set %s -> %s\n", on ? "ON" : "OFF", ok ? "ok" : "FAILED");
    return ok;
}

bool TapoClient::refresh() {
    PlugData p = g_state.plug();
    bool ok = false;

    String info = send("get_device_info", "{}");
    if (info.length()) {
        JsonDocument doc;
        if (!deserializeJson(doc, info) && doc["error_code"] == 0) {
            p.on = doc["result"]["device_on"] | false;
            p.known = true;
            p.updatedMs = millis();
            ok = true;
        }
    }

    String energy = send("get_energy_usage", "{}");
    if (energy.length()) {
        JsonDocument doc;
        if (!deserializeJson(doc, energy) && doc["error_code"] == 0) {
            JsonObject r = doc["result"];
            p.powerW = (r["current_power"] | 0L) / 1000.0f;  // mW -> W
            p.todayKWh = (r["today_energy"] | 0L) / 1000.0f; // Wh -> kWh
            p.todayRuntimeMin = r["today_runtime"] | -1;
        }
    }

    if (ok) {
        failCount_ = 0;
        g_state.setPlug(p);
    } else if (++failCount_ >= kMaxFailsBeforeReset) {
        Serial.println("TAPO: too many failures, will re-handshake");
        ready_ = false;
    }
    return ok;
}
