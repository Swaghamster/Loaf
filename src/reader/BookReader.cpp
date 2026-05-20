// ============================================================
// BookReader.cpp
// ============================================================

#include "BookReader.h"
#include "../display/EPDDisplay.h"
#include "../storage/FileManager.h"
#include "../../include/config.h"

// Button indices — match InputManager / UIManager constants.
// Defined locally to avoid pulling in UIManager.h.
static constexpr uint8_t kBTN_LEFT  = 2;
static constexpr uint8_t kBTN_RIGHT = 3;
static constexpr uint8_t kBTN_UP    = 4;
static constexpr uint8_t kBTN_DOWN  = 5;

// Y coordinate where the status bar ends (must match UIManager::STATUS_H)
static constexpr int16_t kStatusH    = 20;
static constexpr int16_t kContentY   = kStatusH + 2;

// ─────────────────────────────────────────────────────────────────────────────
// instance
// ─────────────────────────────────────────────────────────────────────────────
BookReader& BookReader::instance() {
    static BookReader inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// init — configure default render settings
// ─────────────────────────────────────────────────────────────────────────────
void BookReader::init() {
    RenderConfig cfg;
    cfg.fontSize    = FONT_NORMAL;
    cfg.margin      = MARGIN_X;
    cfg.lineSpacing = LINE_SPACING;
    cfg.bionicMode  = BionicMode::BIONIC_OFF;
    _renderer.setConfig(cfg);
}

// ─────────────────────────────────────────────────────────────────────────────
// openBook
// ─────────────────────────────────────────────────────────────────────────────
bool BookReader::openBook(const char* bookDir) {
    close();
    if (!bookDir || bookDir[0] == '\0') return false;

    _bookDir = String(bookDir);

    if (!_parser.open(bookDir)) {
        Serial.printf("[BookReader] Cannot open: %s\n", bookDir);
        _bookDir = String();
        return false;
    }
    _meta = _parser.getMeta();

    // Restore saved position
    int savedChapter = 0, savedPage = 0;
    _loadProgress(savedChapter, savedPage);
    if (savedChapter >= _parser.chapterCount()) savedChapter = 0;

    _open = true;

    if (!_loadChapter(savedChapter)) {
        if (savedChapter != 0) {
            savedChapter = 0;
            savedPage    = 0;
            if (!_loadChapter(0)) { _open = false; return false; }
        } else {
            _open = false;
            return false;
        }
    }

    _page = (savedPage < (int)_pages.size()) ? savedPage : 0;

    Serial.printf("[BookReader] '%s': %d chs, starting at ch%d pg%d\n",
                  _meta.title.c_str(), _parser.chapterCount(), _chapter, _page);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// close
// ─────────────────────────────────────────────────────────────────────────────
void BookReader::close() {
    if (_open) saveProgress();
    _open       = false;
    _chapter    = 0;
    _page       = 0;
    _bookDir    = String();
    _meta       = BookMeta();
    _curChapter = Chapter();
    _pages.clear();
    _parser.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// saveProgress
// ─────────────────────────────────────────────────────────────────────────────
void BookReader::saveProgress() const {
    if (!_open || !FileManager::instance().isReady()) return;
    FileManager::instance().makeDir(SD_PROGRESS_DIR);
    String content = String("chapter=") + _chapter + "\npage=" + _page + "\n";
    FileManager::instance().writeFile(_progressPath().c_str(), content);
    Serial.printf("[BookReader] Progress saved: ch%d pg%d\n", _chapter, _page);
}

// ─────────────────────────────────────────────────────────────────────────────
// _loadProgress — read "chapter=N\npage=M\n" from SD
// ─────────────────────────────────────────────────────────────────────────────
void BookReader::_loadProgress(int& outChapter, int& outPage) const {
    outChapter = 0;
    outPage    = 0;
    if (!FileManager::instance().isReady()) return;

    String data;
    if (!FileManager::instance().readFile(_progressPath().c_str(), data)) return;

    int i = 0;
    while (i < (int)data.length()) {
        int nl = data.indexOf('\n', i);
        if (nl < 0) nl = (int)data.length();
        String line = data.substring(i, nl);
        line.trim();
        int eq = line.indexOf('=');
        if (eq > 0) {
            String key = line.substring(0, eq);
            String val = line.substring(eq + 1);
            if      (key == "chapter") outChapter = val.toInt();
            else if (key == "page")    outPage    = val.toInt();
        }
        i = nl + 1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// _progressPath — /loaf/progress/<bookname>.txt
// ─────────────────────────────────────────────────────────────────────────────
String BookReader::_progressPath() const {
    String name = bareDir();
    String safe;
    for (int i = 0; i < (int)name.length(); ++i) {
        char c = name[i];
        safe += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    }
    return String(SD_PROGRESS_DIR) + "/" + safe + ".txt";
}

// ─────────────────────────────────────────────────────────────────────────────
// bareDir — last path component of _bookDir
// ─────────────────────────────────────────────────────────────────────────────
String BookReader::bareDir() const {
    int slash = _bookDir.lastIndexOf('/');
    return (slash >= 0) ? _bookDir.substring(slash + 1) : _bookDir;
}

// ─────────────────────────────────────────────────────────────────────────────
// _loadChapter — parse + paginate one chapter
// ─────────────────────────────────────────────────────────────────────────────
bool BookReader::_loadChapter(int idx) {
    if (idx < 0 || idx >= _parser.chapterCount()) return false;

    Chapter ch;
    if (!_parser.loadChapter(idx, ch)) {
        Serial.printf("[BookReader] Failed to load ch%d\n", idx);
        return false;
    }
    _curChapter = ch;

    _pages.clear();
    int n = _renderer.paginate(_curChapter.content, _pages);
    if (n == 0) _pages.push_back(Page());   // placeholder for empty chapter

    _chapter = idx;
    Serial.printf("[BookReader] Ch%d '%s': %d pages\n",
                  idx, ch.title.c_str(), (int)_pages.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// render — draw current page into the framebuffer (no clear/update)
// ─────────────────────────────────────────────────────────────────────────────
void BookReader::render() {
    EPDDisplay& epd = EPDDisplay::instance();

    if (!_open || _pages.empty()) {
        epd.drawText(MARGIN_X, kContentY + 40,
                     "No book open. Go to Library.", FONT_NORMAL, false, 0x0000);
        epd.drawText(MARGIN_X, EPD_HEIGHT - 5,
                     "BACK to return", FONT_SMALL, false, 0x0000);
        return;
    }

    // Draw text content (TextRenderer writes into the framebuffer only)
    if (_page < (int)_pages.size()) {
        _renderer.renderPage(_pages[_page]);
    }

    // Progress footer — centred at the very bottom of the screen
    char buf[72];
    snprintf(buf, sizeof(buf), "< Ch %d/%d  |  Pg %d/%d >",
             _chapter + 1, _parser.chapterCount(),
             _page + 1, (int)_pages.size());
    int16_t bw = epd.getTextWidth(buf, FONT_SMALL, false);
    epd.drawText((EPD_WIDTH - bw) / 2, EPD_HEIGHT - 5,
                 buf, FONT_SMALL, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleButton
//
// Button mapping:
//   RIGHT (short) / DOWN (short) → next page; auto-advances chapter at end
//   LEFT  (short) / UP   (short) → prev page; auto-retreats chapter at start
//   RIGHT (long)  / DOWN (long)  → next chapter
//   LEFT  (long)  / UP   (long)  → prev chapter
//
// Returns true when the displayed page changed.
// ─────────────────────────────────────────────────────────────────────────────
bool BookReader::handleButton(uint8_t btn, bool longPress) {
    if (!_open) return false;

    const bool isRight = (btn == kBTN_RIGHT);
    const bool isLeft  = (btn == kBTN_LEFT);
    const bool isDown  = (btn == kBTN_DOWN);
    const bool isUp    = (btn == kBTN_UP);

    const bool nextPage = (isRight || isDown) && !longPress;
    const bool prevPage = (isLeft  || isUp)   && !longPress;
    const bool nextChap = (isRight || isDown) &&  longPress;
    const bool prevChap = (isLeft  || isUp)   &&  longPress;

    if (nextPage) {
        if (_page + 1 < (int)_pages.size()) {
            ++_page;
            return true;
        }
        if (_chapter + 1 < _parser.chapterCount()) {
            _loadChapter(_chapter + 1);
            _page = 0;
            return true;
        }
        return false;   // already at last page of last chapter
    }

    if (prevPage) {
        if (_page > 0) {
            --_page;
            return true;
        }
        if (_chapter > 0) {
            _loadChapter(_chapter - 1);
            _page = max(0, (int)_pages.size() - 1);
            return true;
        }
        return false;   // already at first page of first chapter
    }

    if (nextChap) {
        if (_chapter + 1 < _parser.chapterCount()) {
            _loadChapter(_chapter + 1);
            _page = 0;
            return true;
        }
        return false;
    }

    if (prevChap) {
        if (_chapter > 0) {
            _loadChapter(_chapter - 1);
            _page = 0;
            return true;
        }
        return false;
    }

    return false;
}
