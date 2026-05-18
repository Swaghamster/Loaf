#pragma once

// ============================================================
// Loaf Firmware — Board & Feature Configuration
// Target: ESP32-C3 (RISC-V, 160 MHz, 4 MB flash, BLE 5.0 LE)
// ============================================================

// ------------------------------------------------------------
// Firmware version
// ------------------------------------------------------------
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0
#define FW_VERSION_STR   "1.0.0"

// ------------------------------------------------------------
// SPI bus (shared by EPD and SD card)
// ------------------------------------------------------------
#define SPI_MOSI  4
#define SPI_MISO  5
#define SPI_SCK   6

// ------------------------------------------------------------
// E-ink display (EPD) — 4.26" 800×480 mono
// Driver: GxEPD2_750_T7 (800×480, swap to panel-specific if available)
// ------------------------------------------------------------
#define PIN_EPD_CS    7
#define PIN_EPD_DC    8
#define PIN_EPD_RST   9
#define PIN_EPD_BUSY  10

#define EPD_WIDTH     800
#define EPD_HEIGHT    480

// Full-refresh threshold: after this many partial updates force a full refresh
#define EPD_PARTIAL_REFRESH_MAX 30

// ------------------------------------------------------------
// SD card — SPI, separate CS
// ------------------------------------------------------------
#define PIN_SD_CS  2

// SdFat SPI speed (Hz)
#define SD_SPI_SPEED SD_SCK_MHZ(20)

// Root paths used by the app layer
#define SD_ROOT          "/"
#define SD_BOOKS_DIR     "/books"
#define SD_NOTES_DIR     "/notes"
#define SD_SETTINGS_FILE "/settings.json"
#define SD_PROGRESS_DIR  "/progress"

// ------------------------------------------------------------
// Physical buttons — Xteink X4 uses an ADC resistor ladder
//
// ADC_PIN_1 (GPIO 1): 4-button ladder → BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT
// ADC_PIN_2 (GPIO 2): 2-button ladder → BTN_UP, BTN_DOWN
// POWER_PIN  (GPIO 3): dedicated digital input (active-low, pull-up)
//
// Button indices (match InputManager from community-sdk):
// ------------------------------------------------------------
#define BUTTON_ADC_PIN_1   1    // GPIO 1 — 4-button ADC ladder
#define BUTTON_ADC_PIN_2   2    // GPIO 2 — 2-button ADC ladder
#define POWER_BUTTON_PIN   3    // GPIO 3 — digital, active-low

// Logical button indices (used throughout UI code)
#define BTN_BACK     0   // ADC1 — go back / cancel
#define BTN_CONFIRM  1   // ADC1 — select / confirm
#define BTN_LEFT     2   // ADC1 — left / prev page
#define BTN_RIGHT    3   // ADC1 — right / next page
#define BTN_UP       4   // ADC2 — scroll up
#define BTN_DOWN     5   // ADC2 — scroll down
#define BTN_POWER    6   // GPIO 3 — power / sleep

#define NUM_BUTTONS  7

// ADC thresholds (12-bit, 0-4095) for resistor ladder — empirically tuned.
// getButtonFromADC(pin1_val) → BTN_BACK/CONFIRM/LEFT/RIGHT or -1 (none)
// getButtonFromADC(pin2_val) → BTN_UP/DOWN or -1 (none)
// No-press voltage sits near 4095 (pull-up to 3.3 V, no path to GND).
#define ADC_THRESHOLD_NONE  3800   // above this → no button on that ladder

// Ladder 1 voltage windows (midpoints between real device averages):
#define ADC1_BACK_MAX     650
#define ADC1_CONFIRM_MAX 1400
#define ADC1_LEFT_MAX    2200
#define ADC1_RIGHT_MAX   3100

// Ladder 2:
#define ADC2_UP_MAX      1400
#define ADC2_DOWN_MAX    3100

// Debounce & long-press timing (ms)
#define BTN_DEBOUNCE_MS        50
#define BTN_LONG_PRESS_MS     800
#define BTN_REPEAT_DELAY_MS   400
#define BTN_REPEAT_INTERVAL_MS 150

// ------------------------------------------------------------
// Font sizes (pixel height, used with Adafruit GFX / GxEPD2)
// ------------------------------------------------------------
#define FONT_SMALL   12
#define FONT_NORMAL  16
#define FONT_LARGE   20
#define FONT_XL      24

// Default reading font size index (0 = SMALL … 3 = XL)
#define FONT_DEFAULT_IDX 1

// ------------------------------------------------------------
// Display layout constants
// ------------------------------------------------------------
#define STATUS_BAR_HEIGHT  20   // px — battery, progress, time
#define MARGIN_X           10   // px — left/right text margin
#define MARGIN_Y            8   // px — top/bottom text margin
#define LINE_SPACING        4   // px — extra leading between lines

// ------------------------------------------------------------
// Bionic Reading modes
// ------------------------------------------------------------
enum class BionicMode : uint8_t {
    BIONIC_OFF    = 0,  // Plain text, no emphasis
    BIONIC_NORMAL = 1,  // Bold first ~half of each word
    BIONIC_SUBTLE = 2   // Bold first ~third of each word (less aggressive)
};

// ------------------------------------------------------------
// Notes application limits
// ------------------------------------------------------------
#define NOTES_MAX_LENGTH   65536   // characters per note
#define NOTES_MAX_COUNT      500   // total notes on SD
#define NOTES_FILENAME_LEN    32   // max filename (without dir)
#define NOTES_TITLE_LEN       64   // max in-note title

// ------------------------------------------------------------
// BLE (Bluetooth Low Energy — BLE 5.0 LE)
// ------------------------------------------------------------
#define BLE_SCAN_TIMEOUT_S      15    // seconds
#define BLE_MAX_PAIRED_DEVICES   5
#define BLE_DEVICE_NAME         "Loaf"
#define BLE_SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define BLE_CHAR_TX_UUID        "12345678-1234-5678-1234-56789abcdef1"
#define BLE_CHAR_RX_UUID        "12345678-1234-5678-1234-56789abcdef2"

// MTU for BLE file transfer chunks (bytes)
#define BLE_CHUNK_SIZE  512

// ------------------------------------------------------------
// Power management
// ------------------------------------------------------------
// Auto-sleep after this many ms of user inactivity (0 = disabled)
#define POWER_AUTO_SLEEP_MS  (5UL * 60UL * 1000UL)  // 5 minutes

// Long-press duration on POWER button to enter deep sleep
#define POWER_LONG_PRESS_SLEEP_MS  1500

// Wakeup sources: BTN_NEXT and BTN_PREV are wired to RTC GPIOs
// ESP32-C3 deep-sleep wakeup via GPIO (active-low → EXT1 or GPIO wakeup)
#define WAKEUP_BTN_BITMASK  ((1ULL << BTN_PREV) | (1ULL << BTN_NEXT))

// ------------------------------------------------------------
// Miscellaneous
// ------------------------------------------------------------
#define SERIAL_BAUD  115200

// GxEPD2 display driver class (4.26" 800×480, partial update capable)
// GxEPD2_750_T7 is 800×480; swap to GxEPD2_426_GDEQ0426T82 once that
// panel header ships with the library version in use.
// Include this only where the display object is instantiated.
// #define LOAF_EPD_CLASS GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT/2>

// ------------------------------------------------------------
// Font identifiers — used by FontManager & reader settings
// ------------------------------------------------------------
enum class FontID : uint8_t {
    SANS        = 0,  // FreeSans — clean sans-serif (default)
    SERIF       = 1,  // FreeSerif — traditional book look
    MONO        = 2,  // FreeMono — monospace / code
    SHARE_TECH  = 3,  // Share Tech Mono — Mr. Robot / hacker terminal
    ORBITRON    = 4,  // Orbitron — Blade Runner / cyberpunk
};

#define FONT_COUNT  5
