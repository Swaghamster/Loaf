// ============================================================
// Loaf Firmware — main.cpp
// Target: ESP32-C3 (RISC-V, 160 MHz, 16 MB flash, BLE 5.0 LE)
//
// Uses open-x4-sdk InputManager for all button handling.
// Buttons: ADC ladder on GPIO 1 & 2, power button on GPIO 3.
// ============================================================

#include <Arduino.h>
#include <esp_sleep.h>

#include "../include/config.h"
#include "display/EPDDisplay.h"
#include "storage/FileManager.h"
#include "ui/UIManager.h"
#include "terminal/TerminalApp.h"

// Community SDK — real hardware drivers
#include <InputManager.h>

// ── Globals ───────────────────────────────────────────────────────────────────

static InputManager input;
static uint32_t     gLastActivityMs = 0;

// Per-button long-press tracking (InputManager tracks overall held time and
// power button separately, but not per-button long-press for non-power btns).
static bool     gLongFired[7]     = {};
static uint32_t gPressStartMs[7]  = {};

// How many fast refreshes since last full refresh
static uint8_t  gFastRefreshCount = 0;

// ── Forward declarations ──────────────────────────────────────────────────────

static void showSplash();
static void enterDeepSleep();

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println(F("\n=== Loaf " FW_VERSION_STR " booting ==="));

    // 1. E-ink display — owns SPI.begin() internally.
    Serial.println(F("[boot] EPDDisplay init"));
    EPDDisplay::instance().init();

    showSplash();

    // 2. SD card — FileManager::init() calls SdMan.begin() internally.
    Serial.println(F("[boot] FileManager init"));
    if (!FileManager::instance().init()) {
        Serial.println(F("[boot] WARNING: SD card not available"));
    }

    // 4. Buttons.
    input.begin();
    gLastActivityMs = millis();

    // 5. UI.
    Serial.println(F("[boot] UIManager init"));
    UIManager::instance().init();

    // 6. Terminal app (BLE keyboard host must be init'd first if using BLE).
    Serial.println(F("[boot] TerminalApp init"));
    TerminalApp::instance().init();

    Serial.println(F("[boot] Ready"));
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    input.update();

    uint32_t now = millis();

    // ── Dispatch button events ────────────────────────────────────────────────
    for (uint8_t i = 0; i <= InputManager::BTN_POWER; ++i) {

        if (input.wasPressed(i)) {
            // Fresh press — reset long-press tracking for this button.
            gPressStartMs[i] = now;
            gLongFired[i]    = false;
            gLastActivityMs  = now;
        }

        if (input.wasReleased(i)) {
            gLastActivityMs = now;
            if (!gLongFired[i]) {
                // Short press confirmed on release.
                UIManager::instance().handleButton(i, /*longPress=*/false);
            }
            gLongFired[i] = false;
        }

        // Long-press detection while held.
        if (input.isPressed(i) && !gLongFired[i]) {
            uint32_t threshold = (i == InputManager::BTN_POWER)
                                 ? POWER_LONG_PRESS_SLEEP_MS
                                 : BTN_LONG_PRESS_MS;

            if ((now - gPressStartMs[i]) >= threshold) {
                gLongFired[i]   = true;
                gLastActivityMs = now;

                if (i == InputManager::BTN_POWER) {
                    Serial.println(F("[button] POWER long-press → deep sleep"));
                    enterDeepSleep();
                } else {
                    UIManager::instance().handleButton(i, /*longPress=*/true);
                }
            }
        }
    }

    // ── Terminal SSH pump ─────────────────────────────────────────────────────
    TerminalApp::instance().tick();

    // ── Auto-sleep ────────────────────────────────────────────────────────────
    if (POWER_AUTO_SLEEP_MS > 0 &&
        (now - gLastActivityMs) >= POWER_AUTO_SLEEP_MS) {
        Serial.println(F("[power] Auto-sleep timeout"));
        enterDeepSleep();
    }

    delay(10);
}

// ─────────────────────────────────────────────────────────────────────────────
// enterDeepSleep
// ─────────────────────────────────────────────────────────────────────────────

static void enterDeepSleep() {
    Serial.println(F("[power] Entering deep sleep..."));
    Serial.flush();

    EPDDisplay::instance().hibernate();   // deepSleep() on EInkDisplay

    // Only GPIO 3 (POWER button, active-low) can reliably wake the ESP32-C3
    // from deep sleep. ADC pins cannot be used as wakeup sources.
    esp_deep_sleep_enable_gpio_wakeup((1ULL << WAKEUP_GPIO_PIN),
                                      ESP_GPIO_WAKEUP_GPIO_LOW);
    delay(50);
    esp_deep_sleep_start();
}

// ─────────────────────────────────────────────────────────────────────────────
// showSplash
// ─────────────────────────────────────────────────────────────────────────────

static void showSplash() {
    EPDDisplay& epd = EPDDisplay::instance();

    epd.clear();   // fill framebuffer white

    // Centre "Loaf" on 800×480
    int16_t tw = epd.getTextWidth("Loaf", FONT_XL, true);
    int16_t tx = (EPD_WIDTH  - tw) / 2;
    int16_t ty = (EPD_HEIGHT * 2)  / 5;
    epd.drawText(tx, ty, "Loaf", FONT_XL, true, 0x0000);

    const char* sub = "e-ink reader";
    int16_t sw = epd.getTextWidth(sub, FONT_NORMAL, false);
    int16_t sx = (EPD_WIDTH - sw) / 2;
    int16_t sy = ty + epd.getLineHeight(FONT_XL) + 8;
    epd.drawText(sx, sy, sub, FONT_NORMAL, false, 0x0000);

    const char* ver = "v" FW_VERSION_STR;
    int16_t vw = epd.getTextWidth(ver, FONT_SMALL, false);
    int16_t vx = (EPD_WIDTH - vw) / 2;
    int16_t vy = sy + epd.getLineHeight(FONT_NORMAL) + 6;
    epd.drawText(vx, vy, ver, FONT_SMALL, false, 0x0000);

    int16_t lineY  = vy + epd.getLineHeight(FONT_SMALL) + 10;
    epd.drawLine(EPD_WIDTH / 4, lineY, (EPD_WIDTH * 3) / 4, lineY, 0x0000);

    epd.updateFull();   // use full refresh for splash (clean first frame)
    Serial.println(F("[splash] Done"));
}
