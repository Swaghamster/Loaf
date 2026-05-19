#pragma once

#include <Arduino.h>
#include <vector>

// ── Screen identifiers ────────────────────────────────────────────────────
enum class Screen : uint8_t {
    SCREEN_HOME = 0,
    SCREEN_LIBRARY,
    SCREEN_READER,
    SCREEN_STATS,
    SCREEN_SETTINGS,
    SCREEN_NOTES,
    SCREEN_DICT,
};

// ── Button indices — must match InputManager constants exactly ────────────
// InputManager: BACK=0, CONFIRM=1, LEFT=2, RIGHT=3, UP=4, DOWN=5, POWER=6
static constexpr uint8_t BTN_BACK   = 0;  // ADC1 lowest resistance
static constexpr uint8_t BTN_SELECT = 1;  // ADC1 CONFIRM
static constexpr uint8_t BTN_LEFT   = 2;  // ADC1 LEFT
static constexpr uint8_t BTN_RIGHT  = 3;  // ADC1 RIGHT
static constexpr uint8_t BTN_UP     = 4;  // ADC2 UP
static constexpr uint8_t BTN_DOWN   = 5;  // ADC2 DOWN

// ─────────────────────────────────────────────────────────────────────────────
// UIManager
//
// Orchestrates all screens: routes button events, drives rendering, and
// maintains a small navigation stack so "back" works across screens.
// ─────────────────────────────────────────────────────────────────────────────
class UIManager {
public:
    static UIManager& instance();

    void init();

    // Called by the button driver with the logical button index (BTN_*)
    // and whether the press was held for >600 ms (long press).
    void handleButton(uint8_t btn, bool longPress);

    // Navigate to a new screen, pushing the current one onto the back stack.
    void navigateTo(Screen s);

    // Pop the navigation stack; returns to the previous screen.
    void back();

    // Redraw the current screen onto the e-paper display.
    void render();

    Screen currentScreen() const { return _current; }

    // Home menu: which icon is highlighted (0-based)
    int  selectedMenuItem() const { return _menuIndex; }

    // Number of items in the app strip (Library, Notes, Stats, Dict, Settings)
    static constexpr int MENU_ITEM_COUNT    = 5;
    // Max recent books shown in the top carousel
    static constexpr int RECENT_BOOKS_MAX   = 10;

    // Called by the reader when a book is opened — updates the recent list.
    void recordRecentBook(const String& title, const String& filename,
                          int currentPage, int totalPages);

private:
    UIManager() = default;

    // ── Screen renderers ──────────────────────────────────────────────────
    void renderHome();
    void renderLibrary();
    void renderReader();
    void renderStats();
    void renderSettings();
    void renderNotes();
    void renderDict();

    // ── Home sub-renderers ────────────────────────────────────────────────
    void _drawBookCarousel();   // top zone: recent books
    void _drawAppStrip();       // bottom zone: app icons

    // ── Shared UI widgets ─────────────────────────────────────────────────
    void drawStatusBar();

    // Draw one book card centred at (cx, cy).
    // bookDir = bare directory name under /books/ (used to locate cover.bmp).
    void _drawBookCard(int16_t cx, int16_t cy, int16_t w, int16_t h,
                       const char* bookDir, const char* title,
                       int page, int total, bool selected);

    // Draw one app icon centred at (cx, cy).
    void _drawAppIcon(int16_t cx, int16_t cy, int itemIndex, bool selected);

    // ── State ─────────────────────────────────────────────────────────────
    Screen _current   = Screen::SCREEN_HOME;
    std::vector<Screen> _history;
    bool _dirty = true;

    // Home — which zone is focused: 0 = book carousel, 1 = app strip
    int _homeZone     = 0;
    // Book carousel selection
    int _bookIndex    = 0;
    // App strip selection
    int _menuIndex    = 0;

    // Recent books ring buffer (newest at index 0)
    struct RecentBook {
        String title;
        String filename;
        int    currentPage = 0;
        int    totalPages  = 0;
    };
    RecentBook       _recentBooks[RECENT_BOOKS_MAX];
    int              _recentCount = 0;

    // Status bar
    static constexpr int16_t STATUS_H    = 20;
    static constexpr int16_t CONTENT_Y   = STATUS_H + 2;

    // Home layout zones
    static constexpr int16_t ZONE_DIV_Y  = 295;   // y of divider between carousel & strip

    // Book carousel card sizes (w × h — portrait book shape)
    static constexpr int16_t BOOK_SEL_W  = 150;
    static constexpr int16_t BOOK_SEL_H  = 190;
    static constexpr int16_t BOOK_ADJ_W  = 100;
    static constexpr int16_t BOOK_ADJ_H  = 130;
    static constexpr int16_t BOOK_GHOST_W =  60;
    static constexpr int16_t BOOK_GHOST_H =  80;

    // App strip icon size
    static constexpr int16_t APP_ICON_SZ =  50;
};
