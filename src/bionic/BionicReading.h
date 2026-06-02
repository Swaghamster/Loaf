#pragma once

// ============================================================
// BionicReading
// Splits words into bold-prefix + normal-suffix pairs for
// bionic/focus reading.  UTF-8 aware: multi-byte codepoints
// count as a single character when computing prefix length.
//
// BionicMode is declared in include/config.h as:
//   enum class BionicMode : uint8_t { BIONIC_OFF, BIONIC_NORMAL, BIONIC_SUBTLE }
// ============================================================

#include <Arduino.h>
#include <vector>
#include "../../include/config.h"

struct BionicWord {
    String prefix;  ///< Characters to render bold
    String suffix;  ///< Characters to render normal
};

class BionicReading {
public:
    // ---------------------------------------------------------
    // Core splitting
    // ---------------------------------------------------------

    /// Split a single word into bold prefix + normal suffix.
    /// Trailing punctuation is always placed in the suffix.
    /// UTF-8 multi-byte sequences count as one character.
    static BionicWord splitWord(const String& word, BionicMode mode);

    /// Process an entire block of text, splitting on whitespace.
    /// Each BionicWord.prefix and .suffix are ready to render.
    static std::vector<BionicWord> processText(const String& text, BionicMode mode);

    /// Render text to a simple HTML-like markup string, e.g.
    ///   "<b>He</b>llo <b>wor</b>ld"
    /// Useful for debugging and reference rendering.
    static String toBionicMarkup(const String& text, BionicMode mode);

    // ---------------------------------------------------------
    // Utility
    // ---------------------------------------------------------

    /// Return the number of UTF-8 characters that should be bold
    /// given the total character count of the word and the mode.
    static int prefixLength(int wordLen, BionicMode mode);

private:
    // Return the number of UTF-8 characters in a String.
    static int _utf8CharCount(const String& s);

    // Return the byte offset just past the n-th UTF-8 character.
    // Returns s.length() if n >= char count.
    static int _utf8ByteOffset(const String& s, int charCount);

    // Return true if c (0-255) is a common English punctuation mark
    // that should be stripped from the end of a word before bolding.
    static bool _isPunct(char c);

    // Split raw token into (letters, trailingPunct) so that
    // punctuation at the end (.,!?;:'"…) stays in the suffix.
    static void _splitTrailingPunct(const String& token,
                                    String& wordPart,
                                    String& punctPart);
};
