#pragma once

// ============================================================
// Loaf Firmware — Board & Feature Configuration
// Target: ESP32-C3 (RISC-V, 160 MHz, 16 MB flash, BLE 5.0 LE)
// Hardware: Xteink X4 (4.26" 800×480 e-ink, 7-button ADC ladder)
// ============================================================

// ------------------------------------------------------------
// Firmware version
// ------------------------------------------------------------
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0
#define FW_VERSION_STR   "1.0.0"

// ------------------------------------------------------------
// E-ink display — EInkDisplay (community SDK)
// Real hardware SPI pins extracted from EInkDisplay README
// ------------------------------------------------------------
#define EPD_SCLK  8
#define EPD_MOSI  10
#define EPD_CS    21
#define EPD_DC    4
#define EPD_RST   5
#define EPD_BUSY  6

#define EPD_WIDTH   800
#define EPD_HEIGHT  480

// Full-refresh threshold: force FULL_REFRESH after this many fast updates
// to clean up ghosting on e-ink panel
#define EPD_FULL_REFRESH_INTERVAL  15

// ------------------------------------------------------------
// SD card — managed by SDCardManager (community SDK, CS=12)
// Root paths used by the app layer
// ------------------------------------------------------------
#define SD_BOOKS_DIR     "/books"
#define SD_NOTES_DIR     "/notes"
#define SD_LOAF_DIR      "/loaf"
#define SD_SETTINGS_FILE "/loaf/settings.json"
#define SD_PROGRESS_DIR  "/loaf/progress"
#define SD_STATS_DIR     "/loaf/stats"
#define SD_DICT_DIR      "/loaf/dict"
#define SD_BT_DIR        "/loaf/bt"

// ------------------------------------------------------------
// Physical buttons — Xteink X4 ADC resistor ladder
// Indices match InputManager (community SDK) constants exactly.
// Do not redefine here — use InputManager::BTN_* in code.
//
//   GPIO 1 (ADC): BTN_BACK=0, BTN_CONFIRM=1, BTN_LEFT=2, BTN_RIGHT=3
//   GPIO 2 (ADC): BTN_UP=4, BTN_DOWN=5
//   GPIO 3 (dig): BTN_POWER=6  (active-low, internal pull-up)
//
// Real device ADC readings (averages from community-sdk source):
//   BACK=3512, CONFIRM=2694, LEFT=1493, RIGHT=5
//   UP=2242,   DOWN=5
// Midpoint ranges in InputManager::ADC_RANGES_1/2.
// ------------------------------------------------------------

// Timing constants (ms)
#define BTN_LONG_PRESS_MS          800
#define POWER_LONG_PRESS_SLEEP_MS  1500

// ------------------------------------------------------------
// Font sizes (logical pixel heights, mapped to pt sizes by FontManager)
// ------------------------------------------------------------
#define FONT_SMALL   12   // → 9pt bitmap
#define FONT_NORMAL  16   // → 12pt bitmap
#define FONT_LARGE   20   // → 18pt bitmap
#define FONT_XL      24   // → 24pt bitmap

#define FONT_DEFAULT_IDX 1  // FONT_NORMAL

// ------------------------------------------------------------
// Display layout constants
// ------------------------------------------------------------
#define STATUS_BAR_HEIGHT  24    // px — top bar (battery, progress, time)
#define MARGIN_X           16    // px — left/right text margin
#define MARGIN_Y           12    // px — top/bottom text margin
#define LINE_SPACING        4    // px — extra leading between lines

// ------------------------------------------------------------
// Bionic Reading modes
// ------------------------------------------------------------
enum class BionicMode : uint8_t {
    BIONIC_OFF    = 0,   // Plain text, no emphasis
    BIONIC_NORMAL = 1,   // Bold first ~half of each word
    BIONIC_SUBTLE = 2    // Bold first ~third of each word
};

// ------------------------------------------------------------
// Notes application limits
// ------------------------------------------------------------
#define NOTES_MAX_LENGTH   65536   // characters per note
#define NOTES_MAX_COUNT      500   // total notes on SD
#define NOTES_FILENAME_LEN    32   // max filename (without dir)
#define NOTES_TITLE_LEN       64   // max in-note title

// ------------------------------------------------------------
// BLE — keyboard host (HID central role)
// ------------------------------------------------------------
#define BLE_SCAN_TIMEOUT_S       15
#define BLE_MAX_PAIRED_DEVICES    5
#define BLE_DEVICE_NAME          "Loaf"
#define BLE_HID_SERVICE_UUID     0x1812

// ------------------------------------------------------------
// Power management
// ------------------------------------------------------------
// Auto-sleep after this many ms of user inactivity (0 = disabled)
#define POWER_AUTO_SLEEP_MS  (5UL * 60UL * 1000UL)  // 5 minutes

// Deep-sleep wakeup: only POWER button (GPIO 3) is reliable
// (ADC pins can't wake from deep sleep)
#define WAKEUP_GPIO_PIN  3   // POWER_BUTTON_PIN

// ------------------------------------------------------------
// Font identifiers — used by FontManager and reader settings
// ------------------------------------------------------------
enum class FontID : uint8_t {
    SANS        = 0,   // FreeSans — clean default
    SERIF       = 1,   // FreeSerif — traditional
    MONO        = 2,   // FreeMono — monospace / code
    SHARE_TECH  = 3,   // Share Tech Mono — Mr. Robot hacker aesthetic
    ORBITRON    = 4,   // Orbitron — Blade Runner cyberpunk aesthetic
};
#define FONT_COUNT  5

// ------------------------------------------------------------
// Miscellaneous
// ------------------------------------------------------------
#define SERIAL_BAUD  115200
