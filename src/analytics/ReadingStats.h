#pragma once

// ============================================================
// ReadingStats
// Persists reading sessions to /loaf/stats/stats.json on SD.
//
// No RTC is present, so:
//   - Session duration  = millis()-based elapsed seconds
//   - "Date"            = boot-count-based pseudo-date stored
//                         in NVS (Preferences).  The date starts
//                         at 2025-01-01 and advances one calendar
//                         day every time 86400 seconds of total
//                         uptime has accumulated since the last
//                         date advance.  The running uptime counter
//                         is also persisted in NVS so power cycles
//                         do not reset it.
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// Forward declarations for display types used in renderStatsScreen.
// Actual GxEPD2 / Adafruit-GFX headers are included only in the .cpp.
class GxEPD2_BW_R;   // not used directly here — kept for documentation

// ---------------------------------------------------------
// Data structures
// ---------------------------------------------------------

struct ReadingSession {
    String   bookId;
    uint32_t startEpoch;    ///< pseudo-epoch (seconds since 2025-01-01 base)
    uint32_t durationSecs;
    int      pagesRead;
};

struct BookStats {
    String   bookId;
    String   title;
    uint32_t totalSecs;
    int      totalPages;
    int      sessions;
    uint32_t lastRead;      ///< pseudo-epoch of last session
    bool     finished;
};

struct DailyRecord {
    String   date;          ///< "YYYY-MM-DD"
    uint32_t totalSecs;
};

// ---------------------------------------------------------
// ReadingStats class
// ---------------------------------------------------------

class ReadingStats {
public:
    static ReadingStats& instance();

    ReadingStats(const ReadingStats&)            = delete;
    ReadingStats& operator=(const ReadingStats&) = delete;

    // ---------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------
    void init();

    // ---------------------------------------------------------
    // Session management
    // ---------------------------------------------------------

    /// Begin a new reading session for bookId.
    void startSession(const String& bookId, const String& title);

    /// End the active session, recording pagesRead.
    void endSession(int pagesRead);

    /// Pause the running timer (e.g. device goes to sleep).
    void pauseSession();

    /// Resume a paused session.
    void resumeSession();

    // ---------------------------------------------------------
    // Queries
    // ---------------------------------------------------------

    BookStats              getBookStats(const String& bookId);
    std::vector<BookStats> getAllBooks();

    /// Total seconds read today (pseudo-date).
    uint32_t getTodaySecs();

    /// Number of consecutive pseudo-days with reading activity.
    uint32_t getStreakDays();

    /// All-time total reading seconds.
    uint32_t getTotalSecs();

    // ---------------------------------------------------------
    // Daily goal
    // ---------------------------------------------------------
    int   getDailyGoalSecs() const  { return _dailyGoalSecs; }
    void  setDailyGoalSecs(int secs);

    /// Fraction of today's goal completed (0.0 – 1.0, clamped).
    float getDailyProgress();

    // ---------------------------------------------------------
    // Display
    // ---------------------------------------------------------

    /// Draw a stats summary screen on the EPD:
    ///   - Today's reading time
    ///   - Streak counter
    ///   - Daily goal progress bar
    ///   - Top 3 most-read books
    void renderStatsScreen();

    // ---------------------------------------------------------
    // Pseudo-date helpers (public so other modules may use them)
    // ---------------------------------------------------------

    /// Return today's pseudo-date as "YYYY-MM-DD".
    String todayString();

    /// Return the pseudo-epoch (seconds since 2025-01-01T00:00:00).
    uint32_t pseudoEpoch();

private:
    ReadingStats() = default;

    // ---------------------------------------------------------
    // Persistence
    // ---------------------------------------------------------
    bool _load();
    bool _save();

    // ---------------------------------------------------------
    // NVS (Preferences) helpers
    // ---------------------------------------------------------
    void     _nvsInit();
    void     _nvsSave();
    uint32_t _nvsUptimeSecs();   ///< accumulated uptime seconds (cross-boot)
    void     _nvsAddUptime(uint32_t secs);
    int      _nvsPseudoDay();    ///< days elapsed since 2025-01-01 (0-based)
    void     _nvsAdvanceDay();

    // ---------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------
    BookStats* _findOrCreateBook(const String& bookId, const String& title);
    void       _updateDailyRecord(const String& date, uint32_t secs);
    String     _dayFromEpoch(uint32_t epoch);  ///< epoch → "YYYY-MM-DD"
    void       _checkDayRollover();

    // ---------------------------------------------------------
    // State
    // ---------------------------------------------------------
    std::vector<BookStats>   _books;
    std::vector<DailyRecord> _daily;
    int                      _dailyGoalSecs = 20 * 60; // default 20 min

    // Active session
    bool     _sessionActive  = false;
    bool     _sessionPaused  = false;
    String   _sessionBookId;
    String   _sessionTitle;
    uint32_t _sessionStartMs = 0;   ///< millis() at start (or last resume)
    uint32_t _sessionAccumSecs = 0; ///< seconds accumulated before last pause

    // Pseudo-date tracking
    uint32_t _bootMillisBase = 0;   ///< millis() at init() — used for uptime
    int      _pseudoDay      = 0;   ///< days since 2025-01-01
    uint32_t _uptimeAtDayStart = 0; ///< accumulated uptime at start of current day

    static constexpr const char* STATS_PATH = "/loaf/stats/stats.json";
    static constexpr const char* NVS_NS     = "loaf_stats";
    static constexpr uint32_t    SECS_PER_DAY = 86400UL;
    // Base date: 2025-01-01 represented as components for calendar arithmetic
    static constexpr int BASE_YEAR  = 2025;
    static constexpr int BASE_MONTH = 1;
    static constexpr int BASE_DAY   = 1;
};
