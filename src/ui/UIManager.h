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
    SCREEN_GAMES,
    SCREEN_APPS,
    SCREEN_TERMINAL,
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

    static constexpr int MENU_ITEM_COUNT   = 8;   // Library Notes Stats Dict Settings Games Apps Terminal
    static constexpr int MENU_VISIBLE_ROWS = 7;   // rows shown at once; list scrolls when count > 7
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
    void renderGames();
    void renderApps();
    void renderTerminal();

    // ── Home helpers ──────────────────────────────────────────────────────
    // Top zone: Cover Flow–style book carousel
    void _drawBookCarousel();
    void _drawBookCard(int16_t cx, int16_t cy, int16_t w, int16_t h,
                       int bookIdx, bool selected, bool zone_active);
    // Bottom zone: classic iPod vertical text menu
    void _drawMenuList();

    // ── Shared ────────────────────────────────────────────────────────────
    void drawStatusBar();

    // ── State ─────────────────────────────────────────────────────────────
    Screen _current   = Screen::SCREEN_HOME;
    std::vector<Screen> _history;
    bool   _dirty     = true;
    int    _menuIndex  = 0;   // selected row in iPod menu (0 – MENU_ITEM_COUNT-1)
    int    _menuScroll = 0;   // first visible row index (for scrolling)
    int    _homeZone   = 0;   // 0=book carousel, 1=menu list
    int    _bookIndex  = 0;   // selected book in carousel

    struct RecentBook {
        String title;
        String filename;
        int    currentPage = 0;
        int    totalPages  = 0;
    };
    RecentBook _recentBooks[RECENT_BOOKS_MAX];
    int        _recentCount = 0;

    // Status bar
    static constexpr int16_t STATUS_H  = 20;
    static constexpr int16_t CONTENT_Y = STATUS_H + 2;

    // Book carousel zone (top half)
    static constexpr int16_t BOOK_ZONE_TOP = CONTENT_Y;
    static constexpr int16_t BOOK_ZONE_BOT = 270;
    static constexpr int16_t BOOK_SEL_W    = 130;
    static constexpr int16_t BOOK_SEL_H    = 170;
    static constexpr int16_t BOOK_ADJ_W    =  85;
    static constexpr int16_t BOOK_ADJ_H    = 115;
    static constexpr int16_t BOOK_GHOST_W  =  48;
    static constexpr int16_t BOOK_GHOST_H  =  64;

    // iPod menu list zone (bottom half)
    static constexpr int16_t MENU_ZONE_TOP = 276;
    static constexpr int16_t MENU_ROW_H    =  29;   // px per row  (7 rows × 29 = 203 ≤ 204px zone)
};
