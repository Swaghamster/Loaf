#pragma once

// ============================================================
// Dictionary
// Offline word lookup loaded from SD at /loaf/dict/dict.json.
//
// dict.json format:
//   { "word": "definition", "another": "its definition", ... }
//
// If the file does not exist a user-visible "not installed"
// message is shown instead of a definition.
// ============================================================

#include <Arduino.h>
#include <vector>

class Dictionary {
public:
    // Singleton access
    static Dictionary& instance();

    // Prevent copy / move
    Dictionary(const Dictionary&)            = delete;
    Dictionary& operator=(const Dictionary&) = delete;
    Dictionary(Dictionary&&)                 = delete;
    Dictionary& operator=(Dictionary&&)      = delete;

    // ---------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------

    /// Load dict.json from SD into memory.
    /// Safe to call multiple times; re-loads only if not yet loaded.
    void init();

    /// Return true if the dictionary was loaded successfully.
    bool isLoaded() const { return _loaded; }

    // ---------------------------------------------------------
    // Lookup
    // ---------------------------------------------------------

    /// Look up word (normalised: lowercase, stripped punctuation).
    /// Tries exact match first, then a stemmed match.
    /// Returns true and fills definition if found, false otherwise.
    bool lookup(const String& word, String& definition);

    // ---------------------------------------------------------
    // Display
    // ---------------------------------------------------------

    /// Draw a popup panel on the EPD showing word + definition.
    /// Panel: white filled rect with black border, word in large
    /// bold at top, definition word-wrapped below.
    void renderLookup(const String& word);

    // ---------------------------------------------------------
    // Stemmer (public so callers can use it independently)
    // ---------------------------------------------------------

    /// Simple English suffix stripper.
    /// Handles: -ing, -ed, -er, -est, -s, -es, -ly, -ness, -ment,
    ///          -tion, -ation, -able, -ible, -ful, -less, -ive.
    /// Applies rules conservatively to avoid over-stemming short roots.
    String stem(const String& word);

private:
    Dictionary() = default;

    // ---------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------

    /// Return word lowercased with leading/trailing punctuation removed.
    String _normalise(const String& word);

    /// Strip a specific suffix from word if it ends with it and
    /// the resulting root is at least minRootLen characters long.
    /// Returns the stripped word, or the original if not applicable.
    String _stripSuffix(const String& word,
                        const char*   suffix,
                        int           minRootLen);

    /// Return true if ch is ASCII punctuation to be stripped.
    static bool _isPunct(char ch);

    // ---------------------------------------------------------
    // State
    // ---------------------------------------------------------

    // The dictionary is stored as parallel vectors rather than
    // std::map to avoid the overhead of tree nodes on a
    // memory-constrained ESP32.  Linear search is acceptable for
    // dictionaries up to a few thousand entries.
    std::vector<String> _keys;        ///< lowercase keys
    std::vector<String> _definitions; ///< corresponding definitions

    bool _loaded     = false;
    bool _initCalled = false;

    static constexpr const char* DICT_PATH = "/loaf/dict/dict.json";
    static constexpr const char* DICT_DIR  = "/loaf/dict";
};
