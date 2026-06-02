#pragma once
#include <Adafruit_GFX.h>
#include <EInkDisplay.h>

class LoafCanvas : public Adafruit_GFX {
public:
    LoafCanvas()
        : Adafruit_GFX(EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT),
          _fb(nullptr), _wBytes(EInkDisplay::DISPLAY_WIDTH_BYTES) {}

    void setFrameBuffer(uint8_t* fb, uint16_t widthBytes) {
        _fb = fb;
        _wBytes = widthBytes;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (!_fb || x < 0 || y < 0 ||
            x >= (int16_t)EInkDisplay::DISPLAY_WIDTH ||
            y >= (int16_t)EInkDisplay::DISPLAY_HEIGHT) return;
        uint32_t idx  = (uint32_t)y * _wBytes + ((uint16_t)x >> 3);
        uint8_t  mask = 0x80u >> (x & 7);
        // GxEPD convention: 0x0000 = black, 0xFFFF = white
        if (color == 0x0000u) _fb[idx] &= ~mask;   // black → clear bit
        else                  _fb[idx] |=  mask;    // white → set bit
    }

    // Fill screen helper (faster than calling drawPixel for every pixel)
    void fillScreenWhite() {
        if (_fb) memset(_fb, 0xFF, (size_t)_wBytes * EInkDisplay::DISPLAY_HEIGHT);
    }
    void fillScreenBlack() {
        if (_fb) memset(_fb, 0x00, (size_t)_wBytes * EInkDisplay::DISPLAY_HEIGHT);
    }

private:
    uint8_t*  _fb;
    uint16_t  _wBytes;
};
