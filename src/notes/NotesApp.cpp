// ============================================================
// NotesApp.cpp
// Loaf Firmware — Notes list browser
// ============================================================

#include "NotesApp.h"
#include "NoteEditor.h"
#include "../display/EPDDisplay.h"
#include "../storage/FileManager.h"
#include "../ui/UIManager.h"
#include "../../include/config.h"


// ── Singleton ──────────────────────────────────────────────────
NotesApp& NotesApp::instance() {
    static NotesApp inst;
    return inst;
}

// ── Lifecycle ──────────────────────────────────────────────────
void NotesApp::init() {
    if (_initialised) return;
    _initialised = true;

    // Ensure the notes directory exists on SD
    FileManager::instance().makeDir(SD_NOTES_DIR);

    refreshList();
    _dirty = true;
}

// ── Render ─────────────────────────────────────────────────────
void NotesApp::render() {
    if (!_dirty) return;
    _dirty = false;

    EPDDisplay& epd = EPDDisplay::instance();

    if (_showDeleteConfirm && !_notes.empty()) {
        _drawDeleteDialog(_notes[_selectedIdx].title);
        epd.update();
        return;
    }

    // ── White canvas ──────────────────────────────────────────
    auto& raw = epd.canvas();
    epd.clear();
    do {
        

        _drawHeader();

        if (_notes.empty()) {
            _drawEmpty();
        } else {
            // Clamp scroll so selected is always in view
            if (_selectedIdx < _scrollOffset) {
                _scrollOffset = _selectedIdx;
            }
            if (_selectedIdx >= _scrollOffset + VISIBLE_ROWS) {
                _scrollOffset = _selectedIdx - VISIBLE_ROWS + 1;
            }

            int16_t y = HEADER_H;
            for (int i = 0; i < VISIBLE_ROWS; ++i) {
                int idx = _scrollOffset + i;
                if (idx >= static_cast<int>(_notes.size())) break;
                _drawNoteRow(y, _notes[idx], idx == _selectedIdx);
                y += ROW_H;
            }
        }

        // ── Footer hint bar ───────────────────────────────────
        {
            int16_t fy = EPD_HEIGHT - FOOTER_H;
            epd.canvas().drawFastHLine(0, fy, EPD_WIDTH, 0x0000);
            epd.drawText(ROW_MARGIN_X, fy + 14,
                         "UP/DN: scroll  SEL: open  MENU: new  SEL long: delete",
                         12, false, 0x0000);
        }

    epd.update();

    epd.update();
}

// ── Input ──────────────────────────────────────────────────────
void NotesApp::handleButton(uint8_t btn, bool longPress) {
    if (_showDeleteConfirm) {
        // Inside the delete confirmation dialog
        if (btn == BTN_UP || btn == BTN_DOWN) {
            _deleteYes = !_deleteYes;
            _dirty = true;
        } else if (btn == BTN_SELECT) {
            if (_deleteYes) {
                deleteNote(_notes[_selectedIdx].filename);
            }
            _showDeleteConfirm = false;
            _dirty = true;
        } else if (btn == BTN_BACK) {
            _showDeleteConfirm = false;
            _dirty = true;
        }
        return;
    }

    switch (btn) {
        case BTN_UP:
            if (_selectedIdx > 0) {
                --_selectedIdx;
                _dirty = true;
            }
            break;

        case BTN_DOWN:
            if (_selectedIdx + 1 < static_cast<int>(_notes.size())) {
                ++_selectedIdx;
                _dirty = true;
            }
            break;

        case BTN_SELECT:
            if (longPress) {
                // Long-press → confirm delete
                if (!_notes.empty()) {
                    _showDeleteConfirm = true;
                    _deleteYes = false;  // Default to NO for safety
                    _dirty = true;
                }
            } else {
                // Short press → open note
                if (!_notes.empty()) {
                    openNote(_notes[_selectedIdx].filename);
                }
            }
            break;

        case BTN_BACK:
            // BTN_BACK acts as MENU in the notes context → create new note
            createNote();
            break;

        default:
            break;
    }
}

// ── Note operations ────────────────────────────────────────────
void NotesApp::createNote() {
    String filename = _makeFilename();
    NoteEditor::instance().init(filename, String());
    NoteEditor::instance().render();
    // Control returns here when NoteEditor exits; refresh list
    refreshList();
    _dirty = true;
}

void NotesApp::openNote(const String& filename) {
    String path = String(SD_NOTES_DIR) + "/" + filename;
    String content;
    FileManager::instance().readFile(path.c_str(), content);

    NoteEditor::instance().init(filename, content);
    NoteEditor::instance().render();

    refreshList();
    _dirty = true;
}

void NotesApp::deleteNote(const String& filename) {
    String path = String(SD_NOTES_DIR) + "/" + filename;
    FileManager::instance().deleteFile(path.c_str());
    refreshList();

    // Clamp selected index
    if (_selectedIdx >= static_cast<int>(_notes.size())) {
        _selectedIdx = static_cast<int>(_notes.size()) - 1;
    }
    if (_selectedIdx < 0) _selectedIdx = 0;
    _dirty = true;
}

void NotesApp::refreshList() {
    _notes.clear();

    std::vector<String> names;
    if (!FileManager::instance().listDir(SD_NOTES_DIR, names, ".txt")) {
        Serial.println("[NotesApp] Cannot list notes dir");
        return;
    }

    // Most-recent first: filenames encode pseudo-time, so reverse-sort
    std::sort(names.begin(), names.end(),
              [](const String& a, const String& b) {
                  return a > b;  // Descending lexicographic = newest first
              });

    for (const auto& name : names) {
        if (static_cast<int>(_notes.size()) >= NOTES_MAX_COUNT) break;

        NoteEntry entry;
        entry.filename = name;
        String path = String(SD_NOTES_DIR) + "/" + name;
        entry.size = FileManager::instance().fileSize(path.c_str());

        if (!_readMeta(path, entry)) {
            entry.title   = name;
            entry.preview = "";
        }

        _notes.push_back(entry);
    }

    Serial.printf("[NotesApp] Loaded %d notes\n", static_cast<int>(_notes.size()));
}

// ── Private: render helpers ────────────────────────────────────

void NotesApp::_drawHeader() {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.canvas();

    // Filled header bar
    epd.canvas().fillRect(0, 0, EPD_WIDTH, HEADER_H, 0x0000);
    epd.drawText(ROW_MARGIN_X, HEADER_H - 7, "Notes", 16, true, 0xFFFF);

    // Note count on right
    String countStr = String(_notes.size()) + " note" +
                      (_notes.size() != 1 ? "s" : "");
    int16_t cw = epd.getTextWidth(countStr.c_str(), 12, false);
    epd.drawText(EPD_WIDTH - cw - ROW_MARGIN_X, HEADER_H - 6,
                 countStr.c_str(), 12, false, 0xFFFF);
}

void NotesApp::_drawNoteRow(int16_t y, const NoteEntry& entry, bool selected) {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.canvas();

    // Selection highlight
    if (selected) {
        epd.canvas().fillRect(0, y, EPD_WIDTH, ROW_H - 1, 0x0000);
    }

    uint16_t textColor = selected ? 0xFFFF : 0x0000;

    // Divider line at bottom of row (skip for selected — already a filled block)
    if (!selected) {
        epd.canvas().drawFastHLine(0, y + ROW_H - 1, EPD_WIDTH, 0x0000);
    }

    // Title (bold, 16pt)
    String title = entry.title.isEmpty() ? entry.filename : entry.title;
    if (title.length() > 40) title = title.substring(0, 40) + "...";
    epd.drawText(ROW_MARGIN_X, y + 17, title.c_str(), 16, true, textColor);

    // Preview (12pt, second line)
    String prev = entry.preview;
    if (prev.length() > 50) prev = prev.substring(0, 50) + "...";
    if (!prev.isEmpty()) {
        epd.drawText(ROW_MARGIN_X, y + 35, prev.c_str(), 12, false, textColor);
    }

    // File size on right of title line
    String sizeStr;
    if (entry.size < 1024) {
        sizeStr = String(entry.size) + "B";
    } else {
        sizeStr = String(entry.size / 1024) + "KB";
    }
    int16_t sw = epd.getTextWidth(sizeStr.c_str(), 12, false);
    epd.drawText(EPD_WIDTH - sw - ROW_MARGIN_X, y + 17,
                 sizeStr.c_str(), 12, false, textColor);
}

void NotesApp::_drawEmpty() {
    EPDDisplay& epd = EPDDisplay::instance();
    int16_t cx = EPD_WIDTH / 2;
    int16_t cy = EPD_HEIGHT / 2;

    epd.drawText(cx - 60, cy - 10, "No notes yet.", 16, false, 0x0000);
    epd.drawText(cx - 90, cy + 14,
                 "Press MENU to create your first note.", 12, false, 0x0000);
}

void NotesApp::_drawDeleteDialog(const String& title) {
    EPDDisplay& epd = EPDDisplay::instance();
    auto& raw = epd.canvas();

    epd.clear();
    do {
        

        // Dialog box
        int16_t dx = 40, dy = 80, dw = EPD_WIDTH - 80, dh = 140;
        epd.canvas().fillRect(dx, dy, dw, dh, 0xFFFF);
        epd.canvas().drawRect(dx, dy, dw, dh, 0x0000);
        epd.canvas().drawRect(dx + 1, dy + 1, dw - 2, dh - 2, 0x0000);

        // Title
        epd.drawText(dx + 12, dy + 22, "Delete note?", 16, true, 0x0000);

        // Note title
        String truncTitle = title.length() > 30 ? title.substring(0, 30) + "..." : title;
        epd.drawText(dx + 12, dy + 44, truncTitle.c_str(), 12, false, 0x0000);

        // Divider
        epd.canvas().drawFastHLine(dx, dy + 54, dw, 0x0000);

        // YES button
        {
            uint16_t btnBg  = _deleteYes ? 0x0000 : 0xFFFF;
            uint16_t btnTxt = _deleteYes ? 0xFFFF : 0x0000;
            epd.canvas().fillRect(dx + 20, dy + 70, 80, 36, btnBg);
            epd.canvas().drawRect(dx + 20, dy + 70, 80, 36, 0x0000);
            epd.drawText(dx + 38, dy + 94, "YES", 16, true, btnTxt);
        }

        // NO button
        {
            uint16_t btnBg  = !_deleteYes ? 0x0000 : 0xFFFF;
            uint16_t btnTxt = !_deleteYes ? 0xFFFF : 0x0000;
            epd.canvas().fillRect(dx + 120, dy + 70, 80, 36, btnBg);
            epd.canvas().drawRect(dx + 120, dy + 70, 80, 36, 0x0000);
            epd.drawText(dx + 141, dy + 94, "NO", 16, true, btnTxt);
        }

        // Hint
        epd.drawText(dx + 12, dy + 124,
                     "UP/DN: choose  SEL: confirm  BACK: cancel",
                     12, false, 0x0000);
    epd.update();
}

// ── Private: static helpers ────────────────────────────────────

String NotesApp::_makeFilename() {
    // Use millis() as a pseudo-timestamp in the filename.
    // Format: YYYYMMDD_HHMMSS.txt  (all digits from millis, zero-padded)
    uint32_t ms = millis();
    uint32_t secs = ms / 1000;

    // Decompose seconds into H/M/S (wrapping)
    uint32_t ss = secs % 60;
    uint32_t mm = (secs / 60) % 60;
    uint32_t hh = (secs / 3600) % 24;
    uint32_t dd = (secs / 86400);

    // Simple pseudo-date starting from 2025-01-01
    // This mirrors the approach used in ReadingStats.
    uint32_t year = 2025 + dd / 365;
    uint32_t doy  = dd % 365;
    // Rough month/day (good enough for unique filenames)
    static const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t month = 1;
    for (int m = 0; m < 12; ++m) {
        if (doy < daysInMonth[m]) { month = m + 1; break; }
        doy -= daysInMonth[m];
    }
    uint32_t day = doy + 1;

    char buf[32];
    snprintf(buf, sizeof(buf), "%04lu%02lu%02lu_%02lu%02lu%02lu.txt",
             (unsigned long)year,  (unsigned long)month, (unsigned long)day,
             (unsigned long)hh,    (unsigned long)mm,    (unsigned long)ss);
    return String(buf);
}

bool NotesApp::_readMeta(const String& path, NoteEntry& out) {
    String content;
    if (!FileManager::instance().readFile(path.c_str(), content)) return false;
    if (content.isEmpty()) {
        out.title   = "(empty)";
        out.preview = "";
        return true;
    }

    // Title = first line (up to NOTES_TITLE_LEN)
    int newline = content.indexOf('\n');
    if (newline < 0) newline = static_cast<int>(content.length());
    out.title = content.substring(0, min(newline, (int)NOTES_TITLE_LEN));

    // Preview = up to 80 chars from after the first newline
    int previewStart = newline + 1;
    if (previewStart < static_cast<int>(content.length())) {
        int previewEnd = min(previewStart + 80,
                             static_cast<int>(content.length()));
        out.preview = content.substring(previewStart, previewEnd);
        // Flatten newlines in preview
        out.preview.replace('\n', ' ');
    }

    return true;
}
