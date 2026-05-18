#pragma once

// ============================================================
// NoteEditor.h
// Loaf Firmware — Full-screen plain-text note editor
//
// Supports two input modes:
//   1. Soft keyboard — QWERTY grid drawn on the bottom 120 px
//      of the EPD; physical buttons navigate the key grid and
//      confirm selection.
//   2. BLE keyboard — when a BLE HID keyboard is connected via
//      BLEKeyboardHost, all keystrokes are routed directly into
//      the text buffer and the soft keyboard is hidden.
//
// Button mapping (no BLE keyboard connected):
//   BTN_UP   (0) — move soft-key cursor up one row
//   BTN_DOWN (1) — move soft-key cursor down one row
//   BTN_SELECT (2) — press the highlighted soft key
//   BTN_BACK (3) — open editor menu
//
// Button mapping (BLE keyboard connected):
//   BTN_UP/DOWN — cursor navigation in text (move by line)
//   BTN_SELECT  — unused / toggle shift
//   BTN_BACK    — open editor menu
//
// Editor menu items:
//   0  Save
//   1  Connect Keyboard  (→ BT scan UI)
//   2  Exit
// ============================================================

#include <Arduino.h>
#include <functional>
#include <vector>
#include "../../include/config.h"

// Forward declarations — full headers included in NoteEditor.cpp
class EPDDisplay;

// ── Editor UI states ──────────────────────────────────────────
enum class EditorMode : uint8_t {
    SOFT_KEYBOARD,   ///< On-screen QWERTY keyboard active
    BLE_KEYBOARD,    ///< External BLE keyboard active; no soft-key grid
    MENU,            ///< Editor menu overlay
    BT_SCAN,         ///< Showing BT device scan results
    BT_CONNECTING,   ///< "Connecting…" splash
};

// ─────────────────────────────────────────────────────────────
// NoteEditor
// ─────────────────────────────────────────────────────────────
class NoteEditor {
public:
    // Singleton accessor
    static NoteEditor& instance();

    // Prevent copy / move
    NoteEditor(const NoteEditor&)            = delete;
    NoteEditor& operator=(const NoteEditor&) = delete;
    NoteEditor(NoteEditor&&)                 = delete;
    NoteEditor& operator=(NoteEditor&&)      = delete;

    // ── Lifecycle ─────────────────────────────────────────────

    /// Open a note for editing.
    /// filename        — bare filename (no dir), used when saving.
    /// initialContent  — full text content already loaded from SD.
    ///                   Pass an empty String for a new note.
    void init(const String& filename, const String& initialContent);

    // ── Main loop hooks ───────────────────────────────────────

    /// Render the editor onto the EPD.  Full refresh on first call;
    /// subsequent calls use partial refresh for the cursor / text area.
    void render();

    /// Called by UIManager's button handler.
    void handleButton(uint8_t btn, bool longPress);

    /// Called from the main loop to service auto-save and BLE state.
    /// Must be called periodically (e.g. every iteration of loop()).
    void tick();

    // ── Text buffer manipulation ──────────────────────────────

    /// Insert character c at the current cursor position.
    void insertChar(char c);

    /// Delete the character immediately before the cursor (backspace).
    void deleteChar();

    /// Move the cursor by delta characters (negative = left / back).
    void moveCursor(int delta);

    /// Move the cursor up or down by `lines` display lines.
    /// (Finds the nearest column-equivalent position in the target line.)
    void moveCursorLine(int lines);

    // ── Persistence ───────────────────────────────────────────

    /// Write the buffer to SD at SD_NOTES_DIR/<_filename>.
    void save();

    /// Returns true if the buffer has been modified since last save.
    bool hasUnsavedChanges() const { return _dirty; }

    // ── BT keyboard UI ────────────────────────────────────────

    /// Transition to BT_SCAN mode: start BLE scan, show spinner.
    void openBTKeyboardMenu();

    /// Draw the BT device list (scan results) on the EPD.
    void renderBTMenu();

    /// True when NoteEditor is done and control should return to NotesApp.
    bool isDone() const { return _done; }

private:
    NoteEditor() = default;

    // ── Internal render helpers ───────────────────────────────

    /// Full-redraw of the text area (top EPD_HEIGHT - KEYBOARD_H pixels).
    void _renderTextArea();

    /// Draw just the soft-keyboard grid (bottom KEYBOARD_H pixels).
    /// doPartialFlush=true: owns its own firstPage/nextPage + partial window commit.
    /// doPartialFlush=false: draws into an already-open firstPage/nextPage loop.
    void _renderSoftKeyboard(bool doPartialFlush = true);

    /// Low-level drawing primitive used by _renderSoftKeyboard().
    /// Must be called inside a firstPage/nextPage loop.
    void _drawSoftKeyboardContent(EPDDisplay& epd);

    /// Draw the editor-menu overlay.
    void _renderMenu();

    /// Draw the "Connecting…" / "Connected to X" splash.
    void _renderBTConnecting();

    /// Blink the text cursor via partial EPD refresh.
    void _blinkCursor();

    // ── Soft keyboard helpers ─────────────────────────────────

    /// Return the character at the current soft-key grid position,
    /// respecting the _shift state.
    char _softKeyChar() const;

    /// Return the pixel rect (x, y, w, h) for soft-key at (col, row).
    void _softKeyRect(int col, int row,
                      int16_t& x, int16_t& y, int16_t& w, int16_t& h) const;

    // ── Word-wrap helper ──────────────────────────────────────

    /// Recompute _lines: split _buffer into display lines of at most
    /// TEXT_COLS characters, honouring hard newlines.
    void _rewrap();

    /// Convert a linear cursor position (_cursorPos) to (col, row) in _lines.
    void _cursorToLineCol(int& line, int& col) const;

    /// Convert (line, col) in _lines back to a linear _cursorPos.
    int  _lineColToCursorPos(int line, int col) const;

    // ── BLE callback (installed in init / openBTKeyboardMenu) ─
    void _installBLECallback();

    // ── Auto-save ─────────────────────────────────────────────
    static constexpr uint32_t AUTOSAVE_MS = 30000UL;  ///< 30 seconds

    // ── Layout constants ──────────────────────────────────────
    static constexpr int16_t KEYBOARD_H    = 120;  ///< Soft keyboard height (px)
    static constexpr int16_t TEXT_AREA_H   = EPD_HEIGHT - KEYBOARD_H - 20;
                                                   ///< 300 - 120 - 20 = 160 px
    static constexpr int16_t STATUS_BAR_H  = 20;  ///< Top status bar
    static constexpr int16_t MARGIN_X      = 8;   ///< Text left/right margin
    static constexpr int16_t TEXT_Y_START  = STATUS_BAR_H + 18; ///< Baseline of 1st line
    static constexpr int16_t TEXT_FONT_SZ  = 16;  ///< Body text font size
    static constexpr int16_t LINE_H        = 20;  ///< Pixels per text line
    static constexpr int      TEXT_COLS    = 46;  ///< Approximate chars per line at 16pt

    // Soft keyboard geometry
    static constexpr int     KB_ROWS       = 4;   ///< Number of key rows
    static constexpr int     KB_COLS_MAX   = 11;  ///< Widest row (numbers)
    static constexpr int16_t KEY_W         = 36;  ///< Key width  (px)
    static constexpr int16_t KEY_H         = 28;  ///< Key height (px) — fits 4 rows in 120px
    static constexpr int16_t KB_TOP        = EPD_HEIGHT - KEYBOARD_H; ///< Y origin of keyboard

    // Editor menu
    static constexpr int MENU_ITEM_COUNT   = 3;

    // ── State ─────────────────────────────────────────────────

    String    _filename;           ///< Bare filename for SD storage
    String    _buffer;             ///< Full note content
    int       _cursorPos = 0;      ///< Linear char position in _buffer
    bool      _dirty     = false;  ///< Buffer modified since last save
    bool      _done      = false;  ///< True when editor should close

    EditorMode _mode     = EditorMode::SOFT_KEYBOARD;

    // Word-wrap state
    // _lines[i] = starting char index of display line i
    std::vector<int> _lineStarts;
    int _topLine = 0;              ///< First visible display line

    // Soft keyboard state
    int  _skRow   = 1;            ///< Current soft-key row (0 = number/sym row)
    int  _skCol   = 0;            ///< Current soft-key col
    bool _shift   = false;        ///< Shift engaged (one-shot)
    bool _capsLock = false;       ///< Caps lock state

    // Cursor blink
    bool     _cursorVisible  = true;
    uint32_t _lastBlinkMs    = 0;
    static constexpr uint32_t BLINK_MS = 600;

    // Auto-save
    uint32_t _lastSaveMs = 0;

    // Editor menu
    int      _menuSelected = 0;

    // BT scan state
    int      _btScanSelected = 0;  ///< Highlighted device in scan list
    bool     _btConnecting   = false;
    String   _btConnectingName;
    uint32_t _btScanStartMs  = 0;
    static constexpr uint32_t SCAN_SPINNER_MS = 300;
    int      _scanSpinnerIdx  = 0;

    bool _needsFullRedraw = true;
    EditorMode _prevMode  = EditorMode::SOFT_KEYBOARD; ///< Mode before entering MENU

    // ── Soft keyboard layout ──────────────────────────────────
    //  Row 0: numbers  1234567890-
    //  Row 1: QWERTYUIOP
    //  Row 2: ASDFGHJKL
    //  Row 3: ZXCVBNM  [BKSP] [SPC]
    //
    // Stored as plain C-strings; shift variants for symbols handled in _softKeyChar.

    static const char* const KB_ROW0;   // "1234567890-="
    static const char* const KB_ROW1;   // "QWERTYUIOP"
    static const char* const KB_ROW2;   // "ASDFGHJKL"
    static const char* const KB_ROW3;   // "ZXCVBNM"   + BKSP + SPC special keys

    // Number of keys in each row (including special keys for row 3)
    static const int KB_ROW_LENS[KB_ROWS];
};
