#pragma once

// ============================================================
// TextRenderer
//
// Lays out a plain-text string onto the 400×300 e-ink display
// with full word-wrap, page-break, and optional bionic reading
// markup.
//
// Usage:
//   RenderConfig cfg;
//   cfg.fontSize    = 16;
//   cfg.margin      = 10;
//   cfg.lineSpacing = 4;
//   cfg.bionicMode  = BionicMode::BIONIC_NORMAL;
//
//   TextRenderer tr;
//   tr.setConfig(cfg);
//
//   std::vector<Page> pages;
//   tr.paginate(chapterText, pages);
//
//   tr.renderPage(pages[0]);   // draws to EPDDisplay
// ============================================================

#include <Arduino.h>
#include <vector>
#include "../../include/config.h"

// ── RenderLine ────────────────────────────────────────────────────────────────
// One line of text on the display, split into up to two runs: bold prefix and
// normal suffix.  When bionicMode == BIONIC_OFF only suffix is used.
struct RenderLine {
    int16_t x;         ///< pixel x of the line start (left margin)
    int16_t y;         ///< pixel y baseline
    String  boldPart;  ///< bold prefix (empty when not in bionic mode)
    String  normalPart;///< normal-weight text (the full word when bionic=off)
};

// ── RenderConfig ──────────────────────────────────────────────────────────────
struct RenderConfig {
    uint8_t    fontSize    = 16;
    uint8_t    margin      = 10;   ///< left and right margin in pixels
    uint8_t    lineSpacing = 4;    ///< extra pixels between lines (leading)
    BionicMode bionicMode  = BionicMode::BIONIC_OFF;
};

// ── Page ─────────────────────────────────────────────────────────────────────
// A page holds all RenderLines that fit between the status bar and the bottom
// of the display.  charStart / charEnd are byte offsets into the original
// text string so that progress can be tracked and re-pagination avoided.
struct Page {
    std::vector<RenderLine> lines;
    int charStart = 0;   ///< byte index of first character on this page
    int charEnd   = 0;   ///< byte index one past the last character on this page
};

// ─────────────────────────────────────────────────────────────────────────────
// TextRenderer
// ─────────────────────────────────────────────────────────────────────────────
class TextRenderer {
public:
    TextRenderer() = default;

    // Apply a new rendering configuration.  Must call paginate() again after
    // changing config if you need fresh page breaks.
    void setConfig(const RenderConfig& cfg);

    const RenderConfig& getConfig() const { return _cfg; }

    // ── Pagination ────────────────────────────────────────────────────────

    /// Split text into pages according to current config.
    /// Returns the number of pages.
    /// pages is cleared and rebuilt from scratch.
    int paginate(const String& text, std::vector<Page>& pages);

    // ── Rendering ────────────────────────────────────────────────────────

    /// Draw a single page to EPDDisplay.  Caller is responsible for
    /// initiating and finalising the display update cycle.
    void renderPage(const Page& page);

    // ── Geometry helpers (public so callers can compute layouts) ─────────

    /// Return the maximum number of text characters per line given current
    /// config.  (Approximation: uses 'M' width × usable width / charWidth.)
    int charsPerLine() const;

    /// Pixel width of the usable content area (display width - 2×margin).
    int16_t contentWidth()  const;

    /// Pixel height of the usable content area (display height - status bar - 2×margin).
    int16_t contentHeight() const;

    /// Height of one line including leading.
    int16_t lineStride()    const;

private:
    // ── Word → RenderLine helpers ─────────────────────────────────────────

    /// Flush the current line-build buffer into the page.
    /// Returns the new baseline Y (advanced by lineStride()).
    int16_t _flushLine(Page& page,
                       std::vector<std::pair<String,String>>& words,
                       int16_t baselineY,
                       int     firstCharIdx);

    /// Measure the pixel width of a word rendered with current config.
    /// Returns combined width of bold prefix + normal suffix.
    int16_t _wordWidth(const String& boldPart, const String& normalPart) const;

    // ── State ─────────────────────────────────────────────────────────────
    RenderConfig _cfg;

    // Layout constants derived from config (cached on setConfig)
    int16_t _lineH   = 20;   ///< lineStride() = getLineHeight(fontSize) + lineSpacing
    int16_t _contentW = 380; ///< contentWidth()
    int16_t _contentH = 260; ///< contentHeight()

    // Y offset where content starts (below status bar + top margin)
    static constexpr int16_t STATUS_H  = 20;
    static constexpr int16_t TOP_MARGIN = 8;
};
