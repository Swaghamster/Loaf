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
static constexpr uint8_t BTN_BACK   = 0;
static constexpr uint8_t BTN_SELECT = 1;
static constexpr uint8_t BTN_LEFT   = 2;
static constexpr uint8_t BTN_RIGHT  = 3;
static constexpr uint8_t BTN_UP     = 4;
static constexpr uint8_t BTN_DOWN   = 5;

// ─────────────────────────────────────────────────────────────────────────────
// UIManager
// ─────────────────────────────────────────────────────────────────────────────
class UIManager {
public:
    static UIManager& instance();

    void init();
    void handleButton(uint8_t btn, bool longPress);
    void navigateTo(Screen s);
    void back();
    void render();

    Screen currentScreen() const { return _current; }
    int    selectedMenuItem() const { return _menuIndex; }

    static constexpr int MENU_ITEM_COUNT  = 5;
    static constexpr int RECENT_BOOKS_MAX = 10;

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

    // ── Home helpers ──────────────────────────────────────────────────────
    // Centre carousel: iPod-style app selector
    void _drawAppCarousel();
    // Bottom strip: small reference icons for all apps
    void _drawAppStrip();
    // Draw one carousel card centred at (cx, cy)
    void _drawCarouselCard(int16_t cx, int16_t cy, int16_t size,
                           int itemIndex, bool selected);
    // Draw one small bottom-strip icon
    void _drawAppIcon(int16_t cx, int16_t cy, int itemIndex, bool selected);

    // ── Shared ────────────────────────────────────────────────────────────
    void drawStatusBar();

    // ── State ─────────────────────────────────────────────────────────────
    Screen _current  = Screen::SCREEN_HOME;
    std::vector<Screen> _history;
    bool   _dirty    = true;
    int    _menuIndex = 0;   // selected app (0-4)

    // Recent books kept for future use (not shown on home in this layout)
    struct RecentBook {
        String title;
        String filename;
        int    currentPage = 0;
        int    totalPages  = 0;
    };
    RecentBook _recentBooks[RECENT_BOOKS_MAX];
    int        _recentCount = 0;

    // Status bar
    static constexpr int16_t STATUS_H   = 20;
    static constexpr int16_t CONTENT_Y  = STATUS_H + 2;

    // Carousel card sizes
    static constexpr int16_t CAR_SEL_SZ   = 160;  // selected (centre)
    static constexpr int16_t CAR_ADJ_SZ   = 105;  // adjacent
    static constexpr int16_t CAR_GHOST_SZ =  58;  // ghost (edge peek)

    // Bottom strip
    static constexpr int16_t STRIP_Y     = 390;   // top of strip zone
    static constexpr int16_t APP_ICON_SZ =  40;
};
