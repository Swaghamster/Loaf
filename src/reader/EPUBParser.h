#pragma once

// ============================================================
// EPUBParser
//
// EPUB books are stored on the SD card as pre-extracted
// directory trees.  The user is expected to unzip the .epub
// file into /books/<bookname>/ before transferring to the card.
//
// Directory layout expected on SD:
//
//   /books/<bookname>/
//     content.opf              (or META-INF/container.xml pointing to it)
//     META-INF/container.xml   (optional — used to find the OPF path)
//     *.xhtml / *.html         (chapter files listed in OPF spine)
//     *.jpg / *.png            (cover image, if any)
//
// The parser:
//   1. Reads META-INF/container.xml (if present) to locate the OPF.
//   2. Parses the OPF for <metadata> (title, author, cover) and
//      the <spine> / <manifest> to build a chapter file list.
//   3. Lazily strips HTML/XML tags from each chapter XHTML file
//      on demand via a simple state-machine plain-text extractor.
// ============================================================

#include <Arduino.h>
#include <vector>

// ── Public data structures ────────────────────────────────────────────────

/// Bibliographic metadata extracted from the OPF <metadata> block.
struct BookMeta {
    String title;
    String author;
    String coverPath;    ///< Absolute SD path to cover image, or empty
    int    chapterCount; ///< Number of chapters in spine order
};

/// A single chapter's content after HTML-tag stripping.
struct Chapter {
    String title;    ///< From <title> tag or fallback to filename
    String content;  ///< Plain text (newlines preserved, tags removed)
};

// ─────────────────────────────────────────────────────────────────────────────
// EPUBParser
// ─────────────────────────────────────────────────────────────────────────────
class EPUBParser {
public:
    EPUBParser() = default;
    ~EPUBParser() { close(); }

    // Non-copyable
    EPUBParser(const EPUBParser&)            = delete;
    EPUBParser& operator=(const EPUBParser&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    /// Open the extracted EPUB at bookDir (e.g. "/books/MyBook").
    /// Returns true if the OPF was found and parsed successfully.
    bool open(const char* bookDir);

    /// Return metadata struct (valid after a successful open()).
    BookMeta getMeta() const;

    /// Number of spine items (chapters).
    int chapterCount() const;

    /// Load chapter idx (0-based) and strip HTML into out.content.
    /// Returns false if idx is out of range or file cannot be read.
    bool loadChapter(int idx, Chapter& out);

    /// Release all state.  Safe to call multiple times.
    void close();

private:
    // ── OPF parsing ───────────────────────────────────────────────────────

    /// Locate the OPF path via META-INF/container.xml, falling back
    /// to scanning for any *.opf file in the book root.
    String _findOpfPath() const;

    /// Parse the OPF XML, filling _meta and _spineFiles.
    bool _parseOpf(const String& opfPath);

    // ── XML/HTML helpers ──────────────────────────────────────────────────

    /// Extract the value of attribute attrName from a tag string.
    /// tagStr should be the raw content inside < >, e.g. "meta name='title' content='Foo'".
    static String _attrValue(const String& tagStr, const char* attrName);

    /// Return the text between the first matching open/close tag pair in xml.
    static String _tagContent(const String& xml, const char* tag);

    /// Strip all XML/HTML tags from src using a state machine.
    /// Also collapses whitespace runs and converts &amp; &lt; &gt; &quot; entities.
    static String _stripTags(const String& src);

    /// Decode the five standard XML character entities in place.
    static String _decodeEntities(const String& src);

    /// Resolve a path that may be relative to opfDir.
    static String _resolvePath(const String& opfDir, const String& href);

    // ── State ─────────────────────────────────────────────────────────────
    String              _bookDir;       ///< absolute SD path, e.g. "/books/Foo"
    String              _opfDir;        ///< directory containing the OPF file
    BookMeta            _meta;
    std::vector<String> _spineFiles;    ///< absolute SD paths in spine order
    bool                _open = false;
};
