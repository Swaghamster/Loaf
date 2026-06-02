#pragma once
// ============================================================
// TerminalApp.h  —  SSH terminal screen for Loaf
//
// Connects a BLE HID keyboard (via BLEKeyboardHost) and an
// SSH session (via SSHClient) and displays the output on the
// e-ink panel using a fixed 12×16 character grid.
//
// Config file on SD card:  /config/terminal.ini
//   wifi_ssid=YourNetwork
//   wifi_pass=YourPassword
//   ssh_host=192.168.1.10
//   ssh_port=22
//   ssh_user=pi
//   ssh_pass=secret
//
// Button mapping while terminal is active:
//   UP/DOWN/LEFT/RIGHT  → send arrow-key escape sequences to SSH
//   SELECT              → send Enter
//   BACK (short)        → disconnect and return to home
//   BACK (long)         → force-quit without graceful disconnect
// ============================================================

#include <Arduino.h>
#include "SSHClient.h"

class TerminalApp {
public:
    static TerminalApp& instance();

    // Call once from main setup() after BLEKeyboardHost::init().
    void init();

    // Called by UIManager when entering / leaving SCREEN_TERMINAL.
    void activate();
    void deactivate();

    // Called by UIManager::render() when SCREEN_TERMINAL is active.
    void render();

    // Called by UIManager::handleButton() for SCREEN_TERMINAL.
    void handleButton(uint8_t btn, bool longPress);

    // Drive SSH I/O — call from Arduino loop() unconditionally;
    // does nothing when the terminal screen is not active.
    void tick();

    // Returns true while the terminal screen is active.
    bool isActive() const { return _active; }

    // ── Terminal dimensions ────────────────────────────────────
    // At Adafruit_GFX textSize(2): chars are 12 px wide × 16 px tall.
    static constexpr int TERM_COLS   = 66;
    static constexpr int TERM_ROWS   = 26;
    static constexpr int CHAR_W      = 12;
    static constexpr int CHAR_H      = 16;
    static constexpr int ORIGIN_X    =  2;
    static constexpr int ORIGIN_Y    = 22;   // below UIManager status bar

private:
    TerminalApp() = default;

    enum class State { IDLE, WIFI_CONNECTING, SSH_CONNECTING, CONNECTED, FAILED };

    // ── Connection ─────────────────────────────────────────────
    bool _loadConfig();
    void _startConnect();

    // ── Key handling ───────────────────────────────────────────
    // Registered as BLEKeyboardHost callback.
    void _onBLEKey(uint8_t keycode, uint8_t modifiers, bool pressed);
    void _sendToSSH(const char* seq, size_t len);
    void _sendChar(char c) { _sendToSSH(&c, 1); }

    // ── Terminal emulator ──────────────────────────────────────
    void _processOutput(const char* data, size_t len);
    void _putChar(char c);
    void _handleEscape();
    void _clearScreen();
    void _scrollUp();

    // ── Rendering ──────────────────────────────────────────────
    void _renderAll();
    void _renderStatusLine();

    // ── State ──────────────────────────────────────────────────
    bool  _active     = false;
    State _state      = State::IDLE;
    bool  _dirty      = true;

    // Terminal buffer — each row is a null-terminated string of TERM_COLS chars.
    char  _buf[TERM_ROWS][TERM_COLS + 1] = {};
    int   _curRow     = 0;
    int   _curCol     = 0;

    // VT100 escape accumulator
    char  _esc[32]    = {};
    int   _escLen     = 0;
    bool  _inEsc      = false;

    // Status / error message shown in the bottom strip
    char  _status[96] = "Press SEL to connect";

    // SSH
    SSHClient _ssh;

    // Config
    String   _wifiSSID, _wifiPass;
    String   _sshHost, _sshUser, _sshPass;
    uint16_t _sshPort = 22;

    // Refresh rate-limiter: only push to e-ink every REFRESH_MS
    static constexpr uint32_t REFRESH_MS = 300;
    uint32_t _lastRefreshMs = 0;
};
