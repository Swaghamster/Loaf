#include "CoverLoader.h"
#include "../storage/FileManager.h"
#include "../display/EPDDisplay.h"
#include "../../include/config.h"

#include <SDCardManager.h>

#define SdMan SDCardManager::getInstance()

// ── BMP file/DIB header sizes ──────────────────────────────────────────────
static constexpr size_t BMP_FILE_HDR  = 14;
static constexpr size_t BMP_DIB_HDR   = 40;   // BITMAPINFOHEADER
static constexpr size_t BMP_HDR_TOTAL = BMP_FILE_HDR + BMP_DIB_HDR;

// ── Helpers ────────────────────────────────────────────────────────────────

static inline uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int32_t les32(const uint8_t* p) {
    return (int32_t)le32(p);
}

// ── _parseBMPHeader ────────────────────────────────────────────────────────

CoverLoader::BMPInfo CoverLoader::_parseBMPHeader(const uint8_t* h, size_t len) {
    BMPInfo info{};
    info.valid = false;

    if (len < BMP_HDR_TOTAL) return info;
    if (h[0] != 'B' || h[1] != 'M') return info;

    uint16_t bpp = le16(h + BMP_FILE_HDR + 14);
    if (bpp != 1) {
        Serial.printf("[CoverLoader] only 1-bit BMP supported (got %u bpp)\n", bpp);
        return info;
    }
    uint32_t compression = le32(h + BMP_FILE_HDR + 16);
    if (compression != 0) {
        Serial.println("[CoverLoader] only uncompressed BMP supported");
        return info;
    }

    info.dataOffset = le32(h + 10);
    info.imgW       = les32(h + BMP_FILE_HDR + 4);
    int32_t rawH    = les32(h + BMP_FILE_HDR + 8);
    info.bottomUp   = (rawH > 0);
    info.imgH       = info.bottomUp ? rawH : -rawH;
    info.valid      = (info.imgW > 0 && info.imgH > 0);
    return info;
}

// ── draw ───────────────────────────────────────────────────────────────────

bool CoverLoader::draw(const char* bookDir,
                       int16_t x, int16_t y,
                       int16_t w, int16_t h) {
    // Build path: /books/<bookdir>/cover.bmp
    char path[128];
    snprintf(path, sizeof(path), "%s/%s/cover.bmp", SD_BOOKS_DIR, bookDir);

    if (!FileManager::instance().exists(path)) return false;

    FsFile f = SdMan.open(path, O_RDONLY);
    if (!f) return false;

    // Read the header
    uint8_t hdr[BMP_HDR_TOTAL + 8];   // +8 for 2-entry colour table
    size_t  read = f.read(hdr, sizeof(hdr));
    if (read < BMP_HDR_TOTAL) { f.close(); return false; }

    BMPInfo info = _parseBMPHeader(hdr, read);
    if (!info.valid) { f.close(); return false; }

    // Row size in BMP is padded to 4-byte boundary.
    // For 1-bit images: ceil(imgW / 8) padded to multiple of 4.
    int32_t rowBytes = ((info.imgW + 31) / 32) * 4;

    // Centre-crop: find source rect that fills dest (w × h) without
    // distortion.  Scale = min(imgW/w, imgH/h) means we show as much
    // of the cover as possible at the target resolution.
    //
    // For e-ink at these small card sizes we do nearest-neighbour sampling:
    //   dst pixel (dx, dy) ← src pixel (srcX, srcY)
    //     srcX = cropX + dx * sampleX
    //     srcY = cropY + dy * sampleY
    //   where sampleX = imgW / w,  sampleY = imgH / h  (fixed-point ×256)

    int32_t sampleX  = (info.imgW  * 256) / w;   // fixed-point 8.8
    int32_t sampleY  = (info.imgH  * 256) / h;

    EPDDisplay& epd = EPDDisplay::instance();

    // We render row by row to keep RAM usage to one BMP row at a time.
    // Allocate for the widest possible row (800px / 8 = 100 bytes, pad to 4).
    const size_t rowBufSz = (size_t)rowBytes;
    uint8_t* rowBuf = (uint8_t*)malloc(rowBufSz);
    if (!rowBuf) { f.close(); return false; }

    for (int16_t dy = 0; dy < h; ++dy) {
        // Which source row do we need?
        int32_t srcY = (int32_t)dy * sampleY / 256;
        if (srcY >= info.imgH) srcY = info.imgH - 1;

        // BMP stores rows bottom-up by default.
        int32_t bmpRow = info.bottomUp ? (info.imgH - 1 - srcY) : srcY;

        uint32_t seekPos = info.dataOffset + (uint32_t)bmpRow * (uint32_t)rowBytes;
        if (!f.seekSet(seekPos)) break;
        if ((size_t)f.read(rowBuf, rowBufSz) < rowBufSz) break;

        for (int16_t dx = 0; dx < w; ++dx) {
            int32_t srcX = (int32_t)dx * sampleX / 256;
            if (srcX >= info.imgW) srcX = info.imgW - 1;

            // Extract the bit: BMP bit 0 is the leftmost pixel in the byte.
            uint8_t  byteVal = rowBuf[srcX / 8];
            uint8_t  bit     = (byteVal >> (7 - (srcX % 8))) & 1;

            // BMP 1-bit: colour table index 0=first colour, 1=second.
            // Standard monochrome BMP: 0=black, 1=white.
            // EPD convention: 0x0000=black, 0xFFFF=white.
            uint16_t color = bit ? 0xFFFF : 0x0000;

            // Bounds-check then write via canvas
            int16_t px = x + dx;
            int16_t py = y + dy;
            if (px >= 0 && px < EPD_WIDTH && py >= 0 && py < EPD_HEIGHT) {
                epd.canvas().drawPixel(px, py, color);
            }
        }
    }

    free(rowBuf);
    f.close();

    // Draw a 1px border around the card so it has a clean edge
    epd.drawRect(x, y, w, h, 0x0000);
    return true;
}
