// ============================================================
// BLEKeyboardHost.cpp
// Loaf Firmware — BLE Central (host) for HID keyboard input
// ============================================================

#include "BLEKeyboardHost.h"
#include "../storage/FileManager.h"
#include <ArduinoJson.h>

// ── Singleton ─────────────────────────────────────────────────
BLEKeyboardHost& BLEKeyboardHost::instance() {
    static BLEKeyboardHost inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────
// BLE Scan Callback
// Filters advertised devices to those that include the HID
// service UUID (0x1812) in their advertisement payload.
// ─────────────────────────────────────────────────────────────
class HIDScanCallback : public BLEAdvertisedDeviceCallbacks {
public:
    explicit HIDScanCallback(BLEKeyboardHost* host) : _host(host) {}

    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (advertisedDevice.haveServiceUUID() &&
            advertisedDevice.isAdvertisingService(HID_SERVICE_UUID)) {
            _host->_onAdvertised(&advertisedDevice);
        }
    }

private:
    BLEKeyboardHost* _host;
};

// ─────────────────────────────────────────────────────────────
// BLE Client Callbacks
// ─────────────────────────────────────────────────────────────
class HIDClientCallback : public BLEClientCallbacks {
public:
    explicit HIDClientCallback(BLEKeyboardHost* host) : _host(host) {}

    void onConnect(BLEClient* /*client*/) override {
        // State already set to CONNECTED by connect()
    }

    void onDisconnect(BLEClient* /*client*/) override {
        _host->_onDisconnect();
    }

private:
    BLEKeyboardHost* _host;
};

// ─────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────
void BLEKeyboardHost::init() {
    _mutex = xSemaphoreCreateMutex();
    configASSERT(_mutex);

    BLEDevice::init(BLE_DEVICE_NAME);
    _scanner = BLEDevice::getScan();
    _scanner->setActiveScan(true);
    _scanner->setInterval(100);
    _scanner->setWindow(99);

    // Attempt to reconnect to persisted paired keyboard
    loadPairedDevice();
}

// ─────────────────────────────────────────────────────────────
// Scanning
// ─────────────────────────────────────────────────────────────
void BLEKeyboardHost::startScan(int timeoutSecs) {
    {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _scanResults.clear();
            _state = BLEKBState::BLE_KB_SCANNING;
            xSemaphoreGive(_mutex);
        }
    }

    _scanner->setAdvertisedDeviceCallbacks(new HIDScanCallback(this), true);
    // Non-blocking: BLE stack fires _onAdvertised() for each result,
    // and transitions state to IDLE when the timeout expires.
    _scanner->start(timeoutSecs, [](BLEScanResults /*results*/) {
        // Scan complete — return to IDLE if still scanning
        BLEKeyboardHost& kb = BLEKeyboardHost::instance();
        if (xSemaphoreTake(kb._mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (kb._state == BLEKBState::BLE_KB_SCANNING) {
                kb._state = BLEKBState::BLE_KB_IDLE;
            }
            xSemaphoreGive(kb._mutex);
        }
    }, false);
}

void BLEKeyboardHost::stopScan() {
    _scanner->stop();
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (_state == BLEKBState::BLE_KB_SCANNING) {
            _state = BLEKBState::BLE_KB_IDLE;
        }
        xSemaphoreGive(_mutex);
    }
}

std::vector<BLEKBDevice> BLEKeyboardHost::getScanResults() {
    std::vector<BLEKBDevice> copy;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        copy = _scanResults;
        xSemaphoreGive(_mutex);
    }
    return copy;
}

// ─────────────────────────────────────────────────────────────
// Connection
// ─────────────────────────────────────────────────────────────
bool BLEKeyboardHost::connect(const String& address) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _state = BLEKBState::BLE_KB_CONNECTING;
        xSemaphoreGive(_mutex);
    }

    // Clean up any previous client
    if (_client) {
        if (_client->isConnected()) {
            _client->disconnect();
        }
        BLEDevice::deleteClient(_client);
        _client = nullptr;
    }

    BLEAddress bleAddr(address.c_str());
    _client = BLEDevice::createClient();
    _client->setClientCallbacks(new HIDClientCallback(this));

    if (!_client->connect(bleAddr)) {
        Serial.println("[BLEKeyboardHost] connect() failed");
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _state = BLEKBState::BLE_KB_DISCONNECTED;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    // Negotiate MTU
    _client->setMTU(517);

    if (!_subscribeToHIDReports(_client)) {
        Serial.println("[BLEKeyboardHost] HID service/char not found");
        _client->disconnect();
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _state = BLEKBState::BLE_KB_DISCONNECTED;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    // Determine the device name from scan results
    String foundName;
    {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (const auto& dev : _scanResults) {
                if (dev.address.equalsIgnoreCase(address)) {
                    foundName = dev.name;
                    break;
                }
            }
            xSemaphoreGive(_mutex);
        }
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _state         = BLEKBState::BLE_KB_CONNECTED;
        _connectedAddr = address;
        _connectedName = foundName.isEmpty() ? address : foundName;
        xSemaphoreGive(_mutex);
    }

    Serial.printf("[BLEKeyboardHost] Connected to %s (%s)\n",
                  _connectedName.c_str(), address.c_str());
    return true;
}

void BLEKeyboardHost::disconnect() {
    if (_client && _client->isConnected()) {
        _client->disconnect();
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _state = BLEKBState::BLE_KB_IDLE;
        xSemaphoreGive(_mutex);
    }
}

bool BLEKeyboardHost::isConnected() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool c = (_state == BLEKBState::BLE_KB_CONNECTED);
        xSemaphoreGive(_mutex);
        return c;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// Status
// ─────────────────────────────────────────────────────────────
BLEKBState BLEKeyboardHost::getState() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        BLEKBState s = _state;
        xSemaphoreGive(_mutex);
        return s;
    }
    return BLEKBState::BLE_KB_IDLE;
}

String BLEKeyboardHost::getConnectedDeviceName() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        String n = _connectedName;
        xSemaphoreGive(_mutex);
        return n;
    }
    return String();
}

// ─────────────────────────────────────────────────────────────
// Persistence
// ─────────────────────────────────────────────────────────────
void BLEKeyboardHost::savePairedDevice(const String& address, const String& name) {
    FileManager::instance().makeDir("/loaf/bt");
    JsonDocument doc;
    doc["address"] = address;
    doc["name"]    = name;
    String json;
    serializeJson(doc, json);
    FileManager::instance().writeFile(BT_PAIRED_PATH, json);
    Serial.printf("[BLEKeyboardHost] Saved paired device: %s\n", address.c_str());
}

bool BLEKeyboardHost::loadPairedDevice() {
    String json;
    if (!FileManager::instance().readFile(BT_PAIRED_PATH, json)) {
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        return false;
    }
    String addr = doc["address"].as<String>();
    String name = doc["name"].as<String>();
    if (addr.isEmpty()) return false;

    Serial.printf("[BLEKeyboardHost] Auto-reconnecting to %s (%s)\n",
                  name.c_str(), addr.c_str());
    // Attempt reconnect (non-blocking: kick off scan then connect)
    // We scan briefly first so the device can be rediscovered
    startScan(5);
    // connect() is called by the app layer after scan completes
    // Store address for deferred auto-connect
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _connectedAddr = addr;
        _connectedName = name;
        xSemaphoreGive(_mutex);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
// Input callback registration
// ─────────────────────────────────────────────────────────────
void BLEKeyboardHost::setKeyCallback(
    std::function<void(uint8_t, uint8_t, bool)> cb)
{
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _keyCallback = cb;
        xSemaphoreGive(_mutex);
    }
}

uint8_t BLEKeyboardHost::getCurrentModifiers() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        uint8_t m = _modifiers;
        xSemaphoreGive(_mutex);
        return m;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
// Internal: subscribe to HID report characteristics
// ─────────────────────────────────────────────────────────────
bool BLEKeyboardHost::_subscribeToHIDReports(BLEClient* client) {
    BLERemoteService* hidSvc = client->getService(HID_SERVICE_UUID);
    if (!hidSvc) {
        Serial.println("[BLEKeyboardHost] HID service not found");
        return false;
    }

    // There may be multiple HID Report characteristics (one per report ID).
    // Iterate and subscribe to all of them.
    std::map<uint16_t, BLERemoteCharacteristic*>* chars =
        hidSvc->getCharacteristics();
    if (!chars) {
        Serial.println("[BLEKeyboardHost] No characteristics in HID service");
        return false;
    }

    int subscribed = 0;
    for (auto& kv : *chars) {
        BLERemoteCharacteristic* ch = kv.second;
        if (ch->getUUID().equals(HID_REPORT_CHAR_UUID)) {
            if (ch->canNotify()) {
                ch->registerForNotify([](BLERemoteCharacteristic* /*c*/,
                                         uint8_t* data, size_t len, bool /*isNotify*/) {
                    BLEKeyboardHost::instance()._onHIDReport(data, len);
                });
                ++subscribed;
                Serial.printf("[BLEKeyboardHost] Subscribed to HID char handle 0x%04X\n",
                              ch->getHandle());
            }
        }
    }
    return subscribed > 0;
}

// ─────────────────────────────────────────────────────────────
// Internal: BLE advertised-device callback
// ─────────────────────────────────────────────────────────────
void BLEKeyboardHost::_onAdvertised(BLEAdvertisedDevice* dev) {
    BLEKBDevice kbDev;
    kbDev.address = String(dev->getAddress().toString().c_str());
    kbDev.name    = dev->haveName()
                    ? String(dev->getName().c_str())
                    : String("HID Device");
    kbDev.rssi    = dev->getRSSI();

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // De-duplicate by address
        bool found = false;
        for (const auto& existing : _scanResults) {
            if (existing.address.equalsIgnoreCase(kbDev.address)) {
                found = true;
                break;
            }
        }
        if (!found) {
            _scanResults.push_back(kbDev);
            Serial.printf("[BLEKeyboardHost] Found HID device: %s (%s) RSSI=%d\n",
                          kbDev.name.c_str(), kbDev.address.c_str(), kbDev.rssi);
        }
        xSemaphoreGive(_mutex);
    }
}

// ─────────────────────────────────────────────────────────────
// Internal: disconnect callback
// ─────────────────────────────────────────────────────────────
void BLEKeyboardHost::_onDisconnect() {
    Serial.println("[BLEKeyboardHost] Keyboard disconnected");
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _state     = BLEKBState::BLE_KB_DISCONNECTED;
        _modifiers = 0;
        memset(_prevKeycodes, 0, sizeof(_prevKeycodes));
        xSemaphoreGive(_mutex);
    }
}

// ─────────────────────────────────────────────────────────────
// Internal: HID report parser
//
// USB HID keyboard report (8 bytes):
//   [0] modifiers  [1] reserved  [2..7] up to 6 simultaneous keycodes
//
// We diff against the previous report to generate key-down /
// key-up events for each changed keycode slot.
// ─────────────────────────────────────────────────────────────
void BLEKeyboardHost::_onHIDReport(const uint8_t* data, size_t length) {
    if (length < 2) return;

    const uint8_t modifiers = data[0];
    // Collect current keycodes (bytes 2-7)
    uint8_t currCodes[6]{};
    const size_t klen = (length >= 8) ? 6 : (length - 2);
    for (size_t i = 0; i < klen; ++i) {
        currCodes[i] = data[2 + i];
    }

    std::function<void(uint8_t, uint8_t, bool)> cb;
    uint8_t prevCodes[6]{};
    {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _modifiers = modifiers;
            memcpy(prevCodes, _prevKeycodes, 6);
            memcpy(_prevKeycodes, currCodes, 6);
            cb = _keyCallback;
            xSemaphoreGive(_mutex);
        }
    }

    if (!cb) return;

    // Detect key-up: was in prev, not in curr
    for (int i = 0; i < 6; ++i) {
        if (prevCodes[i] == 0) continue;
        bool stillHeld = false;
        for (int j = 0; j < 6; ++j) {
            if (currCodes[j] == prevCodes[i]) { stillHeld = true; break; }
        }
        if (!stillHeld) {
            cb(prevCodes[i], modifiers, false);  // key up
        }
    }

    // Detect key-down: in curr, not in prev
    for (int i = 0; i < 6; ++i) {
        if (currCodes[i] == 0) continue;
        bool isNew = true;
        for (int j = 0; j < 6; ++j) {
            if (prevCodes[j] == currCodes[i]) { isNew = false; break; }
        }
        if (isNew) {
            cb(currCodes[i], modifiers, true);   // key down
        }
    }
}

// ─────────────────────────────────────────────────────────────
// HID keycode → ASCII translation
// Covers USB HID Usage Page 0x07 (keyboard/keypad) for the
// standard QWERTY US layout.
// ─────────────────────────────────────────────────────────────
char BLEKeyboardHost::hidKeyToChar(uint8_t keycode, uint8_t modifiers) {
    const bool shift = (modifiers & HID_MOD_SHIFT_MASK) != 0;

    // ── Letters (0x04 = 'a' … 0x1D = 'z') ───────────────────
    if (keycode >= 0x04 && keycode <= 0x1D) {
        char base = static_cast<char>('a' + (keycode - 0x04));
        return shift ? static_cast<char>(base - 32) : base;  // toupper
    }

    // ── Digits & symbol row ───────────────────────────────────
    if (!shift) {
        switch (keycode) {
            case 0x1E: return '1';
            case 0x1F: return '2';
            case 0x20: return '3';
            case 0x21: return '4';
            case 0x22: return '5';
            case 0x23: return '6';
            case 0x24: return '7';
            case 0x25: return '8';
            case 0x26: return '9';
            case 0x27: return '0';
            case 0x2C: return ' ';
            case 0x28: return '\n';   // Enter
            case 0x2A: return '\b';   // Backspace
            case 0x2B: return '\t';   // Tab
            case 0x2D: return '-';
            case 0x2E: return '=';
            case 0x2F: return '[';
            case 0x30: return ']';
            case 0x31: return '\\';
            case 0x33: return ';';
            case 0x34: return '\'';
            case 0x35: return '`';
            case 0x36: return ',';
            case 0x37: return '.';
            case 0x38: return '/';
            default:   return '\0';
        }
    } else {
        // Shifted symbol row
        switch (keycode) {
            case 0x1E: return '!';
            case 0x1F: return '@';
            case 0x20: return '#';
            case 0x21: return '$';
            case 0x22: return '%';
            case 0x23: return '^';
            case 0x24: return '&';
            case 0x25: return '*';
            case 0x26: return '(';
            case 0x27: return ')';
            case 0x2C: return ' ';
            case 0x28: return '\n';
            case 0x2A: return '\b';
            case 0x2B: return '\t';
            case 0x2D: return '_';
            case 0x2E: return '+';
            case 0x2F: return '{';
            case 0x30: return '}';
            case 0x31: return '|';
            case 0x33: return ':';
            case 0x34: return '"';
            case 0x35: return '~';
            case 0x36: return '<';
            case 0x37: return '>';
            case 0x38: return '?';
            default:   return '\0';
        }
    }
}
