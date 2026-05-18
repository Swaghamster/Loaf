// ============================================================
// NoteEditor.cpp
// Loaf Firmware — Full-screen plain-text note editor
// ============================================================

#include "NoteEditor.h"
#include "NotesApp.h"
#include "../display/EPDDisplay.h"
#include "../storage/FileManager.h"
#include "../bluetooth/BLEKeyboardHost.h"
#include "../ui/UIManager.h"
#include "../../include/config.h"

#include <GxEPD2_BW.h>
#include <algorithm>

// ── Keyboard layout tables ────────────────────────────────────
//
// Row 0: digit/symbol row  (12 keys)
// Row 1: QWERTY row        (10 keys)
// Row 2: ASDFGH row        (9 keys)
// Row 3: ZXCVBNM + BKSP + SPC  (7 + 2 = 9 logical keys)
//
// Special key sentinels used in row 3:
//   '\x01' = Backspace
//   '\x02' = Space
//   '\x03' = Shift / CapsLock (toggle)
//   '\x04' = Enter / newline

const char* const NoteEditor::KB_ROW0    = "1234567890-=";  // 12 keys
const char* const NoteEditor::KB_ROW1    = "QWERTYUIOP";   // 10 keys
const char* const NoteEditor::KB_ROW2    = "ASDFGHJKL";    //  9 keys
// Row 3: Z X C V B N M | SHIFT | BKSP | SPC
// Represented by a 10-element string with sentinels:
// 'Z','X','C','V','B','N','M', shift=\x03, bksp=\x01, spc=\x02
const char* const NoteEditor::KB_ROW3    = "ZXCVBNM\x03\x01\x02";  // 10 keys

const int NoteEditor::KB_ROW_LENS[NoteEditor::KB_ROWS] = { 12, 10, 9, 10 };

// Row data pointer array (must match KB_ROWS order)
static const char* const KB_ROWS_DATA[4] = {
    NoteEditor::KB_ROW0,
    NoteEditor::KB_ROW1,
    NoteEditor::KB_ROW2,
    NoteEditor::KB_ROW3,
};

// Shifted symbols for digit row (index 0-11 maps to KB_ROW0 keys)
static const char KB_ROW0_SHIFT[] = "!@#$%^&*()_+";

// ── Singleton ──────────────────────────────────────────────────
NoteEditor& NoteEditor::instance() {
    static NoteEditor inst;
    return inst;
}

// ── Lifecycle ──────────────────────────────────────────────────
void NoteEditor::init(const String& filename, const String& initialContent) {
    _filename      = filename;
    _buffer        = initialContent;
    _cursorPos     = _buffer.length();  // Start at end
    _dirty         = false;
    _done          = false;
    _mode          = EditorMode::SOFT_KEYBOARD;
    _skRow         = 1;
    _skCol         = 4;  // 'T' on QWERTY row — roughly centre
    _shift         = false;
    _capsLock      = false;
    _topLine       = 0;
    _menuSelected  = 0;
    _btScanSelected = 0;
    _btConnecting  = false;
    _lastSaveMs    = millis();
    _lastBlinkMs   = millis();
    _cursorVisible = true;
    _needsFullRedraw = true;

    _rewrap();

    // Register BLE key callback
    _installBLECallback();

    // If BLE keyboard is already connected, switch mode immediately
    if (BLEKeyboardHost::instance().isConnected()) {
        _mode = EditorMode::BLE_KEYBOARD;
    }

    Serial.printf("[NoteEditor] Opened '%s' (%d chars)\n",
                  _filename.c_str(), _buffer.length());
}

// ── Main loop tick ─────────────────────────────────────────────
void NoteEditor::tick() {
    // Auto-save
    if (_dirty && (millis() - _lastSaveMs >= AUTOSAVE_MS)) {
        save();
    }

    // Cursor blinking (text area only, partial refresh)
    if (_mode == EditorMode::SOFT_KEYBOARD || _mode == EditorMode::BLE_KEYBOARD) {
        if (millis() - _lastBlinkMs >= BLINK_MS) {
            _lastBlinkMs = millis();
            _blinkCursor();
        }
    }

    // BT scan spinner
    if (_mode == EditorMode::BT_SCAN) {
        BLEKBState state = BLEKeyboardHost::instance().getState();

        if (state == BLEKBState::BLE_KB_CONNECTED) {
            // Successful connection
            _mode = EditorMode::BLE_KEYBOARD;
            _needsFullRedraw = true;
            render();
        } else if (state == BLEKBState::BLE_KB_IDLE &&
                   millis() - _btScanStartMs > 500) {
            // Scan has completed — show results
            _needsFullRedraw = true;
            renderBTMenu();
        } else {
            // Still scanning — update spinner character periodically
            if (millis() - _btScanStartMs > (uint32_t)(_scanSpinnerIdx * SCAN_SPINNER_MS)) {
                ++_scanSpinnerIdx;
                _needsFullRedraw = true;
                renderBTMenu();
            }
        }
    }

    if (_mode == EditorMode::BT_CONNECTING) {
        BLEKBState state = BLEKeyboardHost::instance().getState();
        if (state == BLEKBState::BLE_KB_CONNECTED) {
            _mode = EditorMode::BLE_KEYBOARD;
            _needsFullRedraw = true;
            render();
        } else if (state == BLEKBState::BLE_KB_DISCONNECTED ||
                   state == BLEKBState::BLE_KB_IDLE) {
            // Connection failed — fall back
            _mode = EditorMode::SOFT_KEYBOARD;
            _needsFullRedraw = true;
            render();
        }
    }
}

// ── render ─────────────────────────────────────────────────────
void NoteEditor::render() {
    switch (_mode) {
        case EditorMode::MENU:
            _renderMenu();
            return;

        case EditorMode::BT_SCAN:
            renderBTMenu();
            return;

        case EditorMode::BT_CONNECTING:
            _renderBTConnecting();
            return;

        default:
            break;
    }

    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.raw();

    bool useFullRefresh = _needsFullRedraw;
    _needsFullRedraw = false;

    if (useFullRefresh) {
        raw.setFullWindow();
        raw.firstPage();
        do {
            raw.fillScreen(GxEPD_WHITE);
            _renderTextArea();
            if (_mode == EditorMode::SOFT_KEYBOARD) {
                _renderSoftKeyboard(/*doPartialFlush=*/false);
            } else {
                // BLE keyboard mode: show slim status bar at bottom
                int16_t barY = EPD_HEIGHT - 20;
                raw.drawFastHLine(0, barY, EPD_WIDTH, GxEPD_BLACK);
                String kbName = BLEKeyboardHost::instance().getConnectedDeviceName();
                String hint   = "BT: " + (kbName.isEmpty() ? String("connected") : kbName);
                hint += "  MENU=options";
                epd.drawText(MARGIN_X, barY + 14, hint.c_str(), 12, false, GxEPD_BLACK);
            }
        } while (raw.nextPage());
        epd.update();
    }
}

// ── handleButton ───────────────────────────────────────────────
void NoteEditor::handleButton(uint8_t btn, bool longPress) {

    // ── Menu mode ─────────────────────────────────────────────
    if (_mode == EditorMode::MENU) {
        switch (btn) {
            case BTN_UP:
                if (_menuSelected > 0) { --_menuSelected; _renderMenu(); }
                break;
            case BTN_DOWN:
                if (_menuSelected < MENU_ITEM_COUNT - 1) { ++_menuSelected; _renderMenu(); }
                break;
            case BTN_SELECT:
                switch (_menuSelected) {
                    case 0:  // Save
                        save();
                        _mode = (_prevMode == EditorMode::BLE_KEYBOARD)
                                ? EditorMode::BLE_KEYBOARD
                                : EditorMode::SOFT_KEYBOARD;
                        _needsFullRedraw = true;
                        render();
                        break;
                    case 1:  // Connect Keyboard
                        openBTKeyboardMenu();
                        break;
                    case 2:  // Exit
                        if (_dirty) save();
                        _done = true;
                        break;
                }
                break;
            case BTN_BACK:
                // Cancel menu
                _mode = (_prevMode == EditorMode::BLE_KEYBOARD)
                        ? EditorMode::BLE_KEYBOARD
                        : EditorMode::SOFT_KEYBOARD;
                _needsFullRedraw = true;
                render();
                break;
            default:
                break;
        }
        return;
    }

    // ── BT scan results mode ──────────────────────────────────
    if (_mode == EditorMode::BT_SCAN) {
        auto results = BLEKeyboardHost::instance().getScanResults();

        switch (btn) {
            case BTN_UP:
                if (_btScanSelected > 0) { --_btScanSelected; renderBTMenu(); }
                break;
            case BTN_DOWN:
                if (_btScanSelected + 1 < static_cast<int>(results.size())) {
                    ++_btScanSelected; renderBTMenu();
                }
                break;
            case BTN_SELECT:
                if (!results.empty()) {
                    const String& addr = results[_btScanSelected].address;
                    _btConnectingName  = results[_btScanSelected].name;
                    _mode = EditorMode::BT_CONNECTING;
                    _renderBTConnecting();
                    // Attempt connection (blocking briefly)
                    if (BLEKeyboardHost::instance().connect(addr)) {
                        BLEKeyboardHost::instance().savePairedDevice(addr, _btConnectingName);
                        _mode = EditorMode::BLE_KEYBOARD;
                    } else {
                        _mode = EditorMode::SOFT_KEYBOARD;
                    }
                    _needsFullRedraw = true;
                    render();
                }
                break;
            case BTN_BACK:
                BLEKeyboardHost::instance().stopScan();
                _mode = EditorMode::SOFT_KEYBOARD;
                _needsFullRedraw = true;
                render();
                break;
            default:
                break;
        }
        return;
    }

    // ── BLE keyboard mode ─────────────────────────────────────
    if (_mode == EditorMode::BLE_KEYBOARD) {
        switch (btn) {
            case BTN_UP:
                moveCursorLine(-1);
                _needsFullRedraw = true;
                render();
                break;
            case BTN_DOWN:
                moveCursorLine(1);
                _needsFullRedraw = true;
                render();
                break;
            case BTN_BACK:
                _prevMode = EditorMode::BLE_KEYBOARD;
                _mode = EditorMode::MENU;
                _menuSelected = 0;
                _renderMenu();
                break;
            default:
                break;
        }
        return;
    }

    // ── Soft keyboard mode ────────────────────────────────────
    switch (btn) {
        case BTN_UP:
            if (_skRow > 0) {
                --_skRow;
                // Clamp column to new row width
                int rowLen = KB_ROW_LENS[_skRow];
                if (_skCol >= rowLen) _skCol = rowLen - 1;
            }
            _renderSoftKeyboard(/*doPartialFlush=*/true);
            break;

        case BTN_DOWN:
            if (_skRow < KB_ROWS - 1) {
                ++_skRow;
                int rowLen = KB_ROW_LENS[_skRow];
                if (_skCol >= rowLen) _skCol = rowLen - 1;
            }
            _renderSoftKeyboard(/*doPartialFlush=*/true);
            break;

        case BTN_SELECT:
            if (longPress) {
                // Long press SELECT = move cursor left in text
                moveCursor(-1);
                _needsFullRedraw = true;
                render();
            } else {
                char c = _softKeyChar();
                if (c == '\x01') {
                    // Backspace
                    deleteChar();
                    _needsFullRedraw = true;
                    render();
                } else if (c == '\x02') {
                    // Space
                    insertChar(' ');
                    _needsFullRedraw = true;
                    render();
                } else if (c == '\x03') {
                    // Shift / CapsLock
                    if (_shift) {
                        // Second press → CapsLock
                        _capsLock = !_capsLock;
                        _shift = false;
                    } else {
                        _shift = true;
                    }
                    _renderSoftKeyboard(/*doPartialFlush=*/true);
                } else if (c == '\x04') {
                    // Enter
                    insertChar('\n');
                    _needsFullRedraw = true;
                    render();
                } else if (c != '\0') {
                    insertChar(c);
                    // One-shot shift
                    if (_shift && !_capsLock) _shift = false;
                    _needsFullRedraw = true;
                    render();
                }
            }
            break;

        case BTN_BACK:
            // Open menu
            _prevMode = EditorMode::SOFT_KEYBOARD;
            _mode = EditorMode::MENU;
            _menuSelected = 0;
            _renderMenu();
            break;

        default:
            break;
    }
}

// ── Text buffer operations ─────────────────────────────────────

void NoteEditor::insertChar(char c) {
    if (static_cast<int>(_buffer.length()) >= NOTES_MAX_LENGTH) return;

    // Insert at cursor position
    _buffer = _buffer.substring(0, _cursorPos) +
              String(c) +
              _buffer.substring(_cursorPos);
    ++_cursorPos;
    _dirty = true;
    _rewrap();
}

void NoteEditor::deleteChar() {
    if (_cursorPos == 0) return;
    _buffer = _buffer.substring(0, _cursorPos - 1) +
              _buffer.substring(_cursorPos);
    --_cursorPos;
    _dirty = true;
    _rewrap();
}

void NoteEditor::moveCursor(int delta) {
    _cursorPos = constrain(_cursorPos + delta,
                           0, static_cast<int>(_buffer.length()));
}

void NoteEditor::moveCursorLine(int lines) {
    if (_lineStarts.empty()) return;

    int curLine, curCol;
    _cursorToLineCol(curLine, curCol);

    int targetLine = constrain(curLine + lines,
                               0, static_cast<int>(_lineStarts.size()) - 1);
    if (targetLine == curLine) return;

    // Try to preserve column; clamp to end of target line
    int lineLen = (targetLine + 1 < static_cast<int>(_lineStarts.size()))
                  ? _lineStarts[targetLine + 1] - _lineStarts[targetLine]
                  : static_cast<int>(_buffer.length()) - _lineStarts[targetLine];
    lineLen = max(lineLen - 1, 0);  // subtract potential newline

    int newCol = min(curCol, lineLen);
    _cursorPos = _lineColToCursorPos(targetLine, newCol);

    // Scroll text area so cursor line is visible
    int visLines = TEXT_AREA_H / LINE_H;
    if (targetLine < _topLine) {
        _topLine = targetLine;
    } else if (targetLine >= _topLine + visLines) {
        _topLine = targetLine - visLines + 1;
    }
}

// ── Persistence ────────────────────────────────────────────────

void NoteEditor::save() {
    String path = String(SD_NOTES_DIR) + "/" + _filename;
    if (FileManager::instance().writeFile(path.c_str(), _buffer)) {
        _dirty = false;
        _lastSaveMs = millis();
        Serial.printf("[NoteEditor] Saved '%s'\n", _filename.c_str());
    } else {
        Serial.printf("[NoteEditor] Save FAILED for '%s'\n", _filename.c_str());
    }
}

// ── BT keyboard menu ───────────────────────────────────────────

void NoteEditor::openBTKeyboardMenu() {
    _mode = EditorMode::BT_SCAN;
    _btScanSelected = 0;
    _btScanStartMs  = millis();
    _scanSpinnerIdx = 0;

    BLEKeyboardHost::instance().startScan(BLE_SCAN_TIMEOUT_S);
    renderBTMenu();
}

void NoteEditor::renderBTMenu() {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.raw();
    BLEKeyboardHost& kb = BLEKeyboardHost::instance();
    BLEKBState state = kb.getState();

    raw.setFullWindow();
    raw.firstPage();
    do {
        raw.fillScreen(GxEPD_WHITE);

        // ── Header ────────────────────────────────────────────
        raw.fillRect(0, 0, EPD_WIDTH, 28, GxEPD_BLACK);
        epd.drawText(MARGIN_X, 21, "Connect Bluetooth Keyboard", 16, true, GxEPD_WHITE);

        // ── Scanning indicator ────────────────────────────────
        static const char* spinners[] = { "|", "/", "-", "\\" };
        if (state == BLEKBState::BLE_KB_SCANNING) {
            String msg = String("Scanning... ") +
                         spinners[_scanSpinnerIdx % 4];
            epd.drawText(MARGIN_X, 55, msg.c_str(), 16, false, GxEPD_BLACK);
            epd.drawText(MARGIN_X, 75,
                         "Looking for HID keyboards nearby.",
                         12, false, GxEPD_BLACK);
        } else {
            // Scan complete — show results
            auto results = kb.getScanResults();

            if (results.empty()) {
                epd.drawText(MARGIN_X, 55,
                             "No keyboards found.", 16, false, GxEPD_BLACK);
                epd.drawText(MARGIN_X, 75,
                             "Make sure keyboard is in pairing mode,",
                             12, false, GxEPD_BLACK);
                epd.drawText(MARGIN_X, 91,
                             "then press BACK and try again.",
                             12, false, GxEPD_BLACK);
            } else {
                epd.drawText(MARGIN_X, 50, "Found keyboards:", 12, true, GxEPD_BLACK);

                int16_t y = 66;
                for (int i = 0; i < static_cast<int>(results.size()); ++i) {
                    bool sel = (i == _btScanSelected);
                    if (sel) {
                        raw.fillRect(0, y - 14, EPD_WIDTH, 18, GxEPD_BLACK);
                    }
                    uint16_t tc = sel ? GxEPD_WHITE : GxEPD_BLACK;
                    String label = results[i].name;
                    if (label.isEmpty()) label = results[i].address;
                    label += "  RSSI:" + String(results[i].rssi) + "dBm";
                    epd.drawText(MARGIN_X, y, label.c_str(), 12, false, tc);
                    y += 20;
                    if (y > EPD_HEIGHT - 30) break;
                }
            }
        }

        // ── Footer ────────────────────────────────────────────
        raw.drawFastHLine(0, EPD_HEIGHT - 20, EPD_WIDTH, GxEPD_BLACK);
        epd.drawText(MARGIN_X, EPD_HEIGHT - 6,
                     "UP/DN: select  SEL: connect  BACK: cancel",
                     12, false, GxEPD_BLACK);

    } while (raw.nextPage());
    epd.update();
}

// ── Internal: render helpers ───────────────────────────────────

void NoteEditor::_renderTextArea() {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.raw();

    // ── Status bar ─────────────────────────────────────────────
    raw.fillRect(0, 0, EPD_WIDTH, STATUS_BAR_H, GxEPD_BLACK);
    {
        // Left: filename (truncated)
        String name = _filename;
        if (name.length() > 24) name = name.substring(0, 24) + "..";
        epd.drawText(MARGIN_X, STATUS_BAR_H - 5, name.c_str(), 12, false, GxEPD_WHITE);

        // Right: dirty indicator + char count
        String info = String(_buffer.length()) + "ch";
        if (_dirty) info = "*" + info;
        int16_t iw = epd.getTextWidth(info.c_str(), 12, false);
        epd.drawText(EPD_WIDTH - iw - MARGIN_X, STATUS_BAR_H - 5,
                     info.c_str(), 12, false, GxEPD_WHITE);
    }

    // ── Text content ───────────────────────────────────────────
    int16_t textBottom = STATUS_BAR_H + TEXT_AREA_H;
    int visLines = TEXT_AREA_H / LINE_H;

    int curLine, curCol;
    _cursorToLineCol(curLine, curCol);

    // Ensure top-line keeps cursor visible
    if (curLine < _topLine) _topLine = curLine;
    if (curLine >= _topLine + visLines) _topLine = curLine - visLines + 1;
    if (_topLine < 0) _topLine = 0;

    int16_t y = TEXT_Y_START;
    for (int li = _topLine; li < _topLine + visLines; ++li) {
        if (li >= static_cast<int>(_lineStarts.size())) break;
        if (y > textBottom) break;

        int lineStart = _lineStarts[li];
        int lineEnd   = (li + 1 < static_cast<int>(_lineStarts.size()))
                        ? _lineStarts[li + 1]
                        : static_cast<int>(_buffer.length());

        // Strip trailing newline from display string
        String lineText = _buffer.substring(lineStart, lineEnd);
        lineText.replace("\n", "");

        epd.drawText(MARGIN_X, y, lineText.c_str(), TEXT_FONT_SZ, false, GxEPD_BLACK);

        // Draw cursor on current line
        if (li == curLine) {
            // Measure width of text before cursor on this line
            String beforeCursor = _buffer.substring(lineStart, lineStart + curCol);
            beforeCursor.replace("\n", "");
            int16_t cx = MARGIN_X +
                         epd.getTextWidth(beforeCursor.c_str(), TEXT_FONT_SZ, false);
            int16_t cy = y - LINE_H + 2;
            if (_cursorVisible) {
                raw.fillRect(cx, cy, 2, LINE_H - 2, GxEPD_BLACK);
            }
        }

        y += LINE_H;
    }

    // Divider between text area and keyboard / status area
    int16_t divY = EPD_HEIGHT - (_mode == EditorMode::SOFT_KEYBOARD ? KEYBOARD_H : 20);
    raw.drawFastHLine(0, divY, EPD_WIDTH, GxEPD_BLACK);
}

void NoteEditor::_renderSoftKeyboard(bool doPartialFlush) {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.raw();

    // When called for a partial flush we need our own firstPage/nextPage scope.
    // When called from inside a full-refresh loop the caller owns the page pump.
    if (doPartialFlush) {
        raw.setPartialWindow(0, KB_TOP, EPD_WIDTH, KEYBOARD_H);
        raw.firstPage();
        do {
            _drawSoftKeyboardContent(epd);
        } while (raw.nextPage());
        return;
    }

    // Called from inside an existing firstPage/nextPage loop — draw directly.
    _drawSoftKeyboardContent(epd);
}

void NoteEditor::_drawSoftKeyboardContent(EPDDisplay& epd) {
    auto& raw = epd.raw();

    // Clear keyboard area
    raw.fillRect(0, KB_TOP, EPD_WIDTH, KEYBOARD_H, GxEPD_WHITE);
    raw.drawFastHLine(0, KB_TOP, EPD_WIDTH, GxEPD_BLACK);

    // Shift / CapsLock indicator in top-right of keyboard area
    {
        String shiftLabel = _capsLock ? "[CAPS]" : (_shift ? "[SHF]" : "");
        if (!shiftLabel.isEmpty()) {
            int16_t sw = epd.getTextWidth(shiftLabel.c_str(), 12, false);
            epd.drawText(EPD_WIDTH - sw - 4, KB_TOP + 12,
                         shiftLabel.c_str(), 12, true, GxEPD_BLACK);
        }
    }

    for (int row = 0; row < KB_ROWS; ++row) {
        int numKeys = KB_ROW_LENS[row];
        for (int col = 0; col < numKeys; ++col) {
            int16_t kx, ky, kw, kh;
            _softKeyRect(col, row, kx, ky, kw, kh);

            bool selected = (row == _skRow && col == _skCol);

            // Key background
            if (selected) {
                raw.fillRect(kx, ky, kw, kh, GxEPD_BLACK);
            } else {
                raw.fillRect(kx, ky, kw, kh, GxEPD_WHITE);
            }
            raw.drawRect(kx, ky, kw, kh, GxEPD_BLACK);

            // Key label
            uint16_t tc = selected ? GxEPD_WHITE : GxEPD_BLACK;
            char keyChar = KB_ROWS_DATA[row][col];
            String label;

            if (keyChar == '\x01') {
                label = "BK";
            } else if (keyChar == '\x02') {
                label = "SPC";
            } else if (keyChar == '\x03') {
                label = _capsLock ? "CAP" : (_shift ? "SHF" : "shf");
            } else if (keyChar == '\x04') {
                label = "RET";
            } else {
                // Normal character — apply shift / caps
                bool useUpper = _capsLock ^ _shift;
                if (row == 0) {
                    // Digit row: shift gives symbol
                    int idx = col;
                    label = String(useUpper
                                   ? KB_ROW0_SHIFT[idx]
                                   : KB_ROWS_DATA[row][col]);
                } else {
                    char base = KB_ROWS_DATA[row][col];  // Already uppercase letter
                    if (!useUpper) {
                        base = static_cast<char>(base + 32);  // lowercase
                    }
                    label = String(base);
                }
            }

            // Centre text in key
            int16_t lw = epd.getTextWidth(label.c_str(), 12, false);
            int16_t lx = kx + (kw - lw) / 2;
            int16_t ly = ky + (kh / 2) + 5;
            epd.drawText(lx, ly, label.c_str(), 12, false, tc);
        }
    }
}
// NOTE: _drawSoftKeyboardContent ends here.
// The actual display commit (partial or full) is handled by _renderSoftKeyboard.

void NoteEditor::_renderMenu() {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.raw();

    static const char* const MENU_ITEMS[MENU_ITEM_COUNT] = {
        "Save",
        "Connect Keyboard",
        "Exit",
    };

    raw.setFullWindow();
    raw.firstPage();
    do {
        raw.fillScreen(GxEPD_WHITE);

        // Dialog box
        int16_t dx = 60, dy = 60, dw = EPD_WIDTH - 120, dh = 180;
        raw.fillRect(dx, dy, dw, dh, GxEPD_WHITE);
        raw.drawRect(dx, dy, dw, dh, GxEPD_BLACK);
        raw.drawRect(dx + 1, dy + 1, dw - 2, dh - 2, GxEPD_BLACK);

        // Title
        raw.fillRect(dx, dy, dw, 26, GxEPD_BLACK);
        epd.drawText(dx + 10, dy + 18, "Editor Menu", 16, true, GxEPD_WHITE);

        // Menu items
        int16_t iy = dy + 42;
        for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
            bool sel = (i == _menuSelected);
            if (sel) {
                raw.fillRect(dx + 2, iy - 14, dw - 4, 20, GxEPD_BLACK);
            }
            uint16_t tc = sel ? GxEPD_WHITE : GxEPD_BLACK;
            epd.drawText(dx + 12, iy, MENU_ITEMS[i], 16, sel, tc);
            iy += 28;
        }

        // Divider + hint
        raw.drawFastHLine(dx, dy + dh - 24, dw, GxEPD_BLACK);
        epd.drawText(dx + 10, dy + dh - 8,
                     "UP/DN: move  SEL: confirm  BACK: close",
                     12, false, GxEPD_BLACK);

    } while (raw.nextPage());
    epd.update();
}

void NoteEditor::_renderBTConnecting() {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.raw();

    raw.setFullWindow();
    raw.firstPage();
    do {
        raw.fillScreen(GxEPD_WHITE);

        int16_t cx = EPD_WIDTH / 2;
        int16_t cy = EPD_HEIGHT / 2;

        String line1 = "Connecting to:";
        String line2 = _btConnectingName.isEmpty() ? "keyboard..." : _btConnectingName;

        int16_t l1w = epd.getTextWidth(line1.c_str(), 16, false);
        int16_t l2w = epd.getTextWidth(line2.c_str(), 16, true);

        epd.drawText(cx - l1w / 2, cy - 12, line1.c_str(), 16, false, GxEPD_BLACK);
        epd.drawText(cx - l2w / 2, cy + 12, line2.c_str(), 16, true,  GxEPD_BLACK);

    } while (raw.nextPage());
    epd.update();
}

void NoteEditor::_blinkCursor() {
    _cursorVisible = !_cursorVisible;

    int curLine, curCol;
    _cursorToLineCol(curLine, curCol);

    // Only blink if the cursor line is currently visible on screen
    int visLines = TEXT_AREA_H / LINE_H;
    if (curLine < _topLine || curLine >= _topLine + visLines) return;

    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.raw();

    int lineStart = _lineStarts[curLine];
    String beforeCursor = _buffer.substring(lineStart, lineStart + curCol);
    beforeCursor.replace("\n", "");

    int16_t cx  = MARGIN_X + epd.getTextWidth(beforeCursor.c_str(), TEXT_FONT_SZ, false);
    int16_t cy  = TEXT_Y_START + (curLine - _topLine) * LINE_H - LINE_H + 2;
    int16_t ch  = LINE_H - 2;

    // Partial-refresh just the 2-pixel cursor column
    raw.setPartialWindow(cx, cy, 3, ch);
    raw.firstPage();
    do {
        raw.fillRect(cx, cy, 3, ch,
                     _cursorVisible ? GxEPD_BLACK : GxEPD_WHITE);
    } while (raw.nextPage());
}

// ── Word-wrap ──────────────────────────────────────────────────

void NoteEditor::_rewrap() {
    _lineStarts.clear();
    _lineStarts.push_back(0);

    const int len = static_cast<int>(_buffer.length());
    int lineCharCount = 0;

    for (int i = 0; i < len; ++i) {
        char c = _buffer[i];

        if (c == '\n') {
            _lineStarts.push_back(i + 1);
            lineCharCount = 0;
            continue;
        }

        ++lineCharCount;

        if (lineCharCount >= TEXT_COLS) {
            // Find last space to break at (simple word-wrap)
            int breakAt = i;
            for (int j = i; j >= _lineStarts.back(); --j) {
                if (_buffer[j] == ' ') {
                    breakAt = j + 1;
                    break;
                }
            }

            if (breakAt == i) breakAt = i + 1;  // No space found — hard break
            _lineStarts.push_back(breakAt);
            lineCharCount = i - breakAt + 1;
        }
    }
}

void NoteEditor::_cursorToLineCol(int& line, int& col) const {
    line = 0;
    col  = _cursorPos;

    for (int i = static_cast<int>(_lineStarts.size()) - 1; i >= 0; --i) {
        if (_lineStarts[i] <= _cursorPos) {
            line = i;
            col  = _cursorPos - _lineStarts[i];
            break;
        }
    }
}

int NoteEditor::_lineColToCursorPos(int line, int col) const {
    if (line < 0 || line >= static_cast<int>(_lineStarts.size())) {
        return _cursorPos;
    }
    return _lineStarts[line] + col;
}

// ── Soft keyboard geometry ─────────────────────────────────────

void NoteEditor::_softKeyRect(int col, int row,
                               int16_t& x, int16_t& y,
                               int16_t& w, int16_t& h) const {
    // Keyboard occupies bottom KEYBOARD_H pixels (KB_TOP … EPD_HEIGHT).
    // 4 rows: each KEY_H = 28 px tall (4 × 28 = 112 < 120, giving 4 px top pad + 4 px gap).
    //
    // Key widths are computed per-row so all rows fit within EPD_WIDTH (400 px):
    //   Row 0 (12 keys): floor(396/12) = 33 px  → total 396 px, 2 px margin
    //   Row 1 (10 keys): 36 px                   → total 360 px, 20 px margin
    //   Row 2 ( 9 keys): 40 px                   → total 360 px, 20 px margin
    //   Row 3 (10 keys, SPC double-wide):
    //             unit = 34 px → 8 normal + SHF + BKSP = 9 × 34 = 306
    //                          + SPC = 2 × 34 = 68 → total 374 px

    static const int16_t ROW_KEY_W[KB_ROWS] = { 33, 36, 40, 34 };

    int16_t kw = ROW_KEY_W[row];

    // Row 3: SPC key (index 9) is double width
    bool isSpcKey = (row == 3 && col == 9);
    int16_t actualW = isSpcKey ? (kw * 2) : (kw - 1);

    // Row pixel width (for centring) — row 3 has 9 normal + 1 double = 11 units
    int    units   = (row == 3) ? 11 : KB_ROW_LENS[row];
    int16_t rowPixW = static_cast<int16_t>(units * kw);

    int16_t startX = (EPD_WIDTH - rowPixW) / 2;
    int16_t startY = KB_TOP + 4 + row * KEY_H;

    x = startX + col * kw;
    y = startY;
    w = actualW;
    h = KEY_H - 2;
}

char NoteEditor::_softKeyChar() const {
    if (_skRow < 0 || _skRow >= KB_ROWS) return '\0';
    if (_skCol < 0 || _skCol >= KB_ROW_LENS[_skRow]) return '\0';

    char raw = KB_ROWS_DATA[_skRow][_skCol];

    // Special sentinels pass through
    if (raw < 0x20) return raw;

    // Letter rows (1-3): apply case
    if (_skRow >= 1) {
        bool useUpper = _capsLock ^ _shift;
        if (!useUpper) {
            // Letters stored as uppercase, convert to lower
            return static_cast<char>(raw + 32);
        }
        return raw;  // uppercase
    }

    // Digit row (0): apply shift → symbol
    if (_skRow == 0) {
        if (_shift || _capsLock) {
            return KB_ROW0_SHIFT[_skCol];
        }
        return raw;
    }

    return raw;
}

// ── BLE callback installation ──────────────────────────────────

void NoteEditor::_installBLECallback() {
    BLEKeyboardHost::instance().setKeyCallback(
        [](uint8_t keycode, uint8_t modifiers, bool pressed) {
            if (!pressed) return;  // Only act on key-down

            char c = BLEKeyboardHost::hidKeyToChar(keycode, modifiers);

            NoteEditor& ed = NoteEditor::instance();

            if (c == '\b') {
                ed.deleteChar();
            } else if (c == '\n' || c == '\r') {
                ed.insertChar('\n');
            } else if (c != '\0') {
                ed.insertChar(c);
            }

            // Schedule a full redraw (can't call render() from BLE task
            // without risk; set flag and let tick() pick it up)
            ed._needsFullRedraw = true;
        }
    );
}

// ── Private members not defined in header but needed by render ─
// _prevMode tracks mode before entering MENU so we know what to return to.
// It is declared inline as a member below; add a definition guard.
// (Defined as a field directly in the class body in NoteEditor.h — added here
//  as a reminder comment only; no extra definition needed.)
