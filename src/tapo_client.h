#pragma once

#include <Arduino.h>
#include <tapo_protocol.h>

// Local KLAP-protocol client for the Tapo P110M. Unlike the upstream
// TapoDevice class this also exposes get_device_info / get_energy_usage so we
// can show plug state and power consumption. All methods block on HTTP and
// must only be called from the main loop task.
class TapoClient {
public:
    // KLAP handshake with the plug. Safe to call again after failures.
    bool begin(const String& ip, const String& user, const String& pass);

    bool ready() const { return ready_; }

    bool setOn(bool on);

    // Polls device info + energy usage and publishes them into g_state.
    // After repeated failures marks the client not-ready so the caller can
    // re-run begin().
    bool refresh();

private:
    // Sends one request, retrying once after a rehandshake on failure.
    // Returns the raw JSON response or "" on failure.
    String send(const String& method, const String& params);

    TapoProtocol protocol_;
    String ip_, user_, pass_;
    bool ready_ = false;
    int failCount_ = 0;
};
