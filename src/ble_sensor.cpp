#include "ble_sensor.h"

#include <NimBLEDevice.h>
#include <set>
#include <string>

#include "app_config.h"
#include "state.h"

namespace {

constexpr uint16_t kXiaomiSvcUuid = 0xFE95;
constexpr uint16_t kProductLYWSDCGQ = 0x01AA; // MJ_HT_V1, round e-ink sensor
constexpr uint32_t kScanDurationMs = 15000;

// MiBeacon frame-control flags
constexpr uint16_t kFcEncrypted = 0x0008;
constexpr uint16_t kFcHasMac = 0x0010;
constexpr uint16_t kFcHasCapability = 0x0020;
constexpr uint16_t kFcHasObject = 0x0040;

std::set<std::string> g_seenMacs; // for one-time discovery logging
volatile bool g_scanPaused = false;

String formatMac(const uint8_t* macLsbFirst) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             macLsbFirst[5], macLsbFirst[4], macLsbFirst[3],
             macLsbFirst[2], macLsbFirst[1], macLsbFirst[0]);
    return String(buf);
}

// Parses one MiBeacon service-data payload. Returns true if a reading from
// the configured (or matching) sensor was extracted and published.
void parseMiBeacon(const uint8_t* d, size_t len) {
    if (len < 5) return;

    uint16_t frameCtrl = d[0] | (d[1] << 8);
    uint16_t productId = d[2] | (d[3] << 8);

    if (frameCtrl & kFcEncrypted) return; // LYWSDCGQ broadcasts in the clear
    if (!(frameCtrl & kFcHasMac)) return;

    size_t off = 5;
    if (off + 6 > len) return;
    String mac = formatMac(d + off);
    off += 6;

    // Log each Xiaomi device once so the user can pin SENSOR_MAC.
    if (g_seenMacs.insert(std::string(mac.c_str())).second) {
        Serial.printf("BLE: Xiaomi device found mac=%s product=0x%04X%s\n",
                      mac.c_str(), productId,
                      productId == kProductLYWSDCGQ ? " (LYWSDCGQ)" : "");
    }

    String wantedMac = SENSOR_MAC;
    wantedMac.toLowerCase();
    if (wantedMac.length() > 0) {
        if (mac != wantedMac) return;
    } else if (productId != kProductLYWSDCGQ) {
        return;
    }

    if (frameCtrl & kFcHasCapability) off += 1;
    if (!(frameCtrl & kFcHasObject)) return;

    SensorData s = g_state.sensor();
    bool gotData = false;

    while (off + 3 <= len) {
        uint16_t objId = d[off] | (d[off + 1] << 8);
        uint8_t objLen = d[off + 2];
        off += 3;
        if (off + objLen > len) break;
        const uint8_t* v = d + off;

        switch (objId) {
        case 0x1004: // temperature, int16 LE, 0.1 degC
            if (objLen >= 2) {
                s.tempC = (int16_t)(v[0] | (v[1] << 8)) / 10.0f;
                gotData = true;
            }
            break;
        case 0x1006: // humidity, uint16 LE, 0.1 %
            if (objLen >= 2) {
                s.humPct = (uint16_t)(v[0] | (v[1] << 8)) / 10.0f;
                gotData = true;
            }
            break;
        case 0x100D: // temperature + humidity
            if (objLen >= 4) {
                s.tempC = (int16_t)(v[0] | (v[1] << 8)) / 10.0f;
                s.humPct = (uint16_t)(v[2] | (v[3] << 8)) / 10.0f;
                gotData = true;
            }
            break;
        case 0x100A: // battery, uint8, %
            if (objLen >= 1) {
                s.battPct = v[0];
                gotData = true;
            }
            break;
        default:
            break;
        }
        off += objLen;
    }

    if (gotData) {
        s.updatedMs = millis();
        strlcpy(s.mac, mac.c_str(), sizeof(s.mac));
        g_state.setSensor(s);
    }
}

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (!dev->haveServiceData()) return;
        uint8_t count = dev->getServiceDataCount();
        for (uint8_t i = 0; i < count; i++) {
            if (dev->getServiceDataUUID(i) != NimBLEUUID(kXiaomiSvcUuid)) continue;
            std::string data = dev->getServiceData(i);
            parseMiBeacon(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        }
    }

    // Restart indefinitely; finite scans clear the duplicate cache so we keep
    // receiving fresh readings. Skip the restart while paused (IR sending).
    void onScanEnd(const NimBLEScanResults&, int) override {
        if (g_scanPaused) return;
        NimBLEDevice::getScan()->start(kScanDurationMs, false, true);
    }
};

ScanCallbacks g_scanCallbacks;

} // namespace

void ble_sensor_start() {
    NimBLEDevice::init("");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCallbacks, false);
    scan->setActiveScan(false);   // MiBeacon data is in the advertisement itself
    scan->setDuplicateFilter(false);
    // Low duty cycle so BLE plays nice with Wi-Fi on the shared radio.
    scan->setInterval(300);
    scan->setWindow(60);
    scan->start(kScanDurationMs, false, true);
    Serial.println("BLE: passive scan started");
}

void ble_sensor_pause() {
    g_scanPaused = true;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
}

void ble_sensor_resume() {
    g_scanPaused = false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan && !scan->isScanning()) scan->start(kScanDurationMs, false, true);
}
