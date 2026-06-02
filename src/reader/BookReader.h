#pragma once

#include <Arduino.h>
#include <vector>
#include "EPUBParser.h"
#include "TextRenderer.h"

// ============================================================
// BookReader — singleton that ties EPUBParser + TextRenderer
// together and manages per-book reading state.
//
// Caller flow:
//   BookReader::instance().openBook("/books/MyBook");
//   // ... in render loop:
//   BookReader::instance().render();        // draws into current framebuffer
//   BookReader::instance().handleButton(btn, longPress);
//   // ... when leaving reader screen:
//   BookReader::instance().saveProgress();
// ============================================================

class BookReader {
public:
    static BookReader& instance();

    void init();

    // Open a book by its full SD path (e.g. "/books/MyBook").
    // Loads saved reading position automatically.
    // Returns false if the OPF cannot be found or parsed.
    bool openBook(const char* bookDir);

    void close();
    void saveProgress() const;

    bool isOpen() const { return _open; }

    // Draw current page into the framebuffer.
    // Assumes UIManager has already called epd.clear() and drawStatusBar().
    // Does NOT call epd.update() — UIManager handles that.
    void render();

    // Handle a button event.  Returns true if the page changed (caller should
    // set dirty flag so UIManager re-renders).
    bool handleButton(uint8_t btn, bool longPress);

    // Accessors for UIManager to record/display reading progress
    const String& bookDir()         const { return _bookDir; }
    const String& title()           const { return _meta.title; }
    int           chapterIdx()      const { return _chapter; }
    int           totalChapters()   const { return _parser.chapterCount(); }
    int           pageIdx()         const { return _page; }
    int           pagesInChapter()  const { return (int)_pages.size(); }

    // Bare directory name (last path component), used by CoverLoader and
    // UIManager::recordRecentBook for the filename field.
    String bareDir() const;

private:
    BookReader() = default;

    bool   _loadChapter(int idx);
    String _progressPath() const;
    void   _loadProgress(int& outChapter, int& outPage) const;

    EPUBParser   _parser;
    TextRenderer _renderer;

    String   _bookDir;
    BookMeta _meta;
    bool     _open = false;

    int     _chapter = 0;
    int     _page    = 0;

    Chapter           _curChapter;
    std::vector<Page> _pages;
};
