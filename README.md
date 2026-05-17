# Loaf

A custom Xteink X4 firmware combining the best features from **CPR-vCodex** and **Shortbread**.

## Features

### Reading Experience
- **Bionic Reading (Focus Reading)** — Bold prefixes on word beginnings for guided reading (from CPR-vCodex)
  - Three modes: Off, Normal, Subtle
  - UTF-8 safe text processing
- **Dictionary** — Offline dictionary with WordNet 3.1 (~150K headwords) in-reader lookup (from Shortbread)
- **Full EPUB Support** — EPUB 2/3, KOReader sync, Calibre wireless transfer
- **Reader Customization** — Multiple fonts, font sizes, line spacing, text darkness, margins

### Reading Analytics (from CPR-vCodex)
- Reading stats and heatmaps
- Daily goal tracking and streaks
- Achievements system
- Session history and per-book statistics
- Manual reading time corrections

### Additional Features
- Games and utilities
- Bookmarks
- Flashcards (CSV-based offline decks)
- Sleep tools with directory selection
- SD card font management
- Dark mode (experimental)

## Device Support
- **Xteink X4** (primary target)
- **Xteink X3** (partial compatibility, not personally tested)

## Building

### Prerequisites
- PlatformIO Core (`pio`) or VS Code + PlatformIO IDE
- Python 3.8+
- USB-C cable for flashing ESP32-C3
- Xteink X4

### Build
```bash
git clone --recursive https://github.com/swaghamster/Loaf
cd Loaf
pio run -j 4
```

## Installation
Coming soon — check releases for firmware binaries.

## Architecture

This project combines:
- **Core**: CrossPoint Reader (upstream base)
- **Reading Analytics**: CPR-vCodex fork
- **Dictionary & UI**: Shortbread fork
- **Integration**: Custom merge of Bionic Reading + dictionary + analytics

## Credits

Built on top of:
- **CrossPoint Reader** — core EPUB engine and rendering
- **CPR-vCodex** (franssjz) — Bionic Reading, analytics, reading insights
- **Shortbread** (deepakvettickal) — dictionary, UI improvements, games
- **Biscuit** — UI architecture and design inspiration
- **CrossInk** — reader fonts and settings

## License

MIT (inherited from upstream projects)

---

**Note**: This project is not affiliated with Xteink or hardware manufacturers.
