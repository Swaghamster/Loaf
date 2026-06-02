#!/usr/bin/env bash
# =============================================================================
# Loaf — generate_fonts.sh
#
# Downloads Share Tech Mono (Mr. Robot) and Orbitron (Blade Runner) from
# Google Fonts and converts them to Adafruit GFX PROGMEM bitmap headers
# using the `fontconvert` tool from the Adafruit-GFX-Library.
#
# Output: src/fonts/ShareTechMono_*.h  and  src/fonts/Orbitron_*.h
#
# Prerequisites:
#   - freetype2 dev headers  (apt: libfreetype6-dev  |  brew: freetype)
#   - fontconvert             (built below from Adafruit source)
#   - curl
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
FONT_OUT="$REPO_ROOT/src/fonts"
TMP_DIR="$(mktemp -d)"

cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

echo "=== Loaf font generator ==="
echo "Output directory: $FONT_OUT"

# ── Build fontconvert ─────────────────────────────────────────────────────────

FONTCONVERT="$TMP_DIR/fontconvert"

if ! command -v fontconvert &>/dev/null; then
    echo "[1/4] Building fontconvert from Adafruit-GFX-Library..."
    ADAFRUIT_DIR="$TMP_DIR/Adafruit-GFX-Library"
    git clone --depth 1 https://github.com/adafruit/Adafruit-GFX-Library.git "$ADAFRUIT_DIR"
    cc -o "$FONTCONVERT" "$ADAFRUIT_DIR/fontconvert/fontconvert.c" \
        $(pkg-config --cflags --libs freetype2) 2>/dev/null \
    || {
        echo "ERROR: could not build fontconvert. Install libfreetype6-dev and retry."
        exit 1
    }
else
    FONTCONVERT="$(command -v fontconvert)"
    echo "[1/4] Using existing fontconvert at $FONTCONVERT"
fi

# ── Download fonts ────────────────────────────────────────────────────────────

echo "[2/4] Downloading fonts from Google Fonts..."

# Share Tech Mono — OFL license — hacker/terminal aesthetic (Mr. Robot)
SHARE_TECH_URL="https://fonts.gstatic.com/s/sharetechmono/v15/J7aHnp1uDWRBEqV98dVQztYldFcLowEF.ttf"
SHARE_TECH_TTF="$TMP_DIR/ShareTechMono.ttf"
curl -fsSL -o "$SHARE_TECH_TTF" "$SHARE_TECH_URL"

# Orbitron — OFL license — geometric futuristic (Blade Runner)
ORBITRON_URL="https://fonts.gstatic.com/s/orbitron/v29/yMJMMIlzdpvBhQQL_SC3X9yhF25-T1nyGy6Spm0T.ttf"
ORBITRON_TTF="$TMP_DIR/Orbitron.ttf"
curl -fsSL -o "$ORBITRON_TTF" "$ORBITRON_URL"

echo "[3/4] Converting to Adafruit GFX bitmap format..."

# ── Convert: Share Tech Mono ──────────────────────────────────────────────────
# 9pt, 12pt, 18pt, 24pt — all same file (no bold variant; mono is already
# visually heavier than proportional fonts).

for size_px in 9 12 18 24; do
    out="$FONT_OUT/ShareTechMono_${size_px}pt.h"
    "$FONTCONVERT" "$SHARE_TECH_TTF" "$size_px" > "$out"
    # Adafruit fontconvert names the struct after the file stem + pt size.
    # Rename to our convention: ShareTechMono${size_px}pt7b
    sed -i "s/const GFXfont [A-Za-z0-9_]*/const GFXfont ShareTechMono${size_px}pt7b/g" "$out" 2>/dev/null || true
    echo "  → $out"
done

# ── Convert: Orbitron ─────────────────────────────────────────────────────────

for size_px in 9 12 18 24; do
    out="$FONT_OUT/Orbitron_${size_px}pt.h"
    "$FONTCONVERT" "$ORBITRON_TTF" "$size_px" > "$out"
    sed -i "s/const GFXfont [A-Za-z0-9_]*/const GFXfont Orbitron${size_px}pt7b/g" "$out" 2>/dev/null || true
    echo "  → $out"
done

echo "[4/4] Updating header guards..."

# Ensure each generated header has a pragma once so double-inclusion is safe.
for f in "$FONT_OUT"/ShareTechMono_*.h "$FONT_OUT"/Orbitron_*.h; do
    if ! grep -q "pragma once" "$f"; then
        { echo "#pragma once"; cat "$f"; } > "${f}.tmp" && mv "${f}.tmp" "$f"
    fi
done

cat <<'EOF'

=== Done! ===

The following fonts are now available in src/fonts/:
  "Mr. Robot"     → ShareTechMono_9/12/18/24pt.h
  "Blade Runner"  → Orbitron_9/12/18/24pt.h

Rebuild the firmware (pio run) and both fonts will appear in
Settings → Reader → Font.

Font credits:
  Share Tech Mono — Carrois Type Design, OFL 1.1
  Orbitron        — Matt McInerney, OFL 1.1
EOF
