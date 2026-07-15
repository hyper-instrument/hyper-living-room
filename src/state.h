#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Latest reading from the LYWSDCGQ sensor (written by the NimBLE host task).
struct SensorData {
    float tempC = NAN;
    float humPct = NAN;
    int battPct = -1;
    uint32_t updatedMs = 0;   // millis() of last accepted advertisement
    char mac[18] = "";
};

// Latest known state of the Tapo P110M (written by the main loop task).
struct PlugData {
    bool known = false;       // at least one successful poll
    bool on = false;
    float powerW = NAN;       // current power draw
    float todayKWh = NAN;     // energy used today
    int todayRuntimeMin = -1; // plug-on runtime today
    uint32_t updatedMs = 0;
};

// Pending plug command, set by web UI / button, consumed by the main loop.
enum PlugCommand : int {
    PLUG_CMD_NONE = -1,
    PLUG_CMD_OFF = 0,
    PLUG_CMD_ON = 1,
    PLUG_CMD_TOGGLE = 2,
};

// Shared between the main loop, the NimBLE host task (BLE callbacks) and the
// async_tcp task (web server handlers). All access goes through the mutex.
class AppState {
public:
    void begin() { mtx_ = xSemaphoreCreateMutex(); }

    SensorData sensor() const {
        lock();
        SensorData s = sensor_;
        unlock();
        return s;
    }

    void setSensor(const SensorData& s) {
        lock();
        sensor_ = s;
        unlock();
    }

    PlugData plug() const {
        lock();
        PlugData p = plug_;
        unlock();
        return p;
    }

    void setPlug(const PlugData& p) {
        lock();
        plug_ = p;
        unlock();
    }

    void requestPlugCommand(PlugCommand cmd) {
        lock();
        pending_ = cmd;
        unlock();
    }

    PlugCommand takePendingPlugCommand() {
        lock();
        PlugCommand cmd = pending_;
        pending_ = PLUG_CMD_NONE;
        unlock();
        return cmd;
    }

private:
    void lock() const { xSemaphoreTake(mtx_, portMAX_DELAY); }
    void unlock() const { xSemaphoreGive(mtx_); }

    mutable SemaphoreHandle_t mtx_ = nullptr;
    SensorData sensor_;
    PlugData plug_;
    PlugCommand pending_ = PLUG_CMD_NONE;
};

extern AppState g_state;
