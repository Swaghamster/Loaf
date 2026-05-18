#pragma once

// ============================================================
// BLEKeyboardHost.h
// Loaf Firmware — BLE Central (host) for HID keyboard input
//
// Acts as a BLE Central that scans for, connects to, and
// receives keystrokes from BLE HID keyboards (0x1812).
//
// Thread-safety note: BLE callbacks fire on the BLE task.
// All shared state is guarded by a FreeRTOS mutex.
// ============================================================

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <BLEAdvertisedDevice.h>
#include <functional>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ── HID / BLE UUIDs ──────────────────────────────────────────
static const BLEUUID HID_SERVICE_UUID        ((uint16_t)0x1812);
static const BLEUUID HID_REPORT_CHAR_UUID    ((uint16_t)0x2A4D);
static const BLEUUID BATTERY_SERVICE_UUID    ((uint16_t)0x180F);
static const BLEUUID BATTERY_LEVEL_CHAR_UUID ((uint16_t)0x2A19);

// HID modifier bit-masks (byte 0 of keyboard report)
static constexpr uint8_t HID_MOD_LCTRL  = 0x01;
static constexpr uint8_t HID_MOD_LSHIFT = 0x02;
static constexpr uint8_t HID_MOD_LALT   = 0x04;
static constexpr uint8_t HID_MOD_LGUI   = 0x08;
static constexpr uint8_t HID_MOD_RCTRL  = 0x10;
static constexpr uint8_t HID_MOD_RSHIFT = 0x20;
static constexpr uint8_t HID_MOD_RALT   = 0x40;
static constexpr uint8_t HID_MOD_RGUI   = 0x80;

// Convenience mask: either shift key
static constexpr uint8_t HID_MOD_SHIFT_MASK = HID_MOD_LSHIFT | HID_MOD_RSHIFT;

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
    String name;       ///< Advertised name (may be empty)
    String address;    ///< BLE MAC address as "XX:XX:XX:XX:XX:XX"
    int    rssi;       ///< Signal strength at time of discovery
};

// Path where paired-device credentials are persisted
static constexpr char BT_PAIRED_PATH[] = "/loaf/bt/paired.json";

// ─────────────────────────────────────────────────────────────
// BLEKeyboardHost
// ─────────────────────────────────────────────────────────────
class BLEKeyboardHost {
public:
    // Singleton
    static BLEKeyboardHost& instance();

    // Prevent copy/move
    BLEKeyboardHost(const BLEKeyboardHost&)            = delete;
    BLEKeyboardHost& operator=(const BLEKeyboardHost&) = delete;
    BLEKeyboardHost(BLEKeyboardHost&&)                 = delete;
    BLEKeyboardHost& operator=(BLEKeyboardHost&&)      = delete;

    // ── Lifecycle ─────────────────────────────────────────────

    /// Initialise BLE stack.  Must be called once from setup()
    /// before any other method.  Attempts auto-reconnect if a
    /// paired device was persisted on SD.
    void init();

    // ── Scanning ──────────────────────────────────────────────

    /// Start an active scan for BLE HID devices.
    /// timeoutSecs: scan window (default BLE_SCAN_TIMEOUT_S from config.h).
    /// Results are accumulated in getScanResults().
    void startScan(int timeoutSecs = 15);

    /// Abort an in-progress scan early.
    void stopScan();

    /// Return the list of HID devices found during the last scan.
    std::vector<BLEKBDevice> getScanResults();

    // ── Connection ────────────────────────────────────────────

    /// Connect to the device identified by its BLE address string.
    /// Returns true if the connection and HID report subscription
    /// succeeded.  Blocks briefly; call from a task, not ISR.
    bool connect(const String& address);

    /// Disconnect the current client and return to IDLE.
    void disconnect();

    /// Returns true when a keyboard is actively connected.
    bool isConnected();

    // ── Status ────────────────────────────────────────────────

    BLEKBState getState();
    String     getConnectedDeviceName();

    // ── Persistence ───────────────────────────────────────────

    /// Persist the chosen device to SD (/loaf/bt/paired.json)
    /// so it can be auto-reconnected on next boot.
    void savePairedDevice(const String& address, const String& name);

    /// Load the persisted paired device and attempt to connect.
    /// Returns true if a paired record was found (connection may
    /// still be asynchronous).
    bool loadPairedDevice();

    // ── Input callback ────────────────────────────────────────

    /// Set the callback fired for every key-down / key-up event.
    ///   keycode   – USB HID usage page 0x07 keycode (0x04-0x73)
    ///   modifiers – bitmask (see HID_MOD_* constants above)
    ///   pressed   – true = key down, false = key up
    void setKeyCallback(
        std::function<void(uint8_t keycode, uint8_t modifiers, bool pressed)> cb
    );

    /// Returns the last modifier byte seen in any HID report.
    uint8_t getCurrentModifiers();

    // ── Key translation ───────────────────────────────────────

    /// Convert a USB HID keycode + modifier byte to a printable
    /// ASCII character.  Returns '\0' for non-printable keys.
    static char hidKeyToChar(uint8_t keycode, uint8_t modifiers);

    // ── Internal (public for BLE callback lambdas) ────────────

    /// Called by the BLEScan advertised-device callback.
    void _onAdvertised(BLEAdvertisedDevice* dev);

    /// Called by the BLE client disconnect callback.
    void _onDisconnect();

    /// Called when a HID notify fires on any subscribed
    /// characteristic; parses the 8-byte keyboard report.
    void _onHIDReport(const uint8_t* data, size_t length);

private:
    BLEKeyboardHost() = default;

    // Subscribe to every HID report characteristic on the
    // connected client; returns true if at least one found.
    bool _subscribeToHIDReports(BLEClient* client);

    // Mutex protecting all mutable shared state
    SemaphoreHandle_t _mutex = nullptr;

    BLEKBState               _state          = BLEKBState::BLE_KB_IDLE;
    String                   _connectedName;
    String                   _connectedAddr;
    uint8_t                  _modifiers      = 0;
    uint8_t                  _prevKeycodes[6]{};  // previous report keycodes

    std::vector<BLEKBDevice> _scanResults;

    BLEScan*   _scanner = nullptr;
    BLEClient* _client  = nullptr;

    std::function<void(uint8_t, uint8_t, bool)> _keyCallback;
};
