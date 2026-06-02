#include "EPDDisplay.h"
#include "LoafCanvas.h"
#include "../fonts/FontManager.h"
#include <EInkDisplay.h>

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
EPDDisplay& EPDDisplay::instance() {
    static EPDDisplay inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// init
//
// Initialises the EInkDisplay hardware and binds the LoafCanvas to the live
// framebuffer returned by the driver.  Safe to call multiple times.
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::init() {
    if (_initialised) return;

    _driver.begin();
    _canvas.setFrameBuffer(_driver.getFrameBuffer(),
                           _driver.getDisplayWidthBytes());
    _canvas.setRotation(0);

    _initialised = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Orientation
//
// Delegates to Adafruit_GFX which handles logical coordinate rotation.
// The EInkDisplay framebuffer is always stored in physical (portrait) layout;
// LoafCanvas::drawPixel maps rotated GFX coordinates to the correct byte.
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::setRotation(uint8_t r) {
    _canvas.setRotation(r & 0x03);
}

// ─────────────────────────────────────────────────────────────────────────────
// clear
//
// Fills the framebuffer with 0xFF (all white).  Does NOT flush to the panel.
// Call update() or updateFull() after drawing to make changes visible.
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::clear() {
    _driver.clearScreen(0xFF);
}

// ─────────────────────────────────────────────────────────────────────────────
// update / updateFull / updatePartial
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::update() {
    _driver.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void EPDDisplay::updateFull() {
    _driver.displayBuffer(EInkDisplay::FULL_REFRESH);
}

void EPDDisplay::updatePartial(int16_t x, int16_t y, int16_t w, int16_t h) {
    _driver.displayWindow(x, y, w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Power management
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::hibernate() {
    _driver.deepSleep();
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
// Logical size → point size:
//   ≤12 px → 9pt  |  ≤16 px → 12pt  |  ≤20 px → 18pt  |  else → 24pt
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::applyFont(uint8_t fontSize, bool bold) {
    const GFXfont* f = FontManager::instance().resolve(_fontID, fontSize, bold);
    _canvas.setFont(f);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawText
//
// (x, y) is the baseline anchor (Adafruit GFX convention for custom fonts).
// color: 0x0000 = black, 0xFFFF = white (any other value → white).
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawText(int16_t x, int16_t y,
                          const char* text,
                          uint8_t  fontSize,
                          bool     bold,
                          uint16_t color) {
    if (!text || *text == '\0') return;

    applyFont(fontSize, bold);
    _canvas.setTextColor(color == 0x0000u ? 0x0000u : 0xFFFFu);
    _canvas.setCursor(x, y);
    _canvas.print(text);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawTextWrapped
//
// Draws text inside the bounding box (x, y, maxW, maxH).
// Word-wraps by splitting on spaces; newlines force a line-break.
// Truncates vertically when the next line would exceed y + maxH.
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawTextWrapped(int16_t  x, int16_t  y,
                                 uint16_t maxW, uint16_t maxH,
                                 const char* text,
                                 uint8_t  fontSize,
                                 bool     bold) {
    if (!text || *text == '\0') return;

    applyFont(fontSize, bold);
    _canvas.setTextColor(0x0000u);   // black

    const int16_t lineH      = getLineHeight(fontSize);
    int16_t       cursorX    = x;
    int16_t       cursorY    = y + lineH;   // baseline of first line
    const int16_t rightEdge  = x + (int16_t)maxW;
    const int16_t bottomEdge = y + (int16_t)maxH;

    // Measure a single space for inter-word gap.
    int16_t  sx1, sy1;
    uint16_t sw, sh;
    _canvas.getTextBounds(" ", 0, 0, &sx1, &sy1, &sw, &sh);
    const int16_t spaceW = (int16_t)sw;

    // Tokenise into words (split on whitespace / newlines).
    String src(text);
    int    start = 0;
    int    len   = (int)src.length();

    while (start < len) {
        // Skip spaces (handle newlines as forced line-break).
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

        // Find end of word (next space or newline).
        int wordEnd = start;
        while (wordEnd < len && src[wordEnd] != ' ' && src[wordEnd] != '\n') {
            ++wordEnd;
        }

        String word = src.substring(start, wordEnd);
        start = wordEnd;

        // Measure word width.
        int16_t  x1, y1;
        uint16_t ww, wh;
        _canvas.getTextBounds(word.c_str(), 0, 0, &x1, &y1, &ww, &wh);
        int16_t wordW = (int16_t)ww;

        // Wrap to next line if word doesn't fit on current line.
        if (cursorX > x && (cursorX + wordW) > rightEdge) {
            cursorX = x;
            cursorY += lineH;
            if (cursorY > bottomEdge) return;
        }

        _canvas.setCursor(cursorX, cursorY);
        _canvas.print(word.c_str());
        cursorX += wordW + spaceW;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape primitives
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) {
    _canvas.drawRect(x, y, w, h, color);
}

void EPDDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) {
    _canvas.fillRect(x, y, w, h, color);
}

void EPDDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          uint16_t color) {
    _canvas.drawLine(x0, y0, x1, y1, color);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawBitmap
//
// 1-bit packed bitmap, MSB first (1 = black, 0 = white).
// PROGMEM bitmaps are handled transparently by Adafruit_GFX.
// ─────────────────────────────────────────────────────────────────────────────
void EPDDisplay::drawBitmap(int16_t x, int16_t y,
                            const uint8_t* bmp,
                            int16_t w, int16_t h) {
    _canvas.drawBitmap(x, y, bmp, w, h, 0x0000u, 0xFFFFu);
}

// ─────────────────────────────────────────────────────────────────────────────
// Metrics
// ─────────────────────────────────────────────────────────────────────────────
int16_t EPDDisplay::getTextWidth(const char* text, uint8_t fontSize, bool bold) {
    if (!text || *text == '\0') return 0;
    applyFont(fontSize, bold);
    int16_t  x1, y1;
    uint16_t w, h;
    _canvas.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (int16_t)w;
}

int16_t EPDDisplay::getLineHeight(uint8_t fontSize) {
    // Line heights chosen for GFX fonts + comfortable leading.
    // Values include the font ascender + descender + inter-line spacing.
    switch (fontSize) {
        case 12: return 16;
        case 16: return 20;
        case 20: return 26;
        case 24: return 32;
        default: return 20;
    }
}
