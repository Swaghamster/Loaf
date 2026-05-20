// ============================================================
// TextRenderer.cpp
// ============================================================

#include "TextRenderer.h"
#include "../display/EPDDisplay.h"
#include "../bionic/BionicReading.h"
#include "../../include/config.h"

#include <cmath>   // std::ceil

// ─────────────────────────────────────────────────────────────────────────────
// setConfig
// ─────────────────────────────────────────────────────────────────────────────
void TextRenderer::setConfig(const RenderConfig& cfg) {
    _cfg = cfg;

    EPDDisplay& epd = EPDDisplay::instance();
    int16_t fontH   = epd.getLineHeight(cfg.fontSize);

    _lineH    = fontH + cfg.lineSpacing;
    _contentW = EPD_WIDTH  - 2 * (int16_t)cfg.margin;
    _contentH = EPD_HEIGHT - STATUS_H - TOP_MARGIN - (int16_t)cfg.margin;
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry helpers
// ─────────────────────────────────────────────────────────────────────────────
int16_t TextRenderer::contentWidth()  const { return _contentW; }
int16_t TextRenderer::contentHeight() const { return _contentH; }
int16_t TextRenderer::lineStride()    const { return _lineH; }

int TextRenderer::charsPerLine() const {
    // Measure the width of a representative character ('n') and estimate
    // how many fit in the content width.
    EPDDisplay& epd = EPDDisplay::instance();
    int16_t charW = epd.getTextWidth("n", _cfg.fontSize, false);
    if (charW <= 0) charW = 8;
    return (int)(_contentW / charW);
}

// ─────────────────────────────────────────────────────────────────────────────
// _wordWidth
//
// Returns the total pixel width of one word, which may consist of a bold
// prefix and a normal-weight suffix (bionic mode) or just a normal suffix.
// A space is NOT included — the caller handles inter-word gaps.
// ─────────────────────────────────────────────────────────────────────────────
int16_t TextRenderer::_wordWidth(const String& boldPart,
                                 const String& normalPart) const {
    EPDDisplay& epd = EPDDisplay::instance();
    int16_t w = 0;
    if (boldPart.length() > 0) {
        w += epd.getTextWidth(boldPart.c_str(), _cfg.fontSize, true);
    }
    if (normalPart.length() > 0) {
        w += epd.getTextWidth(normalPart.c_str(), _cfg.fontSize, false);
    }
    return w;
}

// ─────────────────────────────────────────────────────────────────────────────
// _flushLine
//
// Commit the word list accumulated so far into a single RenderLine in the page.
// Words in the line are joined left-to-right with one space between them.
// Each word's bold prefix and normal suffix are concatenated to form
// the two parts of the RenderLine.
//
// Returns the Y baseline for the NEXT line.
// ─────────────────────────────────────────────────────────────────────────────
int16_t TextRenderer::_flushLine(
        Page& page,
        std::vector<std::pair<String, String>>& words,
        int16_t baselineY,
        int     firstCharIdx) {

    if (words.empty()) {
        return baselineY + _lineH;
    }

    // We store each word as its own RenderLine so that the renderer can
    // lay them out side-by-side at precise x positions.
    EPDDisplay& epd = EPDDisplay::instance();
    int16_t     spaceW = epd.getTextWidth(" ", _cfg.fontSize, false);
    int16_t     cx     = (int16_t)_cfg.margin;

    for (size_t wi = 0; wi < words.size(); ++wi) {
        RenderLine rl;
        rl.x          = cx;
        rl.y          = baselineY;
        rl.boldPart   = words[wi].first;
        rl.normalPart = words[wi].second;
        page.lines.push_back(rl);

        // Advance cursor by the measured width of this word + space
        cx += _wordWidth(rl.boldPart, rl.normalPart);
        if (wi + 1 < words.size()) {
            cx += spaceW;
        }
    }

    words.clear();
    return baselineY + _lineH;
}

// ─────────────────────────────────────────────────────────────────────────────
// paginate
//
// Algorithm:
//   1. Tokenise the text on whitespace boundaries.
//   2. For each token (word or newline):
//      a. Apply bionic splitting if enabled.
//      b. Measure pixel width.
//      c. If the word fits on the current line, accumulate it.
//         Otherwise flush the line and start a new one.
//      d. Newline characters force a line break.
//      e. When the page is full (no more vertical space), close the current
//         page and open a new one.
//   3. charStart / charEnd on each page track byte offsets into the input.
// ─────────────────────────────────────────────────────────────────────────────
int TextRenderer::paginate(const String& text, std::vector<Page>& pages) {
    pages.clear();

    if (text.length() == 0) return 0;

    EPDDisplay& epd   = EPDDisplay::instance();
    const int16_t spaceW = epd.getTextWidth(" ", _cfg.fontSize, false);

    // Maximum lines per page
    const int maxLines = (int)(_contentH / _lineH);
    if (maxLines <= 0) return 0;

    // Current page being built
    Page currentPage;
    currentPage.charStart = 0;

    // Current line accumulator: list of (boldPart, normalPart)
    std::vector<std::pair<String, String>> lineWords;
    int16_t lineWidth = 0;          // current accumulated pixel width (words + spaces)
    int     lineCount = 0;          // lines used on the current page

    // Baseline Y for the first line of a page
    auto firstBaseline = [&]() -> int16_t {
        return STATUS_H + TOP_MARGIN + epd.getLineHeight(_cfg.fontSize);
    };
    int16_t baselineY = firstBaseline();

    // Lambda: flush lineWords into currentPage and advance Y
    auto flushCurrentLine = [&](int firstIdx) {
        if (!lineWords.empty()) {
            baselineY = _flushLine(currentPage, lineWords, baselineY, firstIdx);
            lineCount++;
            lineWidth = 0;
        }
    };

    // Lambda: close the current page and open a fresh one
    auto closePage = [&](int nextCharStart) {
        currentPage.charEnd = nextCharStart;
        pages.push_back(currentPage);
        currentPage = Page();
        currentPage.charStart = nextCharStart;
        lineCount  = 0;
        baselineY  = firstBaseline();
        lineWords.clear();
        lineWidth  = 0;
    };

    // Tokenise
    int  len   = (int)text.length();
    int  i     = 0;

    while (i < len) {
        char c = text[i];

        // Explicit newline → force line break
        if (c == '\n') {
            flushCurrentLine(i);
            if (lineCount >= maxLines) {
                closePage(i + 1);
            }
            ++i;
            continue;
        }

        // Skip spaces
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }

        // Collect a word token
        int wordStart = i;
        while (i < len && text[i] != ' ' && text[i] != '\t' &&
               text[i] != '\r' && text[i] != '\n') {
            ++i;
        }
        String token = text.substring(wordStart, i);

        // Apply bionic splitting
        String boldPart, normalPart;
        if (_cfg.bionicMode != BionicMode::BIONIC_OFF) {
            BionicWord bw = BionicReading::splitWord(token, _cfg.bionicMode);
            boldPart   = bw.prefix;
            normalPart = bw.suffix;
        } else {
            boldPart   = String();
            normalPart = token;
        }

        // Measure the word
        int16_t ww = _wordWidth(boldPart, normalPart);

        // Does the word fit on the current line?
        int16_t neededWidth = lineWidth;
        if (!lineWords.empty()) {
            neededWidth += spaceW;  // space before new word
        }
        neededWidth += ww;

        if (!lineWords.empty() && neededWidth > _contentW) {
            // Word doesn't fit → flush current line
            flushCurrentLine(wordStart);
            lineWidth = 0;

            // Check if page is now full
            if (lineCount >= maxLines) {
                closePage(wordStart);
            }
        }

        // Now add the word to the current line
        lineWords.push_back({ boldPart, normalPart });
        if (lineWords.size() == 1) {
            lineWidth = ww;
        } else {
            lineWidth += spaceW + ww;
        }

        // If the word alone is wider than the content area, force a flush
        if (lineWords.size() == 1 && ww > _contentW) {
            flushCurrentLine(i);
            if (lineCount >= maxLines) {
                closePage(i);
            }
        }
    }

    // Flush any remaining words
    if (!lineWords.empty()) {
        flushCurrentLine(len);
    }

    // Close the last page if it has content
    if (!currentPage.lines.empty()) {
        currentPage.charEnd = len;
        pages.push_back(currentPage);
    }

    return (int)pages.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// renderPage
//
// Clears the framebuffer to white, draws all RenderLines in the page, then
// flushes to the physical display.
//
// For each RenderLine:
//   - If boldPart is non-empty:
//       draw boldPart at (rl.x, rl.y) in bold
//       draw normalPart immediately to the right
//   - Otherwise:
//       draw normalPart at (rl.x, rl.y)
// ─────────────────────────────────────────────────────────────────────────────
void TextRenderer::renderPage(const Page& page) {
    EPDDisplay& epd = EPDDisplay::instance();

    for (const RenderLine& rl : page.lines) {
        if (rl.boldPart.length() > 0) {
            // Draw bold prefix
            epd.drawText(rl.x, rl.y, rl.boldPart.c_str(),
                         _cfg.fontSize, /*bold=*/true, 0x0000);

            // Measure bold prefix width to position suffix correctly
            int16_t bw = epd.getTextWidth(rl.boldPart.c_str(),
                                          _cfg.fontSize, /*bold=*/true);

            // Draw normal suffix immediately to the right
            if (rl.normalPart.length() > 0) {
                epd.drawText(rl.x + bw, rl.y, rl.normalPart.c_str(),
                             _cfg.fontSize, /*bold=*/false, 0x0000);
            }
        } else {
            // No bionic splitting — just draw the whole word normally
            if (rl.normalPart.length() > 0) {
                epd.drawText(rl.x, rl.y, rl.normalPart.c_str(),
                             _cfg.fontSize, /*bold=*/false, 0x0000);
            }
        }
    }
}
