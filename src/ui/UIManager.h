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

// ── Button indices (match GPIO order 0,1,3,20) ───────────────────────────
static constexpr uint8_t BTN_UP     = 0;  // GPIO 0
static constexpr uint8_t BTN_DOWN   = 1;  // GPIO 1
static constexpr uint8_t BTN_SELECT = 2;  // GPIO 3
static constexpr uint8_t BTN_BACK   = 3;  // GPIO 20

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

    // Number of items in the home menu (Library, Notes, Stats, Dict, Settings)
    static constexpr int MENU_ITEM_COUNT = 5;

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

    // ── Shared UI widgets ─────────────────────────────────────────────────
    // Draws the status bar at y=0 (height STATUS_H).
    void drawStatusBar();

    // Draws a single home-menu icon at grid position (col, row).
    // iconIndex selects the icon character/shape; label is drawn below.
    void drawMenuIcon(int16_t cx, int16_t cy,
                      int iconIndex,
                      const char* label,
                      bool selected);

    // ── State ─────────────────────────────────────────────────────────────
    Screen _current = Screen::SCREEN_HOME;

    // Navigation back-stack (up to 8 deep is plenty)
    std::vector<Screen> _history;

    // Home screen selection
    int _menuIndex = 0;

    // Whether the current screen needs a full redraw
    bool _dirty = true;

    // Status bar constants
    static constexpr int16_t STATUS_H   = 20;
    static constexpr int16_t CONTENT_Y  = STATUS_H + 2;

    // Icon grid layout
    static constexpr int16_t ICON_SIZE  = 40;  // box side length
    static constexpr int16_t ICON_GAP   = 16;  // horizontal gap between icons
};
