#include "UIManager.h"
#include "../display/EPDDisplay.h"
#include "../storage/FileManager.h"
#include "../../include/config.h"

// ─────────────────────────────────────────────────────────────────────────────
// Home menu layout
//
// 5 items arranged in a single row centred on the display.
// Each icon is a labelled box drawn inside the content area.
//
//   [Library] [Notes] [Stats] [Dict] [Settings]
//
// ─────────────────────────────────────────────────────────────────────────────

// Menu item descriptors
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

        // ── Home screen ──────────────────────────────────────────────────────
        case Screen::SCREEN_HOME:
            if (btn == BTN_UP) {
                _menuIndex = (_menuIndex - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
                _dirty = true;
            } else if (btn == BTN_DOWN) {
                _menuIndex = (_menuIndex + 1) % MENU_ITEM_COUNT;
                _dirty = true;
            } else if (btn == BTN_SELECT) {
                navigateTo(kMenuItems[_menuIndex].screen);
            }
            // Long press on SELECT from home → do nothing special
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
// drawMenuIcon
//
// Draws a single home-menu icon at centre position (cx, cy).
// The icon is a box with an internal decorative symbol, plus a text label
// drawn below. When selected=true the box is filled black (inverted).
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::drawMenuIcon(int16_t cx, int16_t cy,
                             int iconIndex,
                             const char* label,
                             bool selected) {
    EPDDisplay& epd = EPDDisplay::instance();

    const int16_t half  = ICON_SIZE / 2;
    const int16_t x     = cx - half;
    const int16_t y     = cy - half;

    if (selected) {
        // Filled black box for selected state
        epd.fillRect(x, y, ICON_SIZE, ICON_SIZE, 0x0000);
    } else {
        epd.fillRect(x, y, ICON_SIZE, ICON_SIZE, 0xFFFF);
        epd.drawRect(x, y, ICON_SIZE, ICON_SIZE, 0x0000);
    }

    uint16_t fgColor = selected ? 0xFFFF : 0x0000;

    // Draw a simple distinguishing symbol inside the box based on iconIndex
    switch (iconIndex) {
        case 0: // Library — three horizontal lines (book icon)
            for (int row = 0; row < 3; ++row) {
                int16_t ly = y + 10 + row * 8;
                epd.drawLine(x + 8, ly, x + ICON_SIZE - 8, ly, fgColor);
            }
            break;
        case 1: // Notes — lined page icon
            epd.drawRect(x + 8, y + 6, ICON_SIZE - 16, ICON_SIZE - 12, fgColor);
            for (int row = 0; row < 3; ++row) {
                int16_t ly = y + 13 + row * 7;
                epd.drawLine(x + 12, ly, x + ICON_SIZE - 12, ly, fgColor);
            }
            break;
        case 2: // Stats — bar chart
            epd.fillRect(x + 8,  y + 22, 6, 12, fgColor);
            epd.fillRect(x + 17, y + 16, 6, 18, fgColor);
            epd.fillRect(x + 26, y + 10, 6, 24, fgColor);
            break;
        case 3: // Dict — capital D outline
            epd.drawRect(x + 10, y + 8, 14, 24, fgColor);
            epd.drawLine(x + 10, y + 8,  x + 18, y + 8,  fgColor);
            epd.drawLine(x + 10, y + 32, x + 18, y + 32, fgColor);
            epd.drawLine(x + 24, y + 14, x + 24, y + 26, fgColor);
            break;
        case 4: // Settings — cogwheel approximation (circle + dots)
            epd.drawRect(x + 12, y + 12, 16, 16, fgColor);
            // Cardinal notch lines
            epd.drawLine(cx, y + 6,  cx, y + 10, fgColor);
            epd.drawLine(cx, y + 30, cx, y + 34, fgColor);
            epd.drawLine(x + 6,  cy, x + 10, cy, fgColor);
            epd.drawLine(x + 30, cy, x + 34, cy, fgColor);
            break;
        default:
            // Generic: just the box, no symbol
            break;
    }

    // Label below the icon
    int16_t labelW = epd.getTextWidth(label, 12, false);
    int16_t labelX = cx - labelW / 2;
    int16_t labelY = y + ICON_SIZE + 14;   // baseline below the box
    epd.drawText(labelX, labelY, label, 12, false, 0x0000);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderHome
//
// Draws five icons arranged horizontally, centred vertically in the content
// area below the status bar.
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::renderHome() {
    EPDDisplay& epd = EPDDisplay::instance();

    // Title
    epd.drawText(4, CONTENT_Y + 16, "Loaf", 20, true, 0x0000);

    // Compute layout:
    // Total icon strip width = MENU_ITEM_COUNT * ICON_SIZE + (MENU_ITEM_COUNT-1) * ICON_GAP
    const int16_t totalW = (int16_t)(MENU_ITEM_COUNT * ICON_SIZE
                                   + (MENU_ITEM_COUNT - 1) * ICON_GAP);
    const int16_t startX = (EPD_WIDTH - totalW) / 2 + ICON_SIZE / 2;

    // Vertical centre of icons (account for label below)
    const int16_t iconCY = CONTENT_Y + (EPD_HEIGHT - CONTENT_Y) / 2 - 10;

    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
        int16_t cx = startX + (int16_t)i * (ICON_SIZE + ICON_GAP);
        drawMenuIcon(cx, iconCY, i, kMenuItems[i].label, i == _menuIndex);
    }

    // Navigation hint at bottom
    const char* hint = "UP/DOWN select  SELECT enter";
    int16_t hintW = epd.getTextWidth(hint, 12, false);
    epd.drawText((EPD_WIDTH - hintW) / 2, EPD_HEIGHT - 5, hint, 12, false, 0x0000);
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
