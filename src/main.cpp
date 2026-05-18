// ============================================================
// Loaf Firmware — main.cpp
// Target: ESP32-C3 (RISC-V, 160 MHz, 16 MB flash, BLE 5.0 LE)
//
// Xteink X4 button layout (7 buttons via ADC resistor ladder):
//   GPIO 1 (ADC1): BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT
//   GPIO 2 (ADC2): BTN_UP, BTN_DOWN
//   GPIO 3 (digital, active-low): BTN_POWER
//
// Boot sequence:
//   1. Serial
//   2. E-ink display (EPDDisplay) — owns SPI.begin()
//   3. Splash screen
//   4. SD card / FileManager
//   5. Button ADC init
//   6. UIManager
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>

#include "../include/config.h"
#include "display/EPDDisplay.h"
#include "storage/FileManager.h"
#include "ui/UIManager.h"

// ── Forward declarations ──────────────────────────────────────────────────────
static void     initButtons();
static void     pollButtons();
static void     enterDeepSleep();
static void     showSplash();
static int8_t   getButtonFromADC1(int raw);
static int8_t   getButtonFromADC2(int raw);

// ─────────────────────────────────────────────────────────────────────────────
// Button state
//
// Each of the 7 logical buttons has its own ButtonState.  The ADC pins are
// sampled every loop tick; the active button on each ladder is decoded and
// fed into the same debounce / edge-detection path as a digital button.
// ─────────────────────────────────────────────────────────────────────────────

struct ButtonState {
    bool     isDown       = false;
    bool     longFired    = false;
    uint32_t pressStartMs = 0;
    uint32_t lastChangeMs = 0;
    bool     pendingDown  = false;  // raw decoded state, pre-debounce
    uint32_t rawChangeMs  = 0;
};

static ButtonState gBtns[NUM_BUTTONS];
static uint32_t    gLastActivityMs = 0;

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);

    Serial.println();
    Serial.println(F("=== Loaf " FW_VERSION_STR " booting ==="));

    // E-ink display — must come before any UI code; owns SPI.begin().
    Serial.println(F("[boot] EPDDisplay init"));
    EPDDisplay::instance().init();

    showSplash();

    // SD card — non-fatal if absent; UI shows a warning.
    Serial.println(F("[boot] FileManager init"));
    if (!FileManager::instance().init()) {
        Serial.println(F("[boot] WARNING: SD card not available"));
    }

    initButtons();
    gLastActivityMs = millis();

    Serial.println(F("[boot] UIManager init"));
    UIManager::instance().init();

    Serial.println(F("[boot] Ready"));
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    pollButtons();

    if (POWER_AUTO_SLEEP_MS > 0 &&
        (millis() - gLastActivityMs) >= POWER_AUTO_SLEEP_MS) {
        Serial.println(F("[power] Auto-sleep timeout"));
        enterDeepSleep();
    }

    delay(10);
}

// ─────────────────────────────────────────────────────────────────────────────
// ADC decode helpers
//
// Returns the logical BTN_* index for the pressed button, or -1 for none.
// Thresholds are defined in config.h (ADC1_*/ADC2_*).
// ─────────────────────────────────────────────────────────────────────────────

static int8_t getButtonFromADC1(int v) {
    if (v > ADC_THRESHOLD_NONE)   return -1;           // no button
    if (v <= ADC1_BACK_MAX)       return BTN_BACK;
    if (v <= ADC1_CONFIRM_MAX)    return BTN_CONFIRM;
    if (v <= ADC1_LEFT_MAX)       return BTN_LEFT;
    if (v <= ADC1_RIGHT_MAX)      return BTN_RIGHT;
    return -1;
}

static int8_t getButtonFromADC2(int v) {
    if (v > ADC_THRESHOLD_NONE)   return -1;
    if (v <= ADC2_UP_MAX)         return BTN_UP;
    if (v <= ADC2_DOWN_MAX)       return BTN_DOWN;
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// initButtons
// ─────────────────────────────────────────────────────────────────────────────

static void initButtons() {
    // ADC pins — no pull-up needed (ladder handles it), just set to INPUT.
    pinMode(BUTTON_ADC_PIN_1, INPUT);
    pinMode(BUTTON_ADC_PIN_2, INPUT);

    // Power button — digital, active-low with internal pull-up.
    pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

    uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_BUTTONS; ++i) {
        gBtns[i] = ButtonState{};
        gBtns[i].lastChangeMs = now;
        gBtns[i].rawChangeMs  = now;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// pollButtons
//
// Called every ~10 ms from loop().
//
// 1. Sample ADC1, ADC2, and POWER GPIO.
// 2. Decode which logical button is active on each pin.
// 3. Run debounce + edge detection per button.
// 4. Dispatch to UIManager on press/long-press.
// ─────────────────────────────────────────────────────────────────────────────

static void pollButtons() {
    uint32_t now = millis();

    // --- Sample hardware ---
    int adc1 = analogRead(BUTTON_ADC_PIN_1);
    int adc2 = analogRead(BUTTON_ADC_PIN_2);
    bool powerRaw = (digitalRead(POWER_BUTTON_PIN) == LOW);  // active-low

    // Build a raw "is down" map for all 7 buttons.
    bool rawDown[NUM_BUTTONS] = {};

    int8_t a1btn = getButtonFromADC1(adc1);
    int8_t a2btn = getButtonFromADC2(adc2);

    if (a1btn >= 0 && a1btn < (int8_t)NUM_BUTTONS) rawDown[a1btn] = true;
    if (a2btn >= 0 && a2btn < (int8_t)NUM_BUTTONS) rawDown[a2btn] = true;
    rawDown[BTN_POWER] = powerRaw;

    // --- Debounce + edge detection per button ---
    for (uint8_t i = 0; i < NUM_BUTTONS; ++i) {
        ButtonState& b   = gBtns[i];
        bool         raw = rawDown[i];

        // Detect raw state change and reset debounce timer.
        if (raw != b.pendingDown) {
            b.pendingDown = raw;
            b.rawChangeMs = now;
        }

        // Wait for signal to be stable for BTN_DEBOUNCE_MS.
        if ((now - b.rawChangeMs) < BTN_DEBOUNCE_MS) continue;

        bool wasDown = b.isDown;

        if (raw && !wasDown) {
            // --- Press edge ---
            b.isDown       = true;
            b.pressStartMs = now;
            b.longFired    = false;
            b.lastChangeMs = now;
            gLastActivityMs = now;

        } else if (!raw && wasDown) {
            // --- Release edge ---
            b.isDown       = false;
            b.lastChangeMs = now;

            if (!b.longFired) {
                // Short press — dispatch.
                UIManager::instance().handleButton(i, /*longPress=*/false);
            }

        } else if (raw && wasDown && !b.longFired) {
            // --- Button held: check for long-press threshold ---
            uint32_t threshold = (i == BTN_POWER)
                ? POWER_LONG_PRESS_SLEEP_MS
                : BTN_LONG_PRESS_MS;

            if ((now - b.pressStartMs) >= threshold) {
                b.longFired     = true;
                gLastActivityMs = now;

                if (i == BTN_POWER) {
                    Serial.println(F("[button] POWER long-press → deep sleep"));
                    enterDeepSleep();
                } else {
                    UIManager::instance().handleButton(i, /*longPress=*/true);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// enterDeepSleep
// ─────────────────────────────────────────────────────────────────────────────

static void enterDeepSleep() {
    Serial.println(F("[power] Entering deep sleep..."));
    Serial.flush();

    EPDDisplay::instance().hibernate();

    // Wake on POWER button (GPIO 3, active-low → wake on LOW).
    // BTN_LEFT (ADC1) and BTN_DOWN (ADC2) can't wake from deep sleep via ADC;
    // power button is the only reliable wake source.
    esp_deep_sleep_enable_gpio_wakeup((1ULL << POWER_BUTTON_PIN),
                                      ESP_GPIO_WAKEUP_GPIO_LOW);

    delay(50);
    esp_deep_sleep_start();
}

// ─────────────────────────────────────────────────────────────────────────────
// showSplash
//
// Layout centred on 800×480 display:
//   "Loaf"          — FONT_XL bold, ~40 % height
//   "e-ink reader"  — FONT_NORMAL, just below
//   "v1.0.0"        — FONT_SMALL, below subtitle
//   thin rule
// ─────────────────────────────────────────────────────────────────────────────

static void showSplash() {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw       = epd.raw();

    raw.setFullWindow();
    raw.firstPage();
    do {
        raw.fillScreen(GxEPD_WHITE);

        int16_t tw = epd.getTextWidth("Loaf", FONT_XL, true);
        int16_t tx = (EPD_WIDTH - tw) / 2;
        int16_t ty = (EPD_HEIGHT * 2) / 5;
        epd.drawText(tx, ty, "Loaf", FONT_XL, true, GxEPD_BLACK);

        const char* sub = "e-ink reader";
        int16_t sw = epd.getTextWidth(sub, FONT_NORMAL, false);
        int16_t sx = (EPD_WIDTH - sw) / 2;
        int16_t sy = ty + epd.getLineHeight(FONT_XL) + 8;
        epd.drawText(sx, sy, sub, FONT_NORMAL, false, GxEPD_BLACK);

        const char* ver = "v" FW_VERSION_STR;
        int16_t vw = epd.getTextWidth(ver, FONT_SMALL, false);
        int16_t vx = (EPD_WIDTH - vw) / 2;
        int16_t vy = sy + epd.getLineHeight(FONT_NORMAL) + 6;
        epd.drawText(vx, vy, ver, FONT_SMALL, false, GxEPD_BLACK);

        int16_t lineY  = vy + epd.getLineHeight(FONT_SMALL) + 10;
        int16_t lineX0 = EPD_WIDTH / 4;
        int16_t lineX1 = (EPD_WIDTH * 3) / 4;
        epd.drawLine(lineX0, lineY, lineX1, lineY, GxEPD_BLACK);

    } while (raw.nextPage());

    Serial.println(F("[splash] Done"));
}
