#include "FontManager.h"

// ── singleton ─────────────────────────────────────────────────────────────────

FontManager& FontManager::instance() {
    static FontManager inst;
    return inst;
}

// ── constructor — register all fonts ─────────────────────────────────────────

FontManager::FontManager() {
    // ── Sans-serif (default) ─────────────────────────────────────────────────
    _register({
        FontID::SANS, "Sans", /*mono*/false, /*available*/true,
        &FreeSans9pt7b,  &FreeSans12pt7b,  &FreeSans18pt7b,  &FreeSans24pt7b,
        &FreeSansBold9pt7b, &FreeSansBold12pt7b, &FreeSansBold18pt7b, &FreeSansBold24pt7b,
    });

    // ── Serif ────────────────────────────────────────────────────────────────
    _register({
        FontID::SERIF, "Serif", false, true,
        &FreeSerif9pt7b, &FreeSerif12pt7b, &FreeSerif18pt7b, &FreeSerif24pt7b,
        // Adafruit GFX doesn't ship a bold Serif; fall back to regular.
        &FreeSerif9pt7b, &FreeSerif12pt7b, &FreeSerif18pt7b, &FreeSerif24pt7b,
    });

    // ── Monospace ────────────────────────────────────────────────────────────
    _register({
        FontID::MONO, "Mono", true, true,
        &FreeMono9pt7b, &FreeMono12pt7b, &FreeMono18pt7b, &FreeMono24pt7b,
        &FreeMono9pt7b, &FreeMono12pt7b, &FreeMono18pt7b, &FreeMono24pt7b,
    });

    // ── Share Tech Mono (Mr. Robot / hacker terminal) ─────────────────────────
    // Monospace with sharp rectangular strokes — looks great on e-ink at small
    // sizes.  Falls back to FreeMono if the bitmap header hasn't been generated.
#if LOAF_HAS_SHARE_TECH
    _register({
        FontID::SHARE_TECH, "Mr. Robot", true, true,
        &ShareTechMono9pt7b,  &ShareTechMono12pt7b,  &ShareTechMono18pt7b,  &ShareTechMono24pt7b,
        &ShareTechMono9pt7b,  &ShareTechMono12pt7b,  &ShareTechMono18pt7b,  &ShareTechMono24pt7b,
    });
#else
    _register({
        FontID::SHARE_TECH, "Mr. Robot (install font)", true, false,
        &FreeMono9pt7b, &FreeMono12pt7b, &FreeMono18pt7b, &FreeMono24pt7b,
        &FreeMono9pt7b, &FreeMono12pt7b, &FreeMono18pt7b, &FreeMono24pt7b,
    });
#endif

    // ── Orbitron (Blade Runner / cyberpunk) ──────────────────────────────────
    // Geometric, futuristic uppercase-heavy sans — the closest open-source match
    // to Blade Runner's iconic title card typography.  Falls back to FreeSans.
#if LOAF_HAS_ORBITRON
    _register({
        FontID::ORBITRON, "Blade Runner", false, true,
        &Orbitron9pt7b,  &Orbitron12pt7b,  &Orbitron18pt7b,  &Orbitron24pt7b,
        &Orbitron9pt7b,  &Orbitron12pt7b,  &Orbitron18pt7b,  &Orbitron24pt7b,
    });
#else
    _register({
        FontID::ORBITRON, "Blade Runner (install font)", false, false,
        &FreeSans9pt7b, &FreeSans12pt7b, &FreeSans18pt7b, &FreeSans24pt7b,
        &FreeSansBold9pt7b, &FreeSansBold12pt7b, &FreeSansBold18pt7b, &FreeSansBold24pt7b,
    });
#endif
}

// ── helpers ───────────────────────────────────────────────────────────────────

void FontManager::_register(const LoafFontEntry& e) {
    if (_count < MAX_FONTS) _fonts[_count++] = e;
}

const LoafFontEntry* FontManager::_find(FontID id) const {
    for (uint8_t i = 0; i < _count; ++i) {
        if (_fonts[i].id == id) return &_fonts[i];
    }
    return &_fonts[0];  // default to SANS
}

// ── public API ────────────────────────────────────────────────────────────────

const GFXfont* FontManager::resolve(FontID id, uint8_t logicalSize, bool bold) const {
    const LoafFontEntry* e = _find(id);

    // Map logical pixel sizes to point sizes:
    //   12 px → 9pt,  16 px → 12pt,  20 px → 18pt,  24 px → 24pt
    const GFXfont* base = nullptr;
    const GFXfont* bld  = nullptr;

    if (logicalSize <= 12) {
        base = e->pt9;   bld = e->pt9Bold;
    } else if (logicalSize <= 16) {
        base = e->pt12;  bld = e->pt12Bold;
    } else if (logicalSize <= 20) {
        base = e->pt18;  bld = e->pt18Bold;
    } else {
        base = e->pt24;  bld = e->pt24Bold;
    }

    return (bold && bld) ? bld : base;
}

bool FontManager::isAvailable(FontID id) const {
    const LoafFontEntry* e = _find(id);
    return e->isAvailable;
}

const char* FontManager::name(FontID id) const {
    return _find(id)->name;
}
