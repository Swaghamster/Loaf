// ============================================================
// BionicReading.cpp
// ============================================================

#include "BionicReading.h"
#include <cmath>   // std::ceil

// ------------------------------------------------------------
// Public: prefixLength
// ------------------------------------------------------------
int BionicReading::prefixLength(int wordLen, BionicMode mode) {
    if (wordLen <= 0) return 0;

    switch (mode) {
        case BionicMode::BIONIC_OFF:
            return 0;

        case BionicMode::BIONIC_NORMAL:
            if (wordLen <= 2) return wordLen;          // bold entire short word
            return static_cast<int>(std::ceil(wordLen / 2.0f));

        case BionicMode::BIONIC_SUBTLE:
            if (wordLen <= 2) return 1;                // bold 1 char of short word
            {
                int n = static_cast<int>(std::ceil(wordLen / 3.0f));
                return (n < 1) ? 1 : n;                // minimum 1
            }

        default:
            return 0;
    }
}

// ------------------------------------------------------------
// Public: splitWord
// ------------------------------------------------------------
BionicWord BionicReading::splitWord(const String& word, BionicMode mode) {
    BionicWord result;

    if (mode == BionicMode::BIONIC_OFF || word.length() == 0) {
        result.suffix = word;
        return result;
    }

    // Separate trailing punctuation so it always lands in suffix
    String wordPart, punctPart;
    _splitTrailingPunct(word, wordPart, punctPart);

    int charCount = _utf8CharCount(wordPart);
    int n = prefixLength(charCount, mode);

    int byteOffset = _utf8ByteOffset(wordPart, n);
    result.prefix = wordPart.substring(0, byteOffset);
    result.suffix = wordPart.substring(byteOffset) + punctPart;

    return result;
}

// ------------------------------------------------------------
// Public: processText
// ------------------------------------------------------------
std::vector<BionicWord> BionicReading::processText(const String& text, BionicMode mode) {
    std::vector<BionicWord> words;

    int len = static_cast<int>(text.length());
    int i = 0;

    while (i < len) {
        // Skip whitespace — record whitespace tokens as empty-prefix words
        // so the caller can reconstruct spacing if needed.
        if (isspace((unsigned char)text[i])) {
            // Collect run of whitespace as a single "space" token
            int start = i;
            while (i < len && isspace((unsigned char)text[i])) ++i;
            BionicWord sp;
            sp.suffix = text.substring(start, i);
            words.push_back(sp);
            continue;
        }

        // Collect non-whitespace token
        int start = i;
        while (i < len && !isspace((unsigned char)text[i])) ++i;
        String token = text.substring(start, i);
        words.push_back(splitWord(token, mode));
    }

    return words;
}

// ------------------------------------------------------------
// Public: toBionicMarkup
// ------------------------------------------------------------
String BionicReading::toBionicMarkup(const String& text, BionicMode mode) {
    if (mode == BionicMode::BIONIC_OFF) return text;

    std::vector<BionicWord> words = processText(text, mode);

    String out;
    out.reserve(text.length() * 2); // rough estimate including tags

    for (const BionicWord& bw : words) {
        if (bw.prefix.length() == 0) {
            // Whitespace token or off-mode — just append suffix as-is
            out += bw.suffix;
        } else {
            out += "<b>";
            out += bw.prefix;
            out += "</b>";
            out += bw.suffix;
        }
    }

    return out;
}

// ------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------

bool BionicReading::_isPunct(char c) {
    // Common trailing punctuation to preserve in suffix
    switch (c) {
        case '.': case ',': case '!': case '?': case ';': case ':':
        case '"': case '\'': case ')': case ']': case '}':
        case '\xe2': // start of UTF-8 ellipsis (…) — handled by multi-byte check below
            return true;
        default:
            return false;
    }
}

void BionicReading::_splitTrailingPunct(const String& token,
                                         String& wordPart,
                                         String& punctPart) {
    int end = static_cast<int>(token.length());

    // Walk backwards over ASCII punctuation characters.
    // We stop at the first non-punctuation byte (working byte-by-byte
    // because punctuation here is all single-byte ASCII; multi-byte
    // sequences beginning with 0x80-0xFF are continuation bytes of
    // non-ASCII letters and should NOT be treated as punctuation).
    while (end > 0) {
        unsigned char ch = static_cast<unsigned char>(token[end - 1]);

        // Continuation byte of a multi-byte sequence: part of a character,
        // not trailing punctuation — stop.
        if (ch >= 0x80) break;

        if (_isPunct(static_cast<char>(ch))) {
            --end;
        } else {
            break;
        }
    }

    wordPart  = token.substring(0, end);
    punctPart = token.substring(end);
}

int BionicReading::_utf8CharCount(const String& s) {
    int count = 0;
    int i = 0;
    int len = static_cast<int>(s.length());

    while (i < len) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            // Single-byte ASCII
            ++i;
        } else if ((b & 0xE0) == 0xC0) {
            // 2-byte sequence
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            // 3-byte sequence
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            // 4-byte sequence
            i += 4;
        } else {
            // Continuation or invalid byte — skip
            ++i;
        }
        ++count;
    }

    return count;
}

int BionicReading::_utf8ByteOffset(const String& s, int charCount) {
    int count = 0;
    int i = 0;
    int len = static_cast<int>(s.length());

    while (i < len && count < charCount) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            ++i;
        } else if ((b & 0xE0) == 0xC0) {
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            i += 4;
        } else {
            ++i;
        }
        ++count;
    }

    // Clamp to string length
    if (i > len) i = len;
    return i;
}
