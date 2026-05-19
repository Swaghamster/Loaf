#include "TerminalApp.h"
#include "../display/EPDDisplay.h"
#include "../bluetooth/BLEKeyboardHost.h"
#include "../storage/FileManager.h"
#include "../../include/config.h"

#include <WiFi.h>
#include <SDCardManager.h>

// ── HID keycodes for special keys (USB HID Usage Page 0x07) ──────────────
static constexpr uint8_t HID_ENTER     = 0x28;
static constexpr uint8_t HID_BACKSPACE = 0x2A;
static constexpr uint8_t HID_TAB       = 0x2B;
static constexpr uint8_t HID_ESCAPE    = 0x29;
static constexpr uint8_t HID_DELETE    = 0x4C;
static constexpr uint8_t HID_RIGHT     = 0x4F;
static constexpr uint8_t HID_LEFT      = 0x50;
static constexpr uint8_t HID_DOWN      = 0x51;
static constexpr uint8_t HID_UP        = 0x52;
static constexpr uint8_t HID_PAGEUP    = 0x4B;
static constexpr uint8_t HID_PAGEDOWN  = 0x4E;
static constexpr uint8_t HID_HOME      = 0x4A;
static constexpr uint8_t HID_END       = 0x4D;

// ── Singleton ─────────────────────────────────────────────────────────────
TerminalApp& TerminalApp::instance() {
    static TerminalApp inst;
    return inst;
}

// ── init ──────────────────────────────────────────────────────────────────
void TerminalApp::init() {
    _clearScreen();
}

// ── activate / deactivate ─────────────────────────────────────────────────
void TerminalApp::activate() {
    _active = true;
    _dirty  = true;

    // Register BLE key callback
    BLEKeyboardHost::instance().setKeyCallback(
        [this](uint8_t kc, uint8_t mod, bool pressed) {
            if (pressed) _onBLEKey(kc, mod, pressed);
        }
    );
}

void TerminalApp::deactivate() {
    _active = false;
    // Clear BLE callback so it doesn't fire when terminal is off-screen
    BLEKeyboardHost::instance().setKeyCallback(nullptr);
}

// ── tick ──────────────────────────────────────────────────────────────────
void TerminalApp::tick() {
    if (!_active) return;

    // If SSH connected, pump incoming data
    if (_state == State::CONNECTED && _ssh.isConnected()) {
        char rxBuf[256];
        int n = _ssh.read(rxBuf, sizeof(rxBuf) - 1);
        if (n > 0) {
            rxBuf[n] = '\0';
            _processOutput(rxBuf, n);
            _dirty = true;
        } else if (n < 0) {
            _state = State::FAILED;
            snprintf(_status, sizeof(_status), "SSH disconnected");
            _dirty = true;
        }
    }

    // Rate-limited e-ink refresh
    if (_dirty) {
        uint32_t now = millis();
        if (now - _lastRefreshMs >= REFRESH_MS) {
            render();
            _lastRefreshMs = now;
        }
    }
}

// ── _loadConfig ───────────────────────────────────────────────────────────
// Reads /config/terminal.ini from SD.  Format: key=value, one per line.
bool TerminalApp::_loadConfig() {
    if (!FileManager::instance().isReady()) {
        snprintf(_status, sizeof(_status), "SD card not ready");
        return false;
    }

    FsFile f = SDCardManager::getInstance().open("/config/terminal.ini", O_RDONLY);
    if (!f) {
        snprintf(_status, sizeof(_status), "Missing /config/terminal.ini on SD");
        return false;
    }

    char line[128];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[len++] = c;
        }
        line[len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if      (strcmp(key, "wifi_ssid") == 0) _wifiSSID = val;
        else if (strcmp(key, "wifi_pass") == 0) _wifiPass = val;
        else if (strcmp(key, "ssh_host")  == 0) _sshHost  = val;
        else if (strcmp(key, "ssh_port")  == 0) _sshPort  = (uint16_t)atoi(val);
        else if (strcmp(key, "ssh_user")  == 0) _sshUser  = val;
        else if (strcmp(key, "ssh_pass")  == 0) _sshPass  = val;
    }
    f.close();

    if (_wifiSSID.isEmpty() || _sshHost.isEmpty() || _sshUser.isEmpty()) {
        snprintf(_status, sizeof(_status), "terminal.ini missing wifi_ssid / ssh_host / ssh_user");
        return false;
    }
    return true;
}

// ── _startConnect ─────────────────────────────────────────────────────────
void TerminalApp::_startConnect() {
    if (_state == State::CONNECTED) return;

    if (!_loadConfig()) { _state = State::FAILED; _dirty = true; return; }

    // ── WiFi ─────────────────────────────────────────────────────────────
    if (WiFi.status() != WL_CONNECTED) {
        _state = State::WIFI_CONNECTING;
        snprintf(_status, sizeof(_status), "WiFi: connecting to %s...", _wifiSSID.c_str());
        _dirty = true;
        render();   // show progress immediately

        WiFi.mode(WIFI_STA);
        WiFi.begin(_wifiSSID.c_str(), _wifiPass.c_str());

        uint32_t t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
            delay(200);
        }
        if (WiFi.status() != WL_CONNECTED) {
            _state = State::FAILED;
            snprintf(_status, sizeof(_status), "WiFi: could not connect to %s", _wifiSSID.c_str());
            _dirty = true;
            return;
        }
    }

    // ── SSH ──────────────────────────────────────────────────────────────
    _state = State::SSH_CONNECTING;
    snprintf(_status, sizeof(_status), "SSH: connecting to %s@%s:%u...",
             _sshUser.c_str(), _sshHost.c_str(), _sshPort);
    _dirty = true;
    render();

    SSHConfig cfg{ _sshHost, _sshPort, _sshUser, _sshPass };
    if (!_ssh.connect(cfg)) {
        _state = State::FAILED;
        snprintf(_status, sizeof(_status), "SSH: %s", _ssh.lastError());
        _dirty = true;
        return;
    }

    _state = State::CONNECTED;
    snprintf(_status, sizeof(_status), "Connected  %s@%s  BLE:%s",
             _sshUser.c_str(), _sshHost.c_str(),
             BLEKeyboardHost::instance().isConnected() ? "on" : "off");
    _clearScreen();
    _dirty = true;
}

// ── handleButton ─────────────────────────────────────────────────────────
void TerminalApp::handleButton(uint8_t btn, bool longPress) {
    // BTN_* constants from UIManager.h are in scope via config.h
    switch (btn) {
        case 1:   // BTN_SELECT
            if (_state != State::CONNECTED)
                _startConnect();
            else
                _sendChar('\n');
            break;

        case 0:   // BTN_BACK
            if (_ssh.isConnected()) _ssh.disconnect();
            _state = State::IDLE;
            snprintf(_status, sizeof(_status), "Press SEL to connect");
            // UIManager will navigate back — nothing more needed here
            break;

        case 4:   // BTN_UP
            if (_state == State::CONNECTED) _sendToSSH("\x1b[A", 3);
            break;
        case 5:   // BTN_DOWN
            if (_state == State::CONNECTED) _sendToSSH("\x1b[B", 3);
            break;
        case 3:   // BTN_RIGHT
            if (_state == State::CONNECTED) _sendToSSH("\x1b[C", 3);
            break;
        case 2:   // BTN_LEFT
            if (_state == State::CONNECTED) _sendToSSH("\x1b[D", 3);
            break;
        default: break;
    }
}

// ── _onBLEKey ─────────────────────────────────────────────────────────────
void TerminalApp::_onBLEKey(uint8_t keycode, uint8_t modifiers, bool /*pressed*/) {
    if (_state != State::CONNECTED) return;

    // Special keys → VT100 sequences
    switch (keycode) {
        case HID_ENTER:     _sendChar('\r');              return;
        case HID_BACKSPACE: _sendChar('\x7f');            return;
        case HID_TAB:       _sendChar('\t');              return;
        case HID_ESCAPE:    _sendChar('\x1b');            return;
        case HID_UP:        _sendToSSH("\x1b[A", 3);     return;
        case HID_DOWN:      _sendToSSH("\x1b[B", 3);     return;
        case HID_RIGHT:     _sendToSSH("\x1b[C", 3);     return;
        case HID_LEFT:      _sendToSSH("\x1b[D", 3);     return;
        case HID_HOME:      _sendToSSH("\x1b[H", 3);     return;
        case HID_END:       _sendToSSH("\x1b[F", 3);     return;
        case HID_PAGEUP:    _sendToSSH("\x1b[5~", 4);    return;
        case HID_PAGEDOWN:  _sendToSSH("\x1b[6~", 4);    return;
        case HID_DELETE:    _sendToSSH("\x1b[3~", 4);    return;
        default: break;
    }

    // Printable key
    char c = BLEKeyboardHost::hidKeyToChar(keycode, modifiers);
    if (c) _sendChar(c);
}

// ── _sendToSSH ────────────────────────────────────────────────────────────
void TerminalApp::_sendToSSH(const char* seq, size_t len) {
    if (_state == State::CONNECTED) _ssh.write(seq, len);
}

// ── Terminal emulator ─────────────────────────────────────────────────────
void TerminalApp::_processOutput(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char c = data[i];

        if (_inEsc) {
            if (_escLen < (int)sizeof(_esc) - 1) _esc[_escLen++] = c;
            // ESC sequences end when a letter is encountered
            if (isalpha(c) || c == '~' || c == '@') {
                _esc[_escLen] = '\0';
                _handleEscape();
                _inEsc  = false;
                _escLen = 0;
            }
            continue;
        }

        if (c == '\x1b') {
            _inEsc  = true;
            _escLen = 0;
            continue;
        }

        switch (c) {
            case '\r': _curCol = 0; break;
            case '\n':
                ++_curRow;
                if (_curRow >= TERM_ROWS) _scrollUp();
                break;
            case '\b':
                if (_curCol > 0) { --_curCol; _buf[_curRow][_curCol] = ' '; }
                break;
            case '\t': {
                int next = (_curCol + 8) & ~7;
                while (_curCol < next && _curCol < TERM_COLS) _buf[_curRow][_curCol++] = ' ';
                break;
            }
            default:
                if (c >= 0x20 && c < 0x7f) {
                    if (_curCol >= TERM_COLS) { _curCol = 0; ++_curRow; }
                    if (_curRow >= TERM_ROWS) _scrollUp();
                    _buf[_curRow][_curCol++] = c;
                }
                break;
        }
    }
}

void TerminalApp::_handleEscape() {
    // _esc contains everything after ESC.  Most common patterns:
    const char* s = _esc;

    if (s[0] == '[') {
        const char* p = s + 1;
        char cmd = _esc[_escLen - 1];

        if (cmd == 'H' || cmd == 'f') {
            // Cursor position: ESC[row;colH  (1-based)
            int row = 0, col = 0;
            sscanf(p, "%d;%d", &row, &col);
            _curRow = (row > 0 ? row - 1 : 0);
            _curCol = (col > 0 ? col - 1 : 0);
            _curRow = min(_curRow, TERM_ROWS - 1);
            _curCol = min(_curCol, TERM_COLS - 1);
        } else if (cmd == 'J') {
            if (*p == '2') _clearScreen();
            // ESC[0J or ESC[J: clear from cursor to end (simplified: clear screen)
            else _clearScreen();
        } else if (cmd == 'K') {
            // Erase to end of line
            for (int c2 = _curCol; c2 < TERM_COLS; ++c2) _buf[_curRow][c2] = ' ';
        } else if (cmd == 'A') {
            int n = atoi(p); if (n < 1) n = 1;
            _curRow = max(0, _curRow - n);
        } else if (cmd == 'B') {
            int n = atoi(p); if (n < 1) n = 1;
            _curRow = min(TERM_ROWS - 1, _curRow + n);
        } else if (cmd == 'C') {
            int n = atoi(p); if (n < 1) n = 1;
            _curCol = min(TERM_COLS - 1, _curCol + n);
        } else if (cmd == 'D') {
            int n = atoi(p); if (n < 1) n = 1;
            _curCol = max(0, _curCol - n);
        }
        // ESC[...m (SGR / colour) — ignored on 1-bit e-ink
    }
}

void TerminalApp::_clearScreen() {
    for (int r = 0; r < TERM_ROWS; ++r) memset(_buf[r], ' ', TERM_COLS);
    for (int r = 0; r < TERM_ROWS; ++r) _buf[r][TERM_COLS] = '\0';
    _curRow = 0; _curCol = 0;
}

void TerminalApp::_scrollUp() {
    for (int r = 1; r < TERM_ROWS; ++r)
        memcpy(_buf[r - 1], _buf[r], TERM_COLS + 1);
    memset(_buf[TERM_ROWS - 1], ' ', TERM_COLS);
    _buf[TERM_ROWS - 1][TERM_COLS] = '\0';
    _curRow = TERM_ROWS - 1;
}

// ── render ────────────────────────────────────────────────────────────────
void TerminalApp::render() {
    EPDDisplay& epd = EPDDisplay::instance();
    LoafCanvas& c   = epd.canvas();

    // Clear terminal area (below status bar)
    epd.fillRect(0, ORIGIN_Y, EPD_WIDTH, EPD_HEIGHT - ORIGIN_Y, 0xFFFF);

    // Use Adafruit_GFX built-in 5×7 font at size 2 = 12×16 px per char.
    c.setTextSize(2);
    c.setTextColor(0x0000);
    c.setTextWrap(false);

    for (int row = 0; row < TERM_ROWS; ++row) {
        c.setCursor(ORIGIN_X, ORIGIN_Y + row * CHAR_H);
        c.print(_buf[row]);
    }

    // Draw cursor block (filled rectangle)
    if (_state == State::CONNECTED) {
        int cx = ORIGIN_X + _curCol * CHAR_W;
        int cy = ORIGIN_Y + _curRow * CHAR_H;
        // Inverted cursor: black rect, white char
        epd.fillRect(cx, cy, CHAR_W, CHAR_H, 0x0000);
        char cur[2] = { _buf[_curRow][_curCol], '\0' };
        c.setTextColor(0xFFFF);
        c.setCursor(cx, cy);
        c.print(cur);
        c.setTextColor(0x0000);
    }

    _renderStatusLine();
    epd.update();
    _dirty = false;
}

void TerminalApp::_renderStatusLine() {
    EPDDisplay& epd = EPDDisplay::instance();

    // Status strip at very bottom (last 18px)
    const int16_t sy = EPD_HEIGHT - 18;
    epd.fillRect(0, sy - 1, EPD_WIDTH, 19, 0x0000);

    LoafCanvas& c = epd.canvas();
    c.setTextSize(1);
    c.setTextColor(0xFFFF);
    c.setCursor(4, sy + 4);
    c.print(_status);

    // BLE indicator on the right
    const char* bleStr = BLEKeyboardHost::instance().isConnected() ? "KB:on" : "KB:off";
    int16_t bw = strlen(bleStr) * 6;  // textSize(1) = 6px per char
    c.setCursor(EPD_WIDTH - bw - 4, sy + 4);
    c.print(bleStr);

    c.setTextSize(2);   // restore for terminal text
}
