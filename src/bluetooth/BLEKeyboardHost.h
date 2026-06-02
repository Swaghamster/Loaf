#pragma once

// ============================================================
// BLEKeyboardHost.h — NimBLE rewrite for ESP32-C3
//
// ESP32-C3 uses NimBLE (not Bluedroid).  This file replaces
// all BLE* types with NimBLE* types; the public API surface
// is unchanged so callers (TerminalApp, NotesApp) need no edits.
// ============================================================

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <functional>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ── HID / BLE UUIDs ──────────────────────────────────────────
static const NimBLEUUID HID_SERVICE_UUID      ((uint16_t)0x1812);
static const NimBLEUUID HID_REPORT_CHAR_UUID  ((uint16_t)0x2A4D);

// HID modifier bit-masks (byte 0 of keyboard report)
static constexpr uint8_t HID_MOD_LCTRL       = 0x01;
static constexpr uint8_t HID_MOD_LSHIFT      = 0x02;
static constexpr uint8_t HID_MOD_LALT        = 0x04;
static constexpr uint8_t HID_MOD_LGUI        = 0x08;
static constexpr uint8_t HID_MOD_RCTRL       = 0x10;
static constexpr uint8_t HID_MOD_RSHIFT      = 0x20;
static constexpr uint8_t HID_MOD_RALT        = 0x40;
static constexpr uint8_t HID_MOD_RGUI        = 0x80;
static constexpr uint8_t HID_MOD_SHIFT_MASK  = HID_MOD_LSHIFT | HID_MOD_RSHIFT;

// ── State machine ─────────────────────────────────────────────
enum class BLEKBState : uint8_t {
    BLE_KB_IDLE,
    BLE_KB_SCANNING,
    BLE_KB_CONNECTING,
    BLE_KB_CONNECTED,
    BLE_KB_DISCONNECTED,
};

// ── Discovered device descriptor ─────────────────────────────
struct BLEKBDevice {
    String name;
    String address;
    int    rssi;
};

static constexpr char BT_PAIRED_PATH[] = "/loaf/bt/paired.json";

// ─────────────────────────────────────────────────────────────
// BLEKeyboardHost
// ─────────────────────────────────────────────────────────────
class BLEKeyboardHost {
public:
    static BLEKeyboardHost& instance();

    BLEKeyboardHost(const BLEKeyboardHost&)            = delete;
    BLEKeyboardHost& operator=(const BLEKeyboardHost&) = delete;
    BLEKeyboardHost(BLEKeyboardHost&&)                 = delete;
    BLEKeyboardHost& operator=(BLEKeyboardHost&&)      = delete;

    void init();

    void startScan(int timeoutSecs = 15);
    void stopScan();
    std::vector<BLEKBDevice> getScanResults();

    bool connect(const String& address);
    void disconnect();
    bool isConnected();

    BLEKBState getState();
    String     getConnectedDeviceName();

    void savePairedDevice(const String& address, const String& name);
    bool loadPairedDevice();

    void    setKeyCallback(std::function<void(uint8_t, uint8_t, bool)> cb);
    uint8_t getCurrentModifiers();
    static char hidKeyToChar(uint8_t keycode, uint8_t modifiers);

    // Called by NimBLE callbacks (must be public)
    void _onAdvertised(NimBLEAdvertisedDevice* dev);
    void _onDisconnect();
    void _onHIDReport(const uint8_t* data, size_t length);
    void _onScanEnd();

private:
    BLEKeyboardHost() = default;

    bool _subscribeToHIDReports(NimBLEClient* client);

    SemaphoreHandle_t _mutex = nullptr;

    BLEKBState               _state = BLEKBState::BLE_KB_IDLE;
    String                   _connectedName;
    String                   _connectedAddr;
    uint8_t                  _modifiers = 0;
    uint8_t                  _prevKeycodes[6]{};

    std::vector<BLEKBDevice> _scanResults;

    NimBLEScan*   _scanner = nullptr;
    NimBLEClient* _client  = nullptr;

    std::function<void(uint8_t, uint8_t, bool)> _keyCallback;
};
