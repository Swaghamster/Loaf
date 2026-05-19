#pragma once
// ============================================================
// CoverLoader.h
// Loaf Firmware — 1-bit BMP cover image loader
//
// Reads a pre-converted 1-bit Windows BMP from SD and renders
// it into a rectangular region on the EPD canvas.
//
// Cover convention:
//   /books/<bookdir>/cover.bmp
//
// The BMP must be 1-bit (monochrome). Any size is accepted;
// the loader crops or letterboxes to fit the target rect.
//
// Generating covers (on desktop):
//   convert cover.jpg -resize 150x190 -dither FloydSteinberg \
//           -monochrome BMP3:cover.bmp
// ============================================================

#include <Arduino.h>

class CoverLoader {
public:
    // Draw the cover for bookDir (bare directory name under /books/)
    // into the rectangle (x, y, w, h) on the EPD canvas.
    // Returns true if a cover was found and drawn; false if not found.
    static bool draw(const char* bookDir,
                     int16_t x, int16_t y,
                     int16_t w, int16_t h);

private:
    // Parse BMP header and return pixel data offset, image width/height, and
    // whether pixel rows are stored bottom-up (standard BMP) or top-down.
    struct BMPInfo {
        uint32_t dataOffset;
        int32_t  imgW;
        int32_t  imgH;
        bool     bottomUp;   // true for standard BMP (positive height)
        bool     valid;
    };
    static BMPInfo _parseBMPHeader(const uint8_t* hdr, size_t hdrLen);
};
