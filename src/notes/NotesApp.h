#pragma once

// ============================================================
// NotesApp.h
// Loaf Firmware — Notes list browser
//
// Top-level screen for browsing, creating, and deleting notes.
// Notes are stored as plain text files on SD under SD_NOTES_DIR
// (/notes).  Filenames use a pseudo-timestamp derived from
// millis() to avoid requiring an RTC.
//
// Button mapping (logical indices from UIManager.h):
//   BTN_UP   (0) — scroll list up
//   BTN_DOWN (1) — scroll list down
//   BTN_SELECT (2) — open highlighted note (long-press to delete)
//   BTN_BACK (3) / BTN_MENU — create new note
// ============================================================

#include <Arduino.h>
#include <vector>

// ── Note metadata ─────────────────────────────────────────────
struct NoteEntry {
    String filename;    ///< Bare filename (no dir), e.g. "20250101_000123.txt"
    String title;       ///< First line of the note (up to NOTES_TITLE_LEN chars)
    String preview;     ///< First ~80 chars of body after the title
    size_t size;        ///< File size in bytes
};

// ─────────────────────────────────────────────────────────────
// NotesApp
// ─────────────────────────────────────────────────────────────
class NotesApp {
public:
    // Singleton accessor
    static NotesApp& instance();

    // Prevent copy / move
    NotesApp(const NotesApp&)            = delete;
    NotesApp& operator=(const NotesApp&) = delete;
    NotesApp(NotesApp&&)                 = delete;
    NotesApp& operator=(NotesApp&&)      = delete;

    // ── Lifecycle ─────────────────────────────────────────────

    /// Must be called once (from UIManager or setup()) before use.
    /// Scans SD for existing notes and builds the note list.
    void init();

    // ── Rendering ─────────────────────────────────────────────

    /// Draw the note list onto the EPD.
    /// Shows a scrollable list; each row: title (bold) + preview + size.
    /// Also redraws when _dirty is set by handleButton / refreshList.
    void render();

    // ── Input ─────────────────────────────────────────────────

    /// Route a button press into the Notes app.
    /// btn      — logical button index (BTN_UP / BTN_DOWN / BTN_SELECT / BTN_BACK)
    /// longPress — true if the button was held > BTN_LONG_PRESS_MS
    void handleButton(uint8_t btn, bool longPress);

    // ── Note operations ───────────────────────────────────────

    /// Open NoteEditor with an empty buffer (new note).
    void createNote();

    /// Load the named file from SD and open it in NoteEditor.
    void openNote(const String& filename);

    /// Show a confirmation dialog; if confirmed, delete the file from SD
    /// and refresh the list.
    void deleteNote(const String& filename);

    /// Re-scan SD_NOTES_DIR and rebuild _notes.
    void refreshList();

    // ── State helpers ─────────────────────────────────────────

    /// Mark the list as needing a redraw (e.g. after returning from editor).
    void markDirty() { _dirty = true; }

    /// Number of notes currently in the list.
    int  noteCount() const { return static_cast<int>(_notes.size()); }

private:
    NotesApp() = default;

    // ── Rendering helpers ─────────────────────────────────────

    /// Draw a single note row at pixel y, highlighted if selected.
    void _drawNoteRow(int16_t y, const NoteEntry& entry, bool selected);

    /// Draw "No notes" placeholder when list is empty.
    void _drawEmpty();

    /// Draw the status / header bar at the top.
    void _drawHeader();

    /// Draw a full-screen "Delete [title]? YES / NO" dialog.
    void _drawDeleteDialog(const String& title);

    // ── Internal helpers ──────────────────────────────────────

    /// Build a new filename from millis().
    static String _makeFilename();

    /// Read first line + preview from a note file without loading all content.
    static bool _readMeta(const String& path, NoteEntry& out);

    // ── State ─────────────────────────────────────────────────

    std::vector<NoteEntry> _notes;

    int  _selectedIdx  = 0;   ///< Currently highlighted list row
    int  _scrollOffset = 0;   ///< First visible row index

    bool _dirty        = true; ///< Needs a full redraw
    bool _initialised  = false;

    // Delete-confirmation dialog state
    bool   _showDeleteConfirm = false;
    String _pendingDeleteFile;
    bool   _deleteYes         = true;  ///< Which option is highlighted

    // Layout constants
    static constexpr int16_t HEADER_H       = 28;   ///< Height of header bar (px)
    static constexpr int16_t ROW_H          = 52;   ///< Height of each note row (px)
    static constexpr int16_t ROW_MARGIN_X   = 10;   ///< Left/right text margin in row
    static constexpr int16_t FOOTER_H       = 20;   ///< Bottom hint bar
    static constexpr int     VISIBLE_ROWS   = 4;    ///< Rows visible on 300px panel
};
