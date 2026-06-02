#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>

#include "../../include/config.h"

// ─────────────────────────────────────────────────────────────────────────────
// FontManager
//
// Owns all font registrations for Loaf.  EPDDisplay calls FontManager to
// resolve a (FontID, size, bold) triple into a GFXfont pointer.
//
// Built-in fonts (always available — shipped with Adafruit GFX Library):
//   FONT_SANS   — FreeSans 9/12/18/24pt + bold variants
//   FONT_SERIF  — FreeSerif 9/12/18/24pt
//   FONT_MONO   — FreeMono 9/12/18/24pt
//
// Optional fonts (require running scripts/generate_fonts.sh first):
//   FONT_SHARE_TECH — Share Tech Mono (Mr. Robot / hacker aesthetic)
//   FONT_ORBITRON   — Orbitron (Blade Runner / cyberpunk aesthetic)
//
// If an optional font's bitmap header has not been generated the manager
// transparently falls back to the closest built-in equivalent.
// ─────────────────────────────────────────────────────────────────────────────

// Built-in GFX font headers (always present via Adafruit GFX dependency)
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerif24pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono24pt7b.h>

// Optional custom fonts — conditionally compiled when headers are present.
// Run scripts/generate_fonts.sh to produce these files.
#if __has_include("ShareTechMono_9pt.h")
#  include "ShareTechMono_9pt.h"
#  include "ShareTechMono_12pt.h"
#  include "ShareTechMono_18pt.h"
#  include "ShareTechMono_24pt.h"
#  define LOAF_HAS_SHARE_TECH 1
#else
#  define LOAF_HAS_SHARE_TECH 0
#endif

#if __has_include("Orbitron_9pt.h")
#  include "Orbitron_9pt.h"
#  include "Orbitron_12pt.h"
#  include "Orbitron_18pt.h"
#  include "Orbitron_24pt.h"
#  define LOAF_HAS_ORBITRON 1
#else
#  define LOAF_HAS_ORBITRON 0
#endif

// ── Font descriptor ───────────────────────────────────────────────────────────

struct LoafFontEntry {
    FontID          id;
    const char*     name;           // human-readable (shown in Settings)
    bool            isMonospace;
    bool            isAvailable;    // false = fall back to sans/mono
    const GFXfont*  pt9;
    const GFXfont*  pt12;
    const GFXfont*  pt18;
    const GFXfont*  pt24;
    const GFXfont*  pt9Bold;        // nullptr if no bold variant
    const GFXfont*  pt12Bold;
    const GFXfont*  pt18Bold;
    const GFXfont*  pt24Bold;
};

// ── FontManager singleton ─────────────────────────────────────────────────────

class FontManager {
public:
    static FontManager& instance();

    // Resolve (FontID, logical size in px, bold) → GFXfont*.
    // Sizes: 12→9pt, 16→12pt, 20→18pt, 24→24pt.
    // Returns a built-in font if the requested one is unavailable.
    const GFXfont* resolve(FontID id, uint8_t logicalSize, bool bold) const;

    // List of all fonts with their metadata (for the Settings screen).
    const LoafFontEntry* entries() const { return _fonts; }
    uint8_t              count()   const { return _count; }

    // Is a given font installed (bitmap header generated)?
    bool isAvailable(FontID id) const;

    // Human-readable name
    const char* name(FontID id) const;

private:
    FontManager();

    static constexpr uint8_t MAX_FONTS = FONT_COUNT;
    LoafFontEntry _fonts[MAX_FONTS];
    uint8_t       _count = 0;

    void _register(const LoafFontEntry& e);
    const LoafFontEntry* _find(FontID id) const;
};
