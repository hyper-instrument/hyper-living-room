#include "wg_tunnel.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WireGuard-ESP32.h>
#include <time.h>

#include "app_config.h"

namespace {
WireGuard g_wg;
WiFiUDP g_udp;
bool g_started = false;
uint32_t g_lastKeepalive = 0;

constexpr uint32_t kKeepaliveMs = 20000;
constexpr uint16_t kKeepalivePort = 53; // any port; only the outbound packet matters

bool timeSynced() {
    return time(nullptr) > 1600000000; // sane epoch => NTP has run
}
} // namespace

void wg_tunnel_service() {
    if (strlen(WG_PRIVATE_KEY) == 0) return; // tunnel disabled

    if (!g_started) {
        if (WiFi.status() != WL_CONNECTED || !timeSynced()) return;
        IPAddress localIp;
        if (!localIp.fromString(WG_LOCAL_IP)) {
            Serial.println("WG: invalid WG_LOCAL_IP");
            g_started = true; // don't retry a bad config every loop
            return;
        }
        g_started = g_wg.begin(localIp, WG_PRIVATE_KEY, WG_ENDPOINT,
                               WG_PEER_PUBLIC_KEY, (uint16_t)atoi(WG_PORT));
        Serial.printf("WG: tunnel %s (local %s -> %s:%s)\n",
                      g_started ? "up" : "FAILED", WG_LOCAL_IP, WG_ENDPOINT, WG_PORT);
        g_lastKeepalive = millis();
        return;
    }

    // NAT/session keepalive: one tiny UDP packet to the server's tunnel IP.
    if (g_wg.is_initialized() && millis() - g_lastKeepalive >= kKeepaliveMs) {
        g_lastKeepalive = millis();
        if (g_udp.beginPacket(IPAddress(10, 6, 0, 1), kKeepalivePort)) {
            g_udp.write((uint8_t)0);
            g_udp.endPacket();
        }
    }
}
