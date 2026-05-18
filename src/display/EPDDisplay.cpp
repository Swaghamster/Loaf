#include "EPDDisplay.h"
#include "../fonts/FontManager.h"
#include <SPI.h>

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
EPDDisplay& EPDDisplay::instance() {
    static EPDDisplay inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::init() {
    if (_initialised) return;

    // Configure the hardware SPI bus for ESP32-C3.
    // MISO is not used by EPD but SPI.begin requires a pin for it; -1 disables.
    SPI.begin(EPD_CLK, /*MISO=*/-1, EPD_MOSI, EPD_CS);

    _display.init(115200, /*initial=*/true, /*reset_duration=*/10,
                  /*pullup_reset=*/false);
    _display.setRotation(0);
    _display.setTextColor(GxEPD_BLACK);
    _display.setFullWindow();

    _initialised = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Orientation
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::setRotation(uint8_t r) {
    _display.setRotation(r & 0x03);
}

// ─────────────────────────────────────────────────────────────────────────────
// Refresh helpers
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::clear() {
    _display.setFullWindow();
    _display.firstPage();
    do {
        _display.fillScreen(GxEPD_WHITE);
    } while (_display.nextPage());
}

void EPDDisplay::update() {
    // Caller is expected to have painted into the page buffer already.
    // This triggers the full-refresh display cycle.
    _display.display(/*partial_update_mode=*/false);
}

void EPDDisplay::updatePartial(int16_t x, int16_t y, int16_t w, int16_t h) {
    _display.setPartialWindow(x, y, w, h);
    _display.displayWindow(x, y, w, h, /*partial=*/true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Power management
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::hibernate() {
    _display.hibernate();
}

// ─────────────────────────────────────────────────────────────────────────────
// Font face selection (persists until changed)
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::setFontFace(FontID id) {
    _fontID = id;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyFont — resolves (current FontID, logical px size, bold) via FontManager
//
// Logical size → bitmap size:
//   ≤12 px → 9pt  |  ≤16 px → 12pt  |  ≤20 px → 18pt  |  else → 24pt
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::applyFont(uint8_t fontSize, bool bold) {
    const GFXfont* f = FontManager::instance().resolve(_fontID, fontSize, bold);
    _display.setFont(f);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawText
//
// (x, y) is the baseline anchor (Adafruit GFX convention for custom fonts).
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawText(int16_t x, int16_t y,
                          const char* text,
                          uint8_t fontSize,
                          bool bold,
                          uint16_t color) {
    if (!text || *text == '\0') return;

    applyFont(fontSize, bold);
    _display.setTextColor(color);
    _display.setCursor(x, y);
    _display.print(text);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawTextWrapped
//
// Draws text inside the bounding box (x, y, maxW, maxH).
// Word-wraps by splitting on spaces; truncates vertically at maxH.
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawTextWrapped(int16_t x, int16_t y,
                                 uint16_t maxW, uint16_t maxH,
                                 const char* text,
                                 uint8_t fontSize,
                                 bool bold) {
    if (!text || *text == '\0') return;

    applyFont(fontSize, bold);
    _display.setTextColor(GxEPD_BLACK);

    const int16_t lineH      = getLineHeight(fontSize);
    int16_t       cursorX    = x;
    int16_t       cursorY    = y + lineH;   // baseline of first line
    const int16_t rightEdge  = x + (int16_t)maxW;
    const int16_t bottomEdge = y + (int16_t)maxH;

    // Measure a single space for inter-word gap
    int16_t  sx1, sy1;
    uint16_t sw, sh;
    _display.getTextBounds(" ", 0, 0, &sx1, &sy1, &sw, &sh);
    const int16_t spaceW = (int16_t)sw;

    // Tokenise into words (split on whitespace / newlines)
    String src(text);
    int    start = 0;
    int    len   = (int)src.length();

    while (start < len) {
        // Skip spaces (but handle newlines as forced line-break)
        while (start < len && src[start] == ' ') {
            ++start;
        }
        if (start >= len) break;

        if (src[start] == '\n') {
            ++start;
            cursorX = x;
            cursorY += lineH;
            if (cursorY > bottomEdge) return;
            continue;
        }

        // Find end of word (next space or newline)
        int wordEnd = start;
        while (wordEnd < len && src[wordEnd] != ' ' && src[wordEnd] != '\n') {
            ++wordEnd;
        }

        String word = src.substring(start, wordEnd);
        start = wordEnd;

        // Measure word width
        int16_t  x1, y1;
        uint16_t ww, wh;
        _display.getTextBounds(word.c_str(), 0, 0, &x1, &y1, &ww, &wh);
        int16_t wordW = (int16_t)ww;

        // Wrap to next line if word doesn't fit on current line
        if (cursorX > x && (cursorX + wordW) > rightEdge) {
            cursorX = x;
            cursorY += lineH;
            if (cursorY > bottomEdge) return;
        }

        _display.setCursor(cursorX, cursorY);
        _display.print(word.c_str());
        cursorX += wordW + spaceW;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape primitives
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) {
    _display.drawRect(x, y, w, h, color);
}

void EPDDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) {
    _display.fillRect(x, y, w, h, color);
}

void EPDDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          uint16_t color) {
    _display.drawLine(x0, y0, x1, y1, color);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawBitmap
//
// Draws a 1-bit packed bitmap (1 = black, 0 = white, MSB first).
// The bitmap data is expected to be stored in PROGMEM.
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawBitmap(int16_t x, int16_t y,
                            const uint8_t* bmp,
                            int16_t w, int16_t h) {
    // GxEPD2 inherits Adafruit_GFX::drawBitmap; the bitmap is 1-bit packed,
    // 1 = foreground (black), 0 = background (white).
    _display.drawBitmap(x, y, bmp, w, h, GxEPD_BLACK, GxEPD_WHITE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Metrics
// ─────────────────────────────────────────────────────────────────────────────
int16_t EPDDisplay::getTextWidth(const char* text, uint8_t fontSize, bool bold) {
    if (!text || *text == '\0') return 0;
    applyFont(fontSize, bold);
    int16_t  x1, y1;
    uint16_t w, h;
    _display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (int16_t)w;
}

int16_t EPDDisplay::getLineHeight(uint8_t fontSize) {
    // Line heights empirically chosen for GFX fonts + comfortable leading.
    // Values include the font ascender + descender + inter-line spacing.
    switch (fontSize) {
        case 12: return 16;
        case 16: return 20;
        case 20: return 26;
        case 24: return 32;
        default: return 20;
    }
}
