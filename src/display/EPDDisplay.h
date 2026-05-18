#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_750_T7.h>   // 800×480 — closest stock GxEPD2 match for X4

// Free fonts shipped with GxEPD2 (via Adafruit GFX)
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// ── SPI pin assignments ────────────────────────────────────────────────────
static constexpr uint8_t EPD_MOSI = 4;
static constexpr uint8_t EPD_CLK  = 6;
static constexpr uint8_t EPD_CS   = 7;
static constexpr uint8_t EPD_DC   = 8;
static constexpr uint8_t EPD_RST  = 9;
static constexpr uint8_t EPD_BUSY = 10;

// ── Display geometry ──────────────────────────────────────────────────────
static constexpr int16_t EPD_WIDTH  = 800;
static constexpr int16_t EPD_HEIGHT = 480;

// ── Supported logical font sizes ──────────────────────────────────────────
enum class FontSize : uint8_t {
    PT12 = 12,
    PT16 = 16,
    PT20 = 20,
    PT24 = 24,
};

// ─────────────────────────────────────────────────────────────────────────────
// EPDDisplay
//
// Singleton wrapper around GxEPD2_BW for the 4.26" 800×480 Xteink X4 panel.
// Provides text, shape, and bitmap primitives used by higher-level UI code.
//
// Coordinate system: (0,0) = top-left corner.
// For text drawn with custom GFX fonts, (x, y) is the text *baseline*,
// so the ascender rises above y and the descender falls below.
// ─────────────────────────────────────────────────────────────────────────────
class EPDDisplay {
public:
    // Singleton accessor
    static EPDDisplay& instance();

    // Lifecycle
    void init();
    void hibernate();

    // Full / partial refresh
    void clear();
    void update();
    void updatePartial(int16_t x, int16_t y, int16_t w, int16_t h);

    // Text primitives
    void drawText(int16_t x, int16_t y,
                  const char* text,
                  uint8_t fontSize,
                  bool    bold  = false,
                  uint16_t color = GxEPD_BLACK);

    void drawTextWrapped(int16_t x, int16_t y,
                         uint16_t maxW, uint16_t maxH,
                         const char* text,
                         uint8_t fontSize,
                         bool bold = false);

    // Shape primitives
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    // Bitmap (1-bit, PROGMEM-safe)
    void drawBitmap(int16_t x, int16_t y,
                    const uint8_t* bmp,
                    int16_t w, int16_t h);

    // Metrics
    int16_t getTextWidth(const char* text, uint8_t fontSize, bool bold = false);
    int16_t getLineHeight(uint8_t fontSize);

    // Orientation (0-3)
    void setRotation(uint8_t r);

    // Font face selection (called before text primitives)
    void setFontFace(FontID id);
    FontID currentFontFace() const { return _fontID; }

    // Raw display access for advanced callers (e.g. TextRenderer)
    GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT/2>& raw() { return _display; }

private:
    EPDDisplay() = default;

    // Select and apply the GFX font matching (fontSize, bold)
    void applyFont(uint8_t fontSize, bool bold);

    // Underlying driver instance (800×480, half-height page buffer to fit in RAM)
    GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT/2> _display{
        GxEPD2_750_T7(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
    };

    bool   _initialised = false;
    FontID _fontID      = FontID::SANS;

};
