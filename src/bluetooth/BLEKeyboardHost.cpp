// ============================================================
// BLEKeyboardHost.cpp — NimBLE rewrite for ESP32-C3
// ============================================================

#include "BLEKeyboardHost.h"
#include "../storage/FileManager.h"
#include <ArduinoJson.h>

// ── NimBLE scan callbacks ─────────────────────────────────────
class HIDScanCallback : public NimBLEAdvertisedDeviceCallbacks {
public:
    explicit HIDScanCallback(BLEKeyboardHost* host) : _host(host) {}

    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (dev->haveServiceUUID() &&
            dev->isAdvertisingService(HID_SERVICE_UUID)) {
            _host->_onAdvertised(dev);
        }
    }

    // Called when the scan window expires
    void onScanEnd(NimBLEScanResults /*results*/) override {
        _host->_onScanEnd();
    }

private:
    BLEKeyboardHost* _host;
};

// ── NimBLE client callbacks ───────────────────────────────────
class HIDClientCallback : public NimBLEClientCallbacks {
public:
    explicit HIDClientCallback(BLEKeyboardHost* host) : _host(host) {}

    void onConnect(NimBLEClient* /*client*/) override {}

    void onDisconnect(NimBLEClient* /*client*/, int /*reason*/) override {
        _host->_onDisconnect();
    }

private:
    BLEKeyboardHost* _host;
};

// ── Singleton ─────────────────────────────────────────────────
BLEKeyboardHost& BLEKeyboardHost::instance() {
    static BLEKeyboardHost inst;
    return inst;
}

// ── Lifecycle ─────────────────────────────────────────────────
void BLEKeyboardHost::init() {
    _mutex = xSemaphoreCreateMutex();
    configASSERT(_mutex);

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setMTU(517);

    _scanner = NimBLEDevice::getScan();
    _scanner->setAdvertisedDeviceCallbacks(new HIDScanCallback(this), true);
    _scanner->setActiveScan(true);
    _scanner->setInterval(100);
    _scanner->setWindow(99);

    loadPairedDevice();
}

// ── Scanning ──────────────────────────────────────────────────
void BLEKeyboardHost::startScan(int timeoutSecs) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _scanResults.clear();
        _state = BLEKBState::BLE_KB_SCANNING;
        xSemaphoreGive(_mutex);
    }
    _scanner->start(timeoutSecs, false);
}

void BLEKeyboardHost::stopScan() {
    _scanner->stop();
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (_state == BLEKBState::BLE_KB_SCANNING)
            _state = BLEKBState::BLE_KB_IDLE;
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

// ── Connection ────────────────────────────────────────────────
bool BLEKeyboardHost::connect(const String& address) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _state = BLEKBState::BLE_KB_CONNECTING;
        xSemaphoreGive(_mutex);
    }

    if (_client) {
        if (_client->isConnected()) _client->disconnect();
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
    }

    _client = NimBLEDevice::createClient();
    _client->setClientCallbacks(new HIDClientCallback(this), false);

    NimBLEAddress bleAddr(address.c_str());
    if (!_client->connect(bleAddr)) {
        Serial.println("[BLEKeyboardHost] connect() failed");
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _state = BLEKBState::BLE_KB_DISCONNECTED;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    if (!_subscribeToHIDReports(_client)) {
        Serial.println("[BLEKeyboardHost] HID service/char not found");
        _client->disconnect();
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _state = BLEKBState::BLE_KB_DISCONNECTED;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    String foundName;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (const auto& dev : _scanResults) {
            if (dev.address.equalsIgnoreCase(address)) {
                foundName = dev.name;
                break;
            }
        }
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
    if (_client && _client->isConnected()) _client->disconnect();
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

// ── Status ────────────────────────────────────────────────────
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

// ── Persistence ───────────────────────────────────────────────
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
    if (!FileManager::instance().readFile(BT_PAIRED_PATH, json)) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
    String addr = doc["address"].as<String>();
    String name = doc["name"].as<String>();
    if (addr.isEmpty()) return false;

    Serial.printf("[BLEKeyboardHost] Auto-reconnecting to %s (%s)\n",
                  name.c_str(), addr.c_str());
    startScan(5);
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _connectedAddr = addr;
        _connectedName = name;
        xSemaphoreGive(_mutex);
    }
    return true;
}

// ── Input callback ────────────────────────────────────────────
void BLEKeyboardHost::setKeyCallback(std::function<void(uint8_t, uint8_t, bool)> cb) {
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

// ── Internal: subscribe to HID report characteristics ─────────
bool BLEKeyboardHost::_subscribeToHIDReports(NimBLEClient* client) {
    NimBLERemoteService* hidSvc = client->getService(HID_SERVICE_UUID);
    if (!hidSvc) {
        Serial.println("[BLEKeyboardHost] HID service not found");
        return false;
    }

    // Get all characteristics (refresh = true forces a full read from device)
    std::vector<NimBLERemoteCharacteristic*>* chars = hidSvc->getCharacteristics(true);
    if (!chars || chars->empty()) {
        Serial.println("[BLEKeyboardHost] No characteristics in HID service");
        return false;
    }

    int subscribed = 0;
    for (auto* ch : *chars) {
        if (ch->getUUID() == HID_REPORT_CHAR_UUID && ch->canNotify()) {
            ch->subscribe(true,
                [](NimBLERemoteCharacteristic* /*c*/,
                   uint8_t* data, size_t len, bool /*isNotify*/) {
                    BLEKeyboardHost::instance()._onHIDReport(data, len);
                });
            ++subscribed;
            Serial.printf("[BLEKeyboardHost] Subscribed to HID char 0x%04X\n",
                          ch->getHandle());
        }
    }
    return subscribed > 0;
}

// ── Internal: callbacks ───────────────────────────────────────
void BLEKeyboardHost::_onAdvertised(NimBLEAdvertisedDevice* dev) {
    BLEKBDevice kbDev;
    kbDev.address = String(dev->getAddress().toString().c_str());
    kbDev.name    = dev->haveName()
                    ? String(dev->getName().c_str()) : String("HID Device");
    kbDev.rssi    = dev->getRSSI();

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool found = false;
        for (const auto& e : _scanResults) {
            if (e.address.equalsIgnoreCase(kbDev.address)) { found = true; break; }
        }
        if (!found) {
            _scanResults.push_back(kbDev);
            Serial.printf("[BLEKeyboardHost] Found: %s (%s) RSSI=%d\n",
                          kbDev.name.c_str(), kbDev.address.c_str(), kbDev.rssi);
        }
        xSemaphoreGive(_mutex);
    }
}

void BLEKeyboardHost::_onScanEnd() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (_state == BLEKBState::BLE_KB_SCANNING)
            _state = BLEKBState::BLE_KB_IDLE;
        xSemaphoreGive(_mutex);
    }
}

void BLEKeyboardHost::_onDisconnect() {
    Serial.println("[BLEKeyboardHost] Keyboard disconnected");
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _state     = BLEKBState::BLE_KB_DISCONNECTED;
        _modifiers = 0;
        memset(_prevKeycodes, 0, sizeof(_prevKeycodes));
        xSemaphoreGive(_mutex);
    }
}

void BLEKeyboardHost::_onHIDReport(const uint8_t* data, size_t length) {
    if (length < 2) return;

    const uint8_t modifiers = data[0];
    uint8_t currCodes[6]{};
    const size_t klen = (length >= 8) ? 6 : (length - 2);
    for (size_t i = 0; i < klen; ++i) currCodes[i] = data[2 + i];

    std::function<void(uint8_t, uint8_t, bool)> cb;
    uint8_t prevCodes[6]{};
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _modifiers = modifiers;
        memcpy(prevCodes, _prevKeycodes, 6);
        memcpy(_prevKeycodes, currCodes, 6);
        cb = _keyCallback;
        xSemaphoreGive(_mutex);
    }
    if (!cb) return;

    for (int i = 0; i < 6; ++i) {
        if (prevCodes[i] == 0) continue;
        bool stillHeld = false;
        for (int j = 0; j < 6; ++j) {
            if (currCodes[j] == prevCodes[i]) { stillHeld = true; break; }
        }
        if (!stillHeld) cb(prevCodes[i], modifiers, false);
    }

    for (int i = 0; i < 6; ++i) {
        if (currCodes[i] == 0) continue;
        bool isNew = true;
        for (int j = 0; j < 6; ++j) {
            if (prevCodes[j] == currCodes[i]) { isNew = false; break; }
        }
        if (isNew) cb(currCodes[i], modifiers, true);
    }
}

// ── HID keycode → ASCII ───────────────────────────────────────
char BLEKeyboardHost::hidKeyToChar(uint8_t keycode, uint8_t modifiers) {
    const bool shift = (modifiers & HID_MOD_SHIFT_MASK) != 0;

    if (keycode >= 0x04 && keycode <= 0x1D) {
        char base = static_cast<char>('a' + (keycode - 0x04));
        return shift ? static_cast<char>(base - 32) : base;
    }

    if (!shift) {
        switch (keycode) {
            case 0x1E: return '1'; case 0x1F: return '2'; case 0x20: return '3';
            case 0x21: return '4'; case 0x22: return '5'; case 0x23: return '6';
            case 0x24: return '7'; case 0x25: return '8'; case 0x26: return '9';
            case 0x27: return '0'; case 0x2C: return ' '; case 0x28: return '\n';
            case 0x2A: return '\b'; case 0x2B: return '\t'; case 0x2D: return '-';
            case 0x2E: return '='; case 0x2F: return '['; case 0x30: return ']';
            case 0x31: return '\\'; case 0x33: return ';'; case 0x34: return '\'';
            case 0x35: return '`'; case 0x36: return ','; case 0x37: return '.';
            case 0x38: return '/'; default: return '\0';
        }
    } else {
        switch (keycode) {
            case 0x1E: return '!'; case 0x1F: return '@'; case 0x20: return '#';
            case 0x21: return '$'; case 0x22: return '%'; case 0x23: return '^';
            case 0x24: return '&'; case 0x25: return '*'; case 0x26: return '(';
            case 0x27: return ')'; case 0x2C: return ' '; case 0x28: return '\n';
            case 0x2A: return '\b'; case 0x2B: return '\t'; case 0x2D: return '_';
            case 0x2E: return '+'; case 0x2F: return '{'; case 0x30: return '}';
            case 0x31: return '|'; case 0x33: return ':'; case 0x34: return '"';
            case 0x35: return '~'; case 0x36: return '<'; case 0x37: return '>';
            case 0x38: return '?'; default: return '\0';
        }
    }
}
