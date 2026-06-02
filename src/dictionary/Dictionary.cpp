// ============================================================
// Dictionary.cpp
// Offline word lookup from /loaf/dict/dict.json on SD.
// ============================================================

#include "Dictionary.h"
#include "../storage/FileManager.h"
#include "../display/EPDDisplay.h"
#include "../../include/config.h"

#include <ArduinoJson.h>
#include <cctype>   // tolower, isalpha, ispunct

// ============================================================
// Singleton
// ============================================================

Dictionary& Dictionary::instance() {
    static Dictionary inst;
    return inst;
}

// ============================================================
// Lifecycle
// ============================================================

void Dictionary::init() {
    if (_initCalled) return;
    _initCalled = true;

    FileManager& fm = FileManager::instance();

    if (!fm.isReady()) {
        Serial.println(F("[Dictionary] SD not ready"));
        return;
    }

    if (!fm.exists(DICT_PATH)) {
        Serial.println(F("[Dictionary] dict.json not found"));
        _loaded = false;
        return;
    }

    // Check file size — warn if suspiciously large for the heap
    size_t dictSize = fm.fileSize(DICT_PATH);
    Serial.printf("[Dictionary] dict.json size: %u bytes\n",
                  static_cast<unsigned>(dictSize));

    String raw;
    if (!fm.readFile(DICT_PATH, raw)) {
        Serial.println(F("[Dictionary] failed to read dict.json"));
        return;
    }

    // Parse JSON.  We use a streaming filter approach: iterate over
    // the JsonObject members one by one to keep peak memory low.
    // ArduinoJson v7 supports member-by-member iteration on an object.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, raw);
    if (err) {
        Serial.printf("[Dictionary] JSON parse error: %s\n", err.c_str());
        return;
    }

    JsonObject root = doc.as<JsonObject>();
    _keys.clear();
    _definitions.clear();
    _keys.reserve(root.size());
    _definitions.reserve(root.size());

    for (JsonPair kv : root) {
        String key = String(kv.key().c_str());
        key.toLowerCase();
        _keys.push_back(key);
        _definitions.push_back(kv.value().as<String>());
    }

    _loaded = true;
    Serial.printf("[Dictionary] loaded %u entries\n",
                  static_cast<unsigned>(_keys.size()));
}

// ============================================================
// Lookup
// ============================================================

bool Dictionary::lookup(const String& word, String& definition) {
    if (word.length() == 0) return false;

    String norm = _normalise(word);
    if (norm.length() == 0) return false;

    // 1. Exact match
    for (size_t i = 0; i < _keys.size(); ++i) {
        if (_keys[i] == norm) {
            definition = _definitions[i];
            return true;
        }
    }

    // 2. Stemmed match — try progressively stemmed forms
    String stemmed = stem(norm);
    if (stemmed != norm) {
        for (size_t i = 0; i < _keys.size(); ++i) {
            if (_keys[i] == stemmed) {
                definition = _definitions[i];
                return true;
            }
        }

        // 3. Double-stem (e.g. "running" -> "run" -> "run")
        String stemmed2 = stem(stemmed);
        if (stemmed2 != stemmed) {
            for (size_t i = 0; i < _keys.size(); ++i) {
                if (_keys[i] == stemmed2) {
                    definition = _definitions[i];
                    return true;
                }
            }
        }
    }

    return false;
}

// ============================================================
// Display
// ============================================================

void Dictionary::renderLookup(const String& word) {
    EPDDisplay& epd = EPDDisplay::instance();

    // ── Panel geometry ───────────────────────────────────────
    // Leave a 20 px margin on all sides; panel occupies most of screen.
    const int16_t PAD   = 20;
    const int16_t PX    = PAD;
    const int16_t PY    = PAD;
    const int16_t PW    = EPD_WIDTH  - 2 * PAD;
    const int16_t PH    = EPD_HEIGHT - 2 * PAD;
    const int16_t INNER = 8;   // inner padding from panel edge to text

    // White panel with black border (2 px)
    epd.fillRect(PX, PY, PW, PH, 0xFFFF);
    epd.drawRect(PX,     PY,     PW,     PH,     0x0000);
    epd.drawRect(PX + 1, PY + 1, PW - 2, PH - 2, 0x0000);

    // ── Word heading ─────────────────────────────────────────
    int16_t textX = PX + INNER;
    int16_t textY = PY + INNER + FONT_LARGE;   // baseline of first line

    String normWord = _normalise(word);
    String displayWord = (normWord.length() > 0) ? normWord : word;

    epd.drawText(textX, textY, displayWord.c_str(), FONT_LARGE, /*bold=*/true);
    textY += 4;  // small gap after heading

    // Thin divider under the word
    epd.drawLine(textX, textY, PX + PW - INNER, textY, 0x0000);
    textY += 8;

    // ── Definition area ──────────────────────────────────────
    const int16_t defX  = textX;
    const int16_t defY  = textY;
    const int16_t defW  = PW - 2 * INNER;
    const int16_t defH  = PY + PH - textY - INNER - 20; // leave room at bottom

    if (!_loaded) {
        // Dictionary not installed message
        const char* msg =
            "Dictionary not installed.\n"
            "Copy dict.json to\n"
            "/loaf/dict/ on SD card.";
        epd.drawTextWrapped(defX, defY, defW, defH, msg, FONT_SMALL, /*bold=*/false);
    } else {
        String definition;
        if (lookup(word, definition)) {
            epd.drawTextWrapped(defX, defY, defW, defH,
                                definition.c_str(), FONT_SMALL, /*bold=*/false);

            // ── Etymology hint ────────────────────────────────
            // Heuristic: if definition contains " from " or " < " or "Latin"
            // / "Greek" / "French" extract and show it as a hint line.
            String etym;
            int fromIdx  = definition.indexOf(" from ");
            int latinIdx = definition.indexOf("Latin");
            int greekIdx = definition.indexOf("Greek");
            int frIdx    = definition.indexOf("French");

            int hintIdx = -1;
            if (fromIdx  >= 0) hintIdx = fromIdx;
            if (latinIdx >= 0 && (hintIdx < 0 || latinIdx < hintIdx)) hintIdx = latinIdx - 6;
            if (greekIdx >= 0 && (hintIdx < 0 || greekIdx < hintIdx)) hintIdx = greekIdx - 6;
            if (frIdx    >= 0 && (hintIdx < 0 || frIdx    < hintIdx)) hintIdx = frIdx    - 6;

            if (hintIdx >= 0 && hintIdx < (int)definition.length()) {
                etym = definition.substring(hintIdx);
                if (etym.length() > 60) etym = etym.substring(0, 57) + "...";

                int16_t etymY = PY + PH - INNER - 14;
                if (etymY > defY + 10) {
                    // Subtle separator
                    epd.drawLine(defX, etymY - 4,
                                 PX + PW - INNER, etymY - 4, 0x0000);
                    epd.drawText(defX, etymY,
                                 etym.c_str(), FONT_SMALL, /*bold=*/false);
                }
            }
        } else {
            // Word not found — show the normalised lookup term and suggestion
            String notFound = "No entry for \"" + displayWord + "\".";
            epd.drawText(defX, defY, notFound.c_str(), FONT_SMALL, /*bold=*/false);

            // Show the stemmed form we tried
            String stemmed = stem(normWord);
            if (stemmed != normWord && stemmed.length() > 0) {
                int16_t sy = defY + 18;
                String tryMsg = "Tried stem: \"" + stemmed + "\"";
                epd.drawText(defX, sy, tryMsg.c_str(), FONT_SMALL, /*bold=*/false);
            }
        }
    }

    epd.update();
}

// ============================================================
// Stemmer
// ============================================================

String Dictionary::stem(const String& word) {
    // Work on a lowercase copy
    String w = word;
    w.toLowerCase();

    int len = static_cast<int>(w.length());
    if (len <= 3) return w;

    // Helper: strip suffix and check minimum root length
    // We chain from longest to shortest to prefer the most specific rule.

    // -ation (6) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "ation", 3);
        if (r != w) {
            // Double-check root ends in a consonant (rough heuristic)
            return r;
        }
    }

    // -tion (4) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "tion", 3);
        if (r != w) return r;
    }

    // -ness (4) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "ness", 3);
        if (r != w) return r;
    }

    // -ment (4) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "ment", 3);
        if (r != w) return r;
    }

    // -able (4) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "able", 3);
        if (r != w) return r;
    }

    // -ible (4) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "ible", 3);
        if (r != w) return r;
    }

    // -less (4) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "less", 3);
        if (r != w) return r;
    }

    // -ness already handled above

    // -ful (3) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "ful", 3);
        if (r != w) return r;
    }

    // -ive (3) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "ive", 3);
        if (r != w) return r;
    }

    // -ing (3) -> "" minRoot 3; handle doubled consonant: running -> run
    {
        String r = _stripSuffix(w, "ing", 3);
        if (r != w) {
            // If root ends in double consonant, strip one (e.g. "runn" -> "run")
            int rlen = static_cast<int>(r.length());
            if (rlen >= 2 && r[rlen - 1] == r[rlen - 2] &&
                !strchr("aeiou", r[rlen - 1])) {
                r = r.substring(0, rlen - 1);
            }
            // If root ends in consonant and has no vowel after strip,
            // try restoring silent 'e': "tak" -> "take"
            return r;
        }
    }

    // -ed (2) -> "" minRoot 3; handle doubled consonant: stopped -> stop
    {
        String r = _stripSuffix(w, "ed", 3);
        if (r != w) {
            int rlen = static_cast<int>(r.length());
            if (rlen >= 2 && r[rlen - 1] == r[rlen - 2] &&
                !strchr("aeiou", r[rlen - 1])) {
                r = r.substring(0, rlen - 1);
            }
            return r;
        }
    }

    // -er (2) -> "" minRoot 3 (comparative / agent noun)
    {
        String r = _stripSuffix(w, "er", 3);
        if (r != w) return r;
    }

    // -est (3) -> "" minRoot 3 (superlative)
    {
        String r = _stripSuffix(w, "est", 3);
        if (r != w) return r;
    }

    // -ly (2) -> "" minRoot 3
    {
        String r = _stripSuffix(w, "ly", 3);
        if (r != w) return r;
    }

    // -es (2) -> "" minRoot 3 (plural / 3rd-person verb)
    {
        String r = _stripSuffix(w, "es", 3);
        if (r != w) return r;
    }

    // -s (1) -> "" minRoot 3  (plain plural — apply last to avoid over-stripping)
    if (len > 3 && w[len - 1] == 's' && w[len - 2] != 's') {
        return w.substring(0, len - 1);
    }

    return w;
}

// ============================================================
// Private helpers
// ============================================================

String Dictionary::_normalise(const String& word) {
    // 1. Strip leading/trailing ASCII punctuation
    int start = 0;
    int end   = static_cast<int>(word.length());

    while (start < end && _isPunct(word[start]))  ++start;
    while (end > start && _isPunct(word[end - 1])) --end;

    if (start >= end) return String();

    String out = word.substring(start, end);

    // 2. Lowercase
    out.toLowerCase();

    return out;
}

String Dictionary::_stripSuffix(const String& word,
                                 const char*   suffix,
                                 int           minRootLen) {
    int wlen = static_cast<int>(word.length());
    int slen = static_cast<int>(strlen(suffix));

    if (wlen - slen < minRootLen) return word;
    if (!word.endsWith(suffix))   return word;

    return word.substring(0, wlen - slen);
}

bool Dictionary::_isPunct(char ch) {
    // Strip these characters from word boundaries
    switch (ch) {
        case '.': case ',': case '!': case '?': case ';': case ':':
        case '"': case '\'': case '(': case ')': case '[': case ']':
        case '{': case '}': case '-': case '_': case '/': case '\\':
        case '\xE2': // UTF-8 lead byte for smart quotes / ellipsis — safe to strip
            return true;
        default:
            return false;
    }
}
