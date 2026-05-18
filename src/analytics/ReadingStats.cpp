// ============================================================
// ReadingStats.cpp
// Persists reading sessions to /loaf/stats/stats.json on SD.
// ============================================================

#include "ReadingStats.h"
#include "../storage/FileManager.h"
#include "../display/EPDDisplay.h"
#include "../../include/config.h"

#include <Preferences.h>   // ESP32 NVS
#include <ArduinoJson.h>
#include <algorithm>       // std::sort, std::min

// ============================================================
// Singleton
// ============================================================

ReadingStats& ReadingStats::instance() {
    static ReadingStats inst;
    return inst;
}

// ============================================================
// Lifecycle
// ============================================================

void ReadingStats::init() {
    _bootMillisBase = millis();

    _nvsInit();

    if (!_load()) {
        // Fresh install — start with empty state
        _books.clear();
        _daily.clear();
    }

    // Advance pseudo-day if enough uptime has accumulated
    _checkDayRollover();

    Serial.println(F("[ReadingStats] init complete"));
}

// ============================================================
// Session management
// ============================================================

void ReadingStats::startSession(const String& bookId, const String& title) {
    if (_sessionActive) {
        // Close the previous session without recorded pages
        endSession(0);
    }

    _sessionBookId    = bookId;
    _sessionTitle     = title;
    _sessionStartMs   = millis();
    _sessionAccumSecs = 0;
    _sessionActive    = true;
    _sessionPaused    = false;

    // Ensure the book record exists
    _findOrCreateBook(bookId, title);

    Serial.printf("[ReadingStats] session started: %s\n", bookId.c_str());
}

void ReadingStats::endSession(int pagesRead) {
    if (!_sessionActive) return;

    uint32_t elapsedSecs = _sessionAccumSecs;
    if (!_sessionPaused) {
        elapsedSecs += (millis() - _sessionStartMs) / 1000UL;
    }

    _sessionActive  = false;
    _sessionPaused  = false;

    if (elapsedSecs == 0 && pagesRead == 0) return;

    // Update book stats
    BookStats* bs = _findOrCreateBook(_sessionBookId, _sessionTitle);
    if (bs) {
        bs->totalSecs  += elapsedSecs;
        bs->totalPages += pagesRead;
        bs->sessions   += 1;
        bs->lastRead    = pseudoEpoch();
    }

    // Update daily record
    _updateDailyRecord(todayString(), elapsedSecs);

    // Persist accumulated uptime to NVS for day-rollover tracking
    uint32_t uptimeNow = (millis() - _bootMillisBase) / 1000UL;
    _nvsAddUptime(uptimeNow);

    _save();

    Serial.printf("[ReadingStats] session ended: %us, %d pages\n",
                  elapsedSecs, pagesRead);
}

void ReadingStats::pauseSession() {
    if (!_sessionActive || _sessionPaused) return;

    _sessionAccumSecs += (millis() - _sessionStartMs) / 1000UL;
    _sessionPaused     = true;

    Serial.println(F("[ReadingStats] session paused"));
}

void ReadingStats::resumeSession() {
    if (!_sessionActive || !_sessionPaused) return;

    _sessionStartMs = millis();
    _sessionPaused  = false;

    Serial.println(F("[ReadingStats] session resumed"));
}

// ============================================================
// Queries
// ============================================================

BookStats ReadingStats::getBookStats(const String& bookId) {
    for (const BookStats& bs : _books) {
        if (bs.bookId == bookId) return bs;
    }
    // Return an empty record if not found
    BookStats empty;
    empty.bookId = bookId;
    return empty;
}

std::vector<BookStats> ReadingStats::getAllBooks() {
    return _books;
}

uint32_t ReadingStats::getTodaySecs() {
    String today = todayString();
    uint32_t secs = 0;

    for (const DailyRecord& dr : _daily) {
        if (dr.date == today) {
            secs = dr.totalSecs;
            break;
        }
    }

    // Add any in-progress session time
    if (_sessionActive && !_sessionPaused) {
        secs += _sessionAccumSecs + (millis() - _sessionStartMs) / 1000UL;
    } else if (_sessionActive && _sessionPaused) {
        secs += _sessionAccumSecs;
    }

    return secs;
}

uint32_t ReadingStats::getStreakDays() {
    if (_daily.empty()) return 0;

    // Build a sorted list of unique date strings that have reading time
    std::vector<String> activeDates;
    for (const DailyRecord& dr : _daily) {
        if (dr.totalSecs > 0) {
            activeDates.push_back(dr.date);
        }
    }

    if (activeDates.empty()) return 0;

    // Sort ascending
    std::sort(activeDates.begin(), activeDates.end());

    // Include today if there is active session time
    String today = todayString();
    if (getTodaySecs() > 0) {
        bool found = false;
        for (const String& d : activeDates) {
            if (d == today) { found = true; break; }
        }
        if (!found) activeDates.push_back(today);
        std::sort(activeDates.begin(), activeDates.end());
    }

    // Count consecutive days ending at the last date
    uint32_t streak = 1;
    for (int i = static_cast<int>(activeDates.size()) - 1; i > 0; --i) {
        // Compare dates by converting to pseudo-epoch offsets
        // Dates are "YYYY-MM-DD"; we compare them as strings (lexicographic
        // sort equals chronological for ISO-8601 dates).
        // Determine if activeDates[i] and activeDates[i-1] are consecutive.
        // We use a day-difference helper via _dayFromEpoch inverse.
        // Simple approach: parse both dates, compute day difference.
        const String& d1 = activeDates[i - 1];
        const String& d2 = activeDates[i];

        // Parse "YYYY-MM-DD"
        int y1 = d1.substring(0, 4).toInt();
        int m1 = d1.substring(5, 7).toInt();
        int day1 = d1.substring(8, 10).toInt();

        int y2 = d2.substring(0, 4).toInt();
        int m2 = d2.substring(5, 7).toInt();
        int day2 = d2.substring(8, 10).toInt();

        // Convert both to a day count from a fixed origin (simple Julian-day-like)
        auto toDayNum = [](int y, int m, int d) -> long {
            // Zeller / simple formula: days since year 0
            long a = (14 - m) / 12;
            long yy = y + 4800 - a;
            long mm = m + 12 * a - 3;
            return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4
                   - yy / 100 + yy / 400 - 32045;
        };

        long jd1 = toDayNum(y1, m1, day1);
        long jd2 = toDayNum(y2, m2, day2);

        if (jd2 - jd1 == 1) {
            ++streak;
        } else {
            break;
        }
    }

    return streak;
}

uint32_t ReadingStats::getTotalSecs() {
    uint32_t total = 0;
    for (const BookStats& bs : _books) {
        total += bs.totalSecs;
    }
    // Add current in-progress session
    if (_sessionActive && !_sessionPaused) {
        total += _sessionAccumSecs + (millis() - _sessionStartMs) / 1000UL;
    } else if (_sessionActive && _sessionPaused) {
        total += _sessionAccumSecs;
    }
    return total;
}

// ============================================================
// Daily goal
// ============================================================

void ReadingStats::setDailyGoalSecs(int secs) {
    _dailyGoalSecs = (secs > 0) ? secs : 0;
    _save();
}

float ReadingStats::getDailyProgress() {
    if (_dailyGoalSecs <= 0) return 1.0f;
    float progress = static_cast<float>(getTodaySecs()) /
                     static_cast<float>(_dailyGoalSecs);
    if (progress > 1.0f) progress = 1.0f;
    return progress;
}

// ============================================================
// Pseudo-date helpers
// ============================================================

uint32_t ReadingStats::pseudoEpoch() {
    // Total accumulated uptime in seconds across all boots
    uint32_t uptimeSecs = _nvsUptimeSecs();
    // Add current boot's contribution so far
    uptimeSecs += (millis() - _bootMillisBase) / 1000UL;
    return uptimeSecs;
}

String ReadingStats::todayString() {
    return _dayFromEpoch(static_cast<uint32_t>(_pseudoDay));
}

// ============================================================
// Display
// ============================================================

void ReadingStats::renderStatsScreen() {
    EPDDisplay& epd = EPDDisplay::instance();

    // White background
    epd.fillRect(0, 0, EPD_WIDTH, EPD_HEIGHT, GxEPD_WHITE);

    // ── Title bar ─────────────────────────────────────────────
    epd.fillRect(0, 0, EPD_WIDTH, 24, GxEPD_BLACK);
    epd.drawText(8, 17, "Reading Stats", FONT_NORMAL, /*bold=*/true, GxEPD_WHITE);

    int16_t y = 34;

    // ── Today's reading time ─────────────────────────────────
    uint32_t todaySec = getTodaySecs();
    uint32_t todayMin = todaySec / 60;
    uint32_t todaySec2 = todaySec % 60;

    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "Today: %um %02us", todayMin, todaySec2);
    epd.drawText(8, y, timeBuf, FONT_NORMAL, /*bold=*/false);
    y += 22;

    // ── Streak ────────────────────────────────────────────────
    uint32_t streak = getStreakDays();
    char streakBuf[32];
    snprintf(streakBuf, sizeof(streakBuf), "Streak: %u day%s",
             streak, streak == 1 ? "" : "s");
    epd.drawText(8, y, streakBuf, FONT_NORMAL, /*bold=*/false);
    y += 22;

    // ── Daily goal progress bar ───────────────────────────────
    float progress = getDailyProgress();
    const int16_t BAR_X = 8;
    const int16_t BAR_W = EPD_WIDTH - 16;
    const int16_t BAR_H = 16;

    // Label
    uint32_t goalMin = static_cast<uint32_t>(_dailyGoalSecs) / 60;
    char goalBuf[48];
    snprintf(goalBuf, sizeof(goalBuf), "Daily goal: %um  (%.0f%%)",
             goalMin, progress * 100.0f);
    epd.drawText(8, y, goalBuf, FONT_SMALL, /*bold=*/false);
    y += 16;

    // Outline
    epd.drawRect(BAR_X, y, BAR_W, BAR_H, GxEPD_BLACK);
    // Fill
    int16_t fillW = static_cast<int16_t>(BAR_W * progress);
    if (fillW > 0) {
        epd.fillRect(BAR_X, y, fillW, BAR_H, GxEPD_BLACK);
    }
    y += BAR_H + 8;

    // ── Divider ───────────────────────────────────────────────
    epd.drawLine(8, y, EPD_WIDTH - 8, y, GxEPD_BLACK);
    y += 6;

    // ── Top 3 books ───────────────────────────────────────────
    epd.drawText(8, y, "Top Books:", FONT_SMALL, /*bold=*/true);
    y += 16;

    // Sort books by totalSecs descending
    std::vector<BookStats> sorted = _books;
    std::sort(sorted.begin(), sorted.end(),
              [](const BookStats& a, const BookStats& b) {
                  return a.totalSecs > b.totalSecs;
              });

    int shown = 0;
    for (const BookStats& bs : sorted) {
        if (shown >= 3) break;
        if (y + 18 > EPD_HEIGHT) break;

        uint32_t mins = bs.totalSecs / 60;
        char bookBuf[64];
        // Truncate title to avoid overflow
        String shortTitle = bs.title.length() > 22
                          ? bs.title.substring(0, 21) + String('\x85') // ellipsis
                          : bs.title;
        snprintf(bookBuf, sizeof(bookBuf), "%d. %s  %um",
                 shown + 1, shortTitle.c_str(), mins);
        epd.drawText(8, y, bookBuf, FONT_SMALL, /*bold=*/false);
        y += 17;
        ++shown;
    }

    if (shown == 0) {
        epd.drawText(8, y, "No books read yet.", FONT_SMALL, /*bold=*/false);
    }

    epd.update();
}

// ============================================================
// Persistence
// ============================================================

bool ReadingStats::_load() {
    FileManager& fm = FileManager::instance();
    String raw;

    if (!fm.readFile(STATS_PATH, raw) || raw.length() == 0) {
        return false;
    }

    // Use a heap-allocated document to avoid large stack allocation
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, raw);
    if (err) {
        Serial.printf("[ReadingStats] JSON parse error: %s\n", err.c_str());
        return false;
    }

    // Daily goal
    _dailyGoalSecs = doc["dailyGoalSecs"] | (20 * 60);

    // Books
    _books.clear();
    JsonArray booksArr = doc["books"].as<JsonArray>();
    for (JsonObject obj : booksArr) {
        BookStats bs;
        bs.bookId     = obj["bookId"]     | "";
        bs.title      = obj["title"]      | "";
        bs.totalSecs  = obj["totalSecs"]  | (uint32_t)0;
        bs.totalPages = obj["totalPages"] | 0;
        bs.sessions   = obj["sessions"]   | 0;
        bs.lastRead   = obj["lastRead"]   | (uint32_t)0;
        bs.finished   = obj["finished"]   | false;
        if (bs.bookId.length() > 0) {
            _books.push_back(bs);
        }
    }

    // Daily records
    _daily.clear();
    JsonArray dailyArr = doc["daily"].as<JsonArray>();
    for (JsonObject obj : dailyArr) {
        DailyRecord dr;
        dr.date      = obj["date"]      | "";
        dr.totalSecs = obj["totalSecs"] | (uint32_t)0;
        if (dr.date.length() > 0) {
            _daily.push_back(dr);
        }
    }

    return true;
}

bool ReadingStats::_save() {
    JsonDocument doc;

    doc["dailyGoalSecs"] = _dailyGoalSecs;

    JsonArray booksArr = doc["books"].to<JsonArray>();
    for (const BookStats& bs : _books) {
        JsonObject obj = booksArr.add<JsonObject>();
        obj["bookId"]     = bs.bookId;
        obj["title"]      = bs.title;
        obj["totalSecs"]  = bs.totalSecs;
        obj["totalPages"] = bs.totalPages;
        obj["sessions"]   = bs.sessions;
        obj["lastRead"]   = bs.lastRead;
        obj["finished"]   = bs.finished;
    }

    JsonArray dailyArr = doc["daily"].to<JsonArray>();
    for (const DailyRecord& dr : _daily) {
        JsonObject obj = dailyArr.add<JsonObject>();
        obj["date"]      = dr.date;
        obj["totalSecs"] = dr.totalSecs;
    }

    String out;
    serializeJson(doc, out);

    FileManager& fm = FileManager::instance();
    // Ensure directory exists
    fm.makeDir("/loaf/stats");

    if (!fm.writeFile(STATS_PATH, out)) {
        Serial.println(F("[ReadingStats] save failed"));
        return false;
    }
    return true;
}

// ============================================================
// NVS helpers
// ============================================================

// NVS keys
static constexpr const char* NVS_KEY_UPTIME   = "uptime_s";
static constexpr const char* NVS_KEY_PDAY     = "pday";

void ReadingStats::_nvsInit() {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/false);

    _pseudoDay         = prefs.getInt(NVS_KEY_PDAY, 0);
    // _uptimeAtDayStart tracks how many uptime-seconds had accumulated
    // at the start of the current pseudo-day (used for rollover detection).
    _uptimeAtDayStart  = prefs.getUInt("day_base_s", 0);

    prefs.end();
}

void ReadingStats::_nvsSave() {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putInt(NVS_KEY_PDAY, _pseudoDay);
    prefs.putUInt("day_base_s", static_cast<uint32_t>(_uptimeAtDayStart));
    prefs.end();
}

uint32_t ReadingStats::_nvsUptimeSecs() {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/true);
    uint32_t val = prefs.getUInt(NVS_KEY_UPTIME, 0);
    prefs.end();
    return val;
}

void ReadingStats::_nvsAddUptime(uint32_t additionalSecs) {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/false);
    uint32_t current = prefs.getUInt(NVS_KEY_UPTIME, 0);
    prefs.putUInt(NVS_KEY_UPTIME, current + additionalSecs);
    prefs.end();
}

int ReadingStats::_nvsPseudoDay() {
    return _pseudoDay;
}

void ReadingStats::_nvsAdvanceDay() {
    ++_pseudoDay;
    _nvsSave();
    Serial.printf("[ReadingStats] pseudo-day advanced to %d (%s)\n",
                  _pseudoDay, todayString().c_str());
}

// ============================================================
// Internal helpers
// ============================================================

BookStats* ReadingStats::_findOrCreateBook(const String& bookId,
                                           const String& title) {
    for (BookStats& bs : _books) {
        if (bs.bookId == bookId) {
            // Update title if it changed
            if (title.length() > 0 && bs.title != title) {
                bs.title = title;
            }
            return &bs;
        }
    }
    // Create new record
    BookStats fresh;
    fresh.bookId     = bookId;
    fresh.title      = title;
    fresh.totalSecs  = 0;
    fresh.totalPages = 0;
    fresh.sessions   = 0;
    fresh.lastRead   = 0;
    fresh.finished   = false;
    _books.push_back(fresh);
    return &_books.back();
}

void ReadingStats::_updateDailyRecord(const String& date, uint32_t secs) {
    for (DailyRecord& dr : _daily) {
        if (dr.date == date) {
            dr.totalSecs += secs;
            return;
        }
    }
    DailyRecord fresh;
    fresh.date      = date;
    fresh.totalSecs = secs;
    _daily.push_back(fresh);
}

String ReadingStats::_dayFromEpoch(uint32_t dayOffset) {
    // Base date: 2025-01-01
    // dayOffset = number of days since base date (0-based)
    int year  = BASE_YEAR;
    int month = BASE_MONTH;
    int day   = BASE_DAY;

    uint32_t remaining = dayOffset;

    // Days per month (non-leap year base; we correct for leap years in loop)
    static const uint8_t dpm[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

    auto isLeap = [](int y) -> bool {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };

    while (remaining > 0) {
        uint8_t daysInMonth = dpm[month];
        if (month == 2 && isLeap(year)) daysInMonth = 29;

        uint32_t daysLeft = static_cast<uint32_t>(daysInMonth - day + 1);

        if (remaining < daysLeft) {
            day += static_cast<int>(remaining);
            remaining = 0;
        } else {
            remaining -= daysLeft;
            day = 1;
            ++month;
            if (month > 12) {
                month = 1;
                ++year;
            }
        }
    }

    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return String(buf);
}

void ReadingStats::_checkDayRollover() {
    // Total accumulated uptime across boots including this boot's time so far
    uint32_t savedUptime    = _nvsUptimeSecs();
    uint32_t thisBootUptime = (millis() - _bootMillisBase) / 1000UL;
    uint32_t totalUptime    = savedUptime + thisBootUptime;

    // How many full SECS_PER_DAY have elapsed since the day base?
    // Guard against corrupt NVS: if day_base is ahead of totalUptime, reset it.
    if (_uptimeAtDayStart > totalUptime) {
        _uptimeAtDayStart = 0;
    }
    uint32_t secsIntoCurrentDay = totalUptime - _uptimeAtDayStart;

    while (secsIntoCurrentDay >= SECS_PER_DAY) {
        _uptimeAtDayStart += SECS_PER_DAY;
        secsIntoCurrentDay -= SECS_PER_DAY;
        _nvsAdvanceDay();
    }

    _nvsSave();
}
