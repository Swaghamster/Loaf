#include "UIManager.h"
#include "../display/EPDDisplay.h"
#include "../storage/FileManager.h"
#include "../../include/config.h"

// ─────────────────────────────────────────────────────────────────────────────
// Home menu — iPod-style horizontal carousel
//
// The selected item sits large at the centre; two flanking items appear at
// reduced size on either side, and partial "ghost" items peek in from the
// edges to indicate there is more to scroll through.
//
// Navigation: LEFT / RIGHT (or UP / DOWN) spin the carousel.
//
//   ghost  |  prev  |  [SELECTED]  |  next  |  ghost
//    50px     130px      200px        130px     50px
//
// ─────────────────────────────────────────────────────────────────────────────

struct MenuItem {
    const char* label;
    Screen      screen;
};

static const MenuItem kMenuItems[UIManager::MENU_ITEM_COUNT] = {
    { "Library",  Screen::SCREEN_LIBRARY  },
    { "Notes",    Screen::SCREEN_NOTES    },
    { "Stats",    Screen::SCREEN_STATS    },
    { "Dict",     Screen::SCREEN_DICT     },
    { "Settings", Screen::SCREEN_SETTINGS },
};

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
UIManager& UIManager::instance() {
    static UIManager inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::init() {
    _current   = Screen::SCREEN_HOME;
    _menuIndex = 0;
    _dirty     = true;
    _history.clear();
    _history.reserve(8);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleButton
//
// Button index  InputManager index  Role
// ──────────────────────────────────────────────────────
//  BTN_BACK   0  (ADC1, BACK)       Back / cancel
//  BTN_SELECT 1  (ADC1, CONFIRM)    Confirm / enter
//  BTN_LEFT   2  (ADC1, LEFT)       Left (not used in all screens)
//  BTN_RIGHT  3  (ADC1, RIGHT)      Right (not used in all screens)
//  BTN_UP     4  (ADC2, UP)         Previous item / page up
//  BTN_DOWN   5  (ADC2, DOWN)       Next item / page down
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::handleButton(uint8_t btn, bool longPress) {
    switch (_current) {

        // ── Home screen — two zones ──────────────────────────────────────────
        // Zone 0: book carousel (LEFT/RIGHT navigate books, SELECT opens book)
        // Zone 1: app strip    (LEFT/RIGHT navigate apps,  SELECT enters app)
        // UP/DOWN switches between zones.
        case Screen::SCREEN_HOME:
            if (btn == BTN_UP || btn == BTN_DOWN) {
                _homeZone = (_homeZone == 0) ? 1 : 0;
                _dirty = true;
            } else if (btn == BTN_LEFT) {
                if (_homeZone == 0 && _recentCount > 0) {
                    _bookIndex = (_bookIndex - 1 + _recentCount) % _recentCount;
                } else {
                    _menuIndex = (_menuIndex - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
                }
                _dirty = true;
            } else if (btn == BTN_RIGHT) {
                if (_homeZone == 0 && _recentCount > 0) {
                    _bookIndex = (_bookIndex + 1) % _recentCount;
                } else {
                    _menuIndex = (_menuIndex + 1) % MENU_ITEM_COUNT;
                }
                _dirty = true;
            } else if (btn == BTN_SELECT) {
                if (_homeZone == 1) {
                    navigateTo(kMenuItems[_menuIndex].screen);
                }
                // Zone 0 (book) open: handled by reader subsystem in future
            }
            break;

        // ── Library ──────────────────────────────────────────────────────────
        case Screen::SCREEN_LIBRARY:
            if (btn == BTN_BACK || (btn == BTN_SELECT && longPress)) {
                back();
            } else if (btn == BTN_UP || btn == BTN_DOWN) {
                // Future: scroll book list; mark dirty so render can respond.
                _dirty = true;
            }
            break;

        // ── Reader ───────────────────────────────────────────────────────────
        case Screen::SCREEN_READER:
            // Navigation is handled directly by the reader subsystem;
            // UIManager just needs to route "back" gracefully.
            if (btn == BTN_BACK && !longPress) {
                back();
            } else if (btn == BTN_SELECT && longPress) {
                back();
            }
            break;

        // ── Stats / Settings / Notes / Dict ──────────────────────────────────
        case Screen::SCREEN_STATS:
        case Screen::SCREEN_SETTINGS:
        case Screen::SCREEN_NOTES:
        case Screen::SCREEN_DICT:
            if (btn == BTN_BACK) {
                back();
            } else if (btn == BTN_UP || btn == BTN_DOWN) {
                _dirty = true;
            }
            break;

        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// navigateTo
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::navigateTo(Screen s) {
    if (_history.size() < 16) {      // guard against stack overflow
        _history.push_back(_current);
    }
    _current = s;
    _dirty   = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// back
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::back() {
    if (_history.empty()) {
        _current = Screen::SCREEN_HOME;
    } else {
        _current = _history.back();
        _history.pop_back();
    }
    _dirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// render
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::render() {
    if (!_dirty) return;
    _dirty = false;

    EPDDisplay& epd = EPDDisplay::instance();

    // Clear the framebuffer to white, then draw the current screen.
    epd.clear();

    // Every screen gets the status bar at the top.
    drawStatusBar();

    switch (_current) {
        case Screen::SCREEN_HOME:     renderHome();     break;
        case Screen::SCREEN_LIBRARY:  renderLibrary();  break;
        case Screen::SCREEN_READER:   renderReader();   break;
        case Screen::SCREEN_STATS:    renderStats();    break;
        case Screen::SCREEN_SETTINGS: renderSettings(); break;
        case Screen::SCREEN_NOTES:    renderNotes();    break;
        case Screen::SCREEN_DICT:     renderDict();     break;
        default: break;
    }

    epd.update();
}

// ─────────────────────────────────────────────────────────────────────────────
// drawStatusBar
//
// Layout (left → right, y=0 .. STATUS_H):
//   [Battery placeholder]  [Note count]  [Elapsed time]
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::drawStatusBar() {
    EPDDisplay& epd = EPDDisplay::instance();

    // Background stripe — draw a thin rule along the bottom edge of the bar
    epd.drawLine(0, STATUS_H - 1, EPD_WIDTH - 1, STATUS_H - 1, 0x0000);

    // Battery icon placeholder: small rectangle with a nub
    const int16_t bx = 4, by = 4;
    const int16_t bw = 18, bh = STATUS_H - 8;
    epd.drawRect(bx, by, bw, bh, 0x0000);
    // Nub on right
    epd.fillRect(bx + bw, by + (bh / 2) - 2, 3, 4, 0x0000);
    // Simple fill to indicate ~75% charge (placeholder)
    epd.fillRect(bx + 2, by + 2, (int16_t)((bw - 4) * 3 / 4), bh - 4, 0x0000);

    // Note count
    char noteBuf[24];
    int  noteCount = 0;
    {
        // Count notes if filesystem is ready; otherwise show 0.
        std::vector<String> noteFiles;
        if (FileManager::instance().isReady()) {
            FileManager::instance().listDir(SD_NOTES_DIR, noteFiles, ".txt");
            noteCount = (int)noteFiles.size();
        }
    }
    snprintf(noteBuf, sizeof(noteBuf), "%d notes", noteCount);
    epd.drawText(34, STATUS_H - 5, noteBuf, 12, false, 0x0000);

    // Elapsed time from millis() (HH:MM:SS format, 0-based from boot)
    uint32_t totalSec = millis() / 1000UL;
    uint32_t hh = totalSec / 3600UL;
    uint32_t mm = (totalSec % 3600UL) / 60UL;
    uint32_t ss = totalSec % 60UL;
    char timeBuf[16];
    if (hh > 0) {
        snprintf(timeBuf, sizeof(timeBuf), "%u:%02u:%02u", (unsigned)hh, (unsigned)mm, (unsigned)ss);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "%u:%02u", (unsigned)mm, (unsigned)ss);
    }
    // Right-align time — measure width first
    int16_t timeW = epd.getTextWidth(timeBuf, 12, false);
    epd.drawText(EPD_WIDTH - timeW - 4, STATUS_H - 5, timeBuf, 12, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// recordRecentBook — prepend a book to the recent list (newest-first)
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::recordRecentBook(const String& title, const String& filename,
                                 int currentPage, int totalPages) {
    // Remove existing entry for same file if present
    int dest = 0;
    for (int i = 0; i < _recentCount; ++i) {
        if (_recentBooks[i].filename != filename) {
            _recentBooks[dest++] = _recentBooks[i];
        }
    }
    _recentCount = dest;

    // Shift everything down one slot to make room at index 0
    int newCount = min(_recentCount + 1, RECENT_BOOKS_MAX);
    for (int i = newCount - 1; i > 0; --i) {
        _recentBooks[i] = _recentBooks[i - 1];
    }
    _recentBooks[0].title       = title;
    _recentBooks[0].filename    = filename;
    _recentBooks[0].currentPage = currentPage;
    _recentBooks[0].totalPages  = totalPages;
    _recentCount = newCount;

    // Keep carousel index in range
    if (_bookIndex >= _recentCount) _bookIndex = 0;
    _dirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// _drawBookCard  — portrait-shaped book card centred at (cx, cy)
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::_drawBookCard(int16_t cx, int16_t cy,
                              int16_t w, int16_t h,
                              const char* title,
                              int page, int total,
                              bool selected) {
    EPDDisplay& epd = EPDDisplay::instance();
    int16_t x = cx - w / 2;
    int16_t y = cy - h / 2;

    if (selected) {
        epd.fillRect(x, y, w, h, 0x0000);
        // Horizontal "page lines" in white
        for (int r = 0; r < 4; ++r) {
            int16_t ly = y + h / 5 + r * (h / 6);
            int16_t lw = (int16_t)(w * 7 / 10);
            epd.drawLine(x + w / 8, ly, x + w / 8 + lw, ly, 0xFFFF);
        }
        if (title && *title) {
            uint8_t fsz = (w >= BOOK_SEL_W) ? 12 : 9;
            // Clip title to fit
            String t(title);
            while (t.length() > 1 &&
                   epd.getTextWidth(t.c_str(), fsz, true) > w - 8) {
                t = t.substring(0, t.length() - 1);
            }
            int16_t tw = epd.getTextWidth(t.c_str(), fsz, true);
            epd.drawText(cx - tw / 2, y + h + fsz + 4,
                         t.c_str(), fsz, true, 0x0000);
        }
        // Progress bar below title
        if (total > 0 && w >= BOOK_SEL_W) {
            int16_t barY = y + h + 22;
            int16_t barW = w;
            epd.drawRect(cx - barW / 2, barY, barW, 5, 0x0000);
            int16_t fill = (int16_t)((int32_t)barW * page / total);
            if (fill > 0) epd.fillRect(cx - barW / 2, barY, fill, 5, 0x0000);
        }
    } else {
        epd.fillRect(x, y, w, h, 0xFFFF);
        epd.drawRect(x, y, w, h, 0x0000);
        if (w >= BOOK_ADJ_W) {
            epd.drawRect(x + 2, y + 2, w - 4, h - 4, 0x0000);
            // Faint lines
            for (int r = 0; r < 3; ++r) {
                int16_t ly = y + h / 5 + r * (h / 5);
                epd.drawLine(x + 6, ly, x + w - 6, ly, 0x0000);
            }
            if (title && *title) {
                String t(title);
                while (t.length() > 1 &&
                       epd.getTextWidth(t.c_str(), 9, false) > w - 4) {
                    t = t.substring(0, t.length() - 1);
                }
                int16_t tw = epd.getTextWidth(t.c_str(), 9, false);
                epd.drawText(cx - tw / 2, y + h + 12,
                             t.c_str(), 9, false, 0x0000);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// _drawAppIcon — small square icon for the app strip
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::_drawAppIcon(int16_t cx, int16_t cy, int idx, bool selected) {
    EPDDisplay& epd = EPDDisplay::instance();
    int16_t half = APP_ICON_SZ / 2;
    int16_t x = cx - half, y = cy - half;
    int16_t s = APP_ICON_SZ;

    if (selected) {
        epd.fillRect(x, y, s, s, 0x0000);
    } else {
        epd.fillRect(x, y, s, s, 0xFFFF);
        epd.drawRect(x, y, s, s, 0x0000);
        epd.drawRect(x+2, y+2, s-4, s-4, 0x0000);
    }
    uint16_t fg = selected ? 0xFFFF : 0x0000;
    int16_t p = s / 6, in = s - p * 2, ix = x + p, iy = y + p;

    switch (idx) {
        case 0: // Library: 3 lines
            for (int r=0;r<3;++r) epd.drawLine(ix, iy+in/4+r*in/4, ix+in, iy+in/4+r*in/4, fg);
            break;
        case 1: // Notes: lined page
            epd.drawRect(ix, iy, in, in, fg);
            for (int r=0;r<3;++r) epd.drawLine(ix+3, iy+in/5+r*in/4, ix+in-3, iy+in/5+r*in/4, fg);
            break;
        case 2: // Stats: 3 bars
            epd.fillRect(cx-in/3-2, iy+in/2, in/4, in/2, fg);
            epd.fillRect(cx-in/8,   iy+in/3, in/4, in*2/3, fg);
            epd.fillRect(cx+in/4+2, iy+in/5, in/4, in*4/5, fg);
            break;
        case 3: // Dict: open book
            epd.drawLine(cx, iy, cx, iy+in, fg);
            epd.drawLine(ix, iy+2, cx, iy, fg);
            epd.drawLine(ix, iy+in, cx, iy+in, fg);
            epd.drawLine(cx, iy, ix+in, iy+2, fg);
            epd.drawLine(cx, iy+in, ix+in, iy+in, fg);
            break;
        case 4: // Settings: gear
            epd.drawRect(cx-in/4, cy-in/4, in/2, in/2, fg);
            epd.drawLine(cx, iy,        cx, iy+in/5,    fg);
            epd.drawLine(cx, iy+in*4/5, cx, iy+in,      fg);
            epd.drawLine(ix, cy,        ix+in/5, cy,     fg);
            epd.drawLine(ix+in*4/5, cy, ix+in, cy,       fg);
            break;
        default: break;
    }

    // Label below
    const char* label = kMenuItems[idx].label;
    int16_t lw = epd.getTextWidth(label, 9, selected);
    epd.drawText(cx - lw/2, y + s + 13, label, 9, selected, 0x0000);

    // Underline for selected item
    if (selected) {
        epd.drawLine(x, y + s + 15, x + s, y + s + 15, 0x0000);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// _drawBookCarousel — top zone of home screen
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::_drawBookCarousel() {
    EPDDisplay& epd = EPDDisplay::instance();

    const int16_t zoneTop = CONTENT_Y;
    const int16_t zoneH   = ZONE_DIV_Y - zoneTop;
    const int16_t cx      = EPD_WIDTH / 2;
    const int16_t cy      = zoneTop + zoneH / 2 - 10;

    // Zone focus indicator: bold label when this zone is active
    uint16_t labelColor = (_homeZone == 0) ? 0x0000 : 0x0000;
    bool labelBold = (_homeZone == 0);
    epd.drawText(MARGIN_X, zoneTop + 14, "Recently Read", 12, labelBold, labelColor);
    // Focus underline
    if (_homeZone == 0) {
        epd.drawLine(MARGIN_X, zoneTop + 16,
                     MARGIN_X + epd.getTextWidth("Recently Read", 12, true) + 2,
                     zoneTop + 16, 0x0000);
    }

    if (_recentCount == 0) {
        const char* msg = "No books read yet — go to Library";
        int16_t mw = epd.getTextWidth(msg, 12, false);
        epd.drawText(cx - mw/2, cy + 6, msg, 12, false, 0x0000);
        return;
    }

    // Horizontal offsets
    static constexpr int16_t OFF_ADJ   = 175;
    static constexpr int16_t OFF_GHOST = 310;

    int n = _recentCount;

    // Ghost left
    if (n > 2) {
        int gi = (_bookIndex - 2 + n) % n;
        _drawBookCard(cx - OFF_GHOST, cy, BOOK_GHOST_W, BOOK_GHOST_H,
                      _recentBooks[gi].title.c_str(), 0, 0, false);
    }
    // Adjacent left
    if (n > 1) {
        int ai = (_bookIndex - 1 + n) % n;
        _drawBookCard(cx - OFF_ADJ, cy, BOOK_ADJ_W, BOOK_ADJ_H,
                      _recentBooks[ai].title.c_str(), 0, 0, false);
    }
    // Centre
    _drawBookCard(cx, cy, BOOK_SEL_W, BOOK_SEL_H,
                  _recentBooks[_bookIndex].title.c_str(),
                  _recentBooks[_bookIndex].currentPage,
                  _recentBooks[_bookIndex].totalPages,
                  _homeZone == 0);
    // Adjacent right
    if (n > 1) {
        int ai = (_bookIndex + 1) % n;
        _drawBookCard(cx + OFF_ADJ, cy, BOOK_ADJ_W, BOOK_ADJ_H,
                      _recentBooks[ai].title.c_str(), 0, 0, false);
    }
    // Ghost right
    if (n > 2) {
        int gi = (_bookIndex + 2) % n;
        _drawBookCard(cx + OFF_GHOST, cy, BOOK_GHOST_W, BOOK_GHOST_H,
                      _recentBooks[gi].title.c_str(), 0, 0, false);
    }

    // Dot indicators
    if (n > 1 && n <= RECENT_BOOKS_MAX) {
        const int16_t dotR   = 3;
        const int16_t dotGap = 10;
        int16_t dotsW = (int16_t)(n * dotGap);
        int16_t dotX  = cx - dotsW / 2 + dotR;
        int16_t dotY  = ZONE_DIV_Y - 8;
        for (int i = 0; i < n; ++i) {
            if (i == _bookIndex && _homeZone == 0) {
                epd.fillRect(dotX - dotR, dotY - dotR, dotR*2, dotR*2, 0x0000);
            } else {
                epd.drawRect(dotX - dotR, dotY - dotR, dotR*2, dotR*2, 0x0000);
            }
            dotX += dotGap;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// _drawAppStrip — bottom zone of home screen
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::_drawAppStrip() {
    // 5 icons evenly spaced across 800px
    const int16_t step = EPD_WIDTH / MENU_ITEM_COUNT;
    const int16_t cy   = ZONE_DIV_Y + (EPD_HEIGHT - ZONE_DIV_Y - 24) / 2;

    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
        int16_t cx = (int16_t)(step / 2 + i * step);
        _drawAppIcon(cx, cy, i, i == _menuIndex && _homeZone == 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// renderHome
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderHome() {
    _drawBookCarousel();

    EPDDisplay& epd = EPDDisplay::instance();
    epd.drawLine(0, ZONE_DIV_Y, EPD_WIDTH - 1, ZONE_DIV_Y, 0x0000);

    _drawAppStrip();

    // Hint
    const char* hint = "L/R: browse   UP/DN: switch zone   SEL: open";
    int16_t hw = epd.getTextWidth(hint, 9, false);
    epd.drawText((EPD_WIDTH - hw) / 2, EPD_HEIGHT - 4, hint, 9, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderLibrary
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderLibrary() {
    EPDDisplay& epd = EPDDisplay::instance();

    epd.drawText(MARGIN_X, CONTENT_Y + 16, "Library", 20, true, 0x0000);
    epd.drawLine(MARGIN_X, CONTENT_Y + 20,
                 EPD_WIDTH - MARGIN_X, CONTENT_Y + 20, 0x0000);

    std::vector<String> books;
    bool ok = FileManager::instance().isReady() &&
              FileManager::instance().listDir(SD_BOOKS_DIR, books, nullptr);

    if (!ok || books.empty()) {
        epd.drawText(MARGIN_X, CONTENT_Y + 50,
                     "No books found.", 16, false, 0x0000);
        epd.drawText(MARGIN_X, CONTENT_Y + 72,
                     "Extract EPUBs to /books/<title>/", 12, false, 0x0000);
    } else {
        const int16_t lineH = epd.getLineHeight(16);
        int16_t       iy    = CONTENT_Y + 28 + lineH;
        for (const String& b : books) {
            if (iy + lineH > EPD_HEIGHT - 10) {
                epd.drawText(MARGIN_X, iy, "...", 12, false, 0x0000);
                break;
            }
            epd.drawText(MARGIN_X, iy, b.c_str(), 16, false, 0x0000);
            iy += lineH + 2;
        }
    }

    epd.drawText(MARGIN_X, EPD_HEIGHT - 5, "BACK to return", 12, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderReader
// (Placeholder — actual page rendering is driven by TextRenderer.)
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderReader() {
    EPDDisplay& epd = EPDDisplay::instance();

    epd.drawText(MARGIN_X, CONTENT_Y + 16, "Reader", 16, true, 0x0000);
    epd.drawText(MARGIN_X, CONTENT_Y + 40,
                 "No book open. Go to Library.", 16, false, 0x0000);
    epd.drawText(MARGIN_X, EPD_HEIGHT - 5, "BACK to return", 12, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderStats
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderStats() {
    EPDDisplay& epd = EPDDisplay::instance();

    epd.drawText(MARGIN_X, CONTENT_Y + 16, "Reading Stats", 20, true, 0x0000);
    epd.drawLine(MARGIN_X, CONTENT_Y + 20,
                 EPD_WIDTH - MARGIN_X, CONTENT_Y + 20, 0x0000);

    // Session timer from millis — simple uptime display without RTC
    uint32_t totalSec = millis() / 1000UL;
    uint32_t hh = totalSec / 3600UL;
    uint32_t mm = (totalSec % 3600UL) / 60UL;
    char buf[64];
    snprintf(buf, sizeof(buf), "Session time: %uh %02um", (unsigned)hh, (unsigned)mm);
    epd.drawText(MARGIN_X, CONTENT_Y + 46, buf, 16, false, 0x0000);

    epd.drawText(MARGIN_X, CONTENT_Y + 70,
                 "Full stats: see Stats module.", 12, false, 0x0000);

    epd.drawText(MARGIN_X, EPD_HEIGHT - 5, "BACK to return", 12, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderSettings
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderSettings() {
    EPDDisplay& epd = EPDDisplay::instance();

    epd.drawText(MARGIN_X, CONTENT_Y + 16, "Settings", 20, true, 0x0000);
    epd.drawLine(MARGIN_X, CONTENT_Y + 20,
                 EPD_WIDTH - MARGIN_X, CONTENT_Y + 20, 0x0000);

    const char* lines[] = {
        "Font Size:     Normal",
        "Bionic Mode:   Off",
        "Line Spacing:  4 px",
        "Auto-Sleep:    5 min",
        "FW: " FW_VERSION_STR,
    };
    const int16_t lineH = epd.getLineHeight(16);
    int16_t iy = CONTENT_Y + 28 + lineH;
    for (const char* l : lines) {
        epd.drawText(MARGIN_X, iy, l, 16, false, 0x0000);
        iy += lineH + 2;
    }

    epd.drawText(MARGIN_X, EPD_HEIGHT - 5, "BACK to return", 12, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderNotes
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderNotes() {
    EPDDisplay& epd = EPDDisplay::instance();

    epd.drawText(MARGIN_X, CONTENT_Y + 16, "Notes", 20, true, 0x0000);
    epd.drawLine(MARGIN_X, CONTENT_Y + 20,
                 EPD_WIDTH - MARGIN_X, CONTENT_Y + 20, 0x0000);

    std::vector<String> noteFiles;
    bool ok = FileManager::instance().isReady() &&
              FileManager::instance().listDir(SD_NOTES_DIR, noteFiles, ".txt");

    if (!ok || noteFiles.empty()) {
        epd.drawText(MARGIN_X, CONTENT_Y + 50,
                     "No notes yet.", 16, false, 0x0000);
    } else {
        const int16_t lineH = epd.getLineHeight(16);
        int16_t       iy    = CONTENT_Y + 28 + lineH;
        for (const String& nf : noteFiles) {
            if (iy + lineH > EPD_HEIGHT - 20) {
                epd.drawText(MARGIN_X, iy, "...", 12, false, 0x0000);
                break;
            }
            epd.drawText(MARGIN_X, iy, nf.c_str(), 16, false, 0x0000);
            iy += lineH + 2;
        }
    }

    epd.drawText(MARGIN_X, EPD_HEIGHT - 5, "BACK to return", 12, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderDict
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderDict() {
    EPDDisplay& epd = EPDDisplay::instance();

    epd.drawText(MARGIN_X, CONTENT_Y + 16, "Dictionary", 20, true, 0x0000);
    epd.drawLine(MARGIN_X, CONTENT_Y + 20,
                 EPD_WIDTH - MARGIN_X, CONTENT_Y + 20, 0x0000);

    epd.drawText(MARGIN_X, CONTENT_Y + 50,
                 "Look up: (no keyboard input yet)", 16, false, 0x0000);
    epd.drawText(MARGIN_X, CONTENT_Y + 74,
                 "Connect BLE keyboard to search.", 12, false, 0x0000);

    epd.drawText(MARGIN_X, EPD_HEIGHT - 5, "BACK to return", 12, false, 0x0000);
}
