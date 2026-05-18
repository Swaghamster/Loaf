#pragma once

#include <Arduino.h>
#include <EInkDisplay.h>
#include "LoafCanvas.h"
#include "../../include/config.h"

// ─────────────────────────────────────────────────────────────────────────────
// EPDDisplay
//
// Singleton wrapper around EInkDisplay (raw framebuffer) + LoafCanvas
// (Adafruit_GFX subclass) for the 4.26" 800×480 Xteink X4 panel.
// Provides text, shape, and bitmap primitives used by higher-level UI code.
//
// Coordinate system: (0,0) = top-left corner.
// For text drawn with custom GFX fonts, (x, y) is the text *baseline*,
// so the ascender rises above y and the descender falls below.
//
// Caller pattern:
//   auto& disp = EPDDisplay::instance();
//   disp.clear();
//   disp.drawText(10, 40, "Hello", FONT_NORMAL);
//   disp.update();          // fast refresh
//   disp.updateFull();      // full (clean) refresh
// ─────────────────────────────────────────────────────────────────────────────
class EPDDisplay {
public:
    // Singleton accessor
    static EPDDisplay& instance();

    // Lifecycle
    void init();
    void hibernate();

    // Buffer management
    // clear() fills the framebuffer with white — does NOT flush to the panel.
    void clear();

    // Flush framebuffer to the physical display.
    void update();        // FAST_REFRESH
    void updateFull();    // FULL_REFRESH (use periodically for best image quality)

    // Partial region refresh (experimental).
    void updatePartial(int16_t x, int16_t y, int16_t w, int16_t h);

    // Text primitives
    // (x, y) is the text baseline (Adafruit GFX convention for custom fonts).
    void drawText(int16_t x, int16_t y,
                  const char* text,
                  uint8_t  fontSize,
                  bool     bold  = false,
                  uint16_t color = 0x0000);   // 0x0000 = black

    void drawTextWrapped(int16_t  x, int16_t  y,
                         uint16_t maxW, uint16_t maxH,
                         const char* text,
                         uint8_t  fontSize,
                         bool     bold = false);

    // Shape primitives (color: 0x0000 = black, 0xFFFF = white)
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    // Bitmap (1-bit packed, MSB-first, PROGMEM-safe)
    void drawBitmap(int16_t x, int16_t y,
                    const uint8_t* bmp,
                    int16_t w, int16_t h);

    // Metrics
    int16_t getTextWidth(const char* text, uint8_t fontSize, bool bold = false);
    int16_t getLineHeight(uint8_t fontSize);

    // Orientation (0-3, passed to Adafruit_GFX — coordinate rotation only)
    void setRotation(uint8_t r);

    // Font face selection (persists until changed)
    void   setFontFace(FontID id);
    FontID currentFontFace() const { return _fontID; }

    // Low-level access for advanced callers (e.g. TextRenderer)
    LoafCanvas& canvas() { return _canvas; }
    EInkDisplay& driver() { return _driver; }

private:
    EPDDisplay() = default;

    // Resolve (current FontID, logical px size, bold) and apply to _canvas.
    void applyFont(uint8_t fontSize, bool bold);

    // ── Hardware driver — pin order: SCLK, MOSI, CS, DC, RST, BUSY ──────────
    EInkDisplay _driver{8, 10, 21, 4, 5, 6};

    // ── GFX rendering canvas backed by _driver's framebuffer ─────────────────
    LoafCanvas _canvas;

    bool   _initialised = false;
    FontID _fontID      = FontID::SANS;
};
