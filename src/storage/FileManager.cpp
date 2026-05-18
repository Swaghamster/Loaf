#include "FileManager.h"
#include "../../include/config.h"

#include <Arduino.h>
#include <cctype>    // tolower
#include <cstring>   // strlen, strncpy, strrchr

// ============================================================
// Singleton
// ============================================================

FileManager& FileManager::instance() {
    static FileManager inst;
    return inst;
}

// ============================================================
// Lifecycle
// ============================================================

bool FileManager::init() {
    if (_ready) return true;   // already initialised

    // SdSpiConfig: use the shared SPI bus at the configured speed,
    // with the dedicated chip-select pin.
    SdSpiConfig cfg(PIN_SD_CS, SHARED_SPI, SD_SPI_SPEED);

    if (!_sd.begin(cfg)) {
        Serial.println(F("[FileManager] SD init failed"));
        _ready = false;
        return false;
    }

    // Verify we can open the root — sanity-checks the filesystem.
    FsFile root;
    if (!root.open("/")) {
        Serial.println(F("[FileManager] Cannot open SD root"));
        _ready = false;
        return false;
    }
    root.close();

    // Create required application directories if they don't exist yet.
    _sd.mkdir(SD_BOOKS_DIR,    true);
    _sd.mkdir(SD_NOTES_DIR,    true);
    _sd.mkdir(SD_PROGRESS_DIR, true);

    _ready = true;
    Serial.println(F("[FileManager] SD ready"));
    return true;
}

// ============================================================
// Queries
// ============================================================

bool FileManager::exists(const char* path) {
    if (!_ready) return false;
    return _sd.exists(path);
}

size_t FileManager::fileSize(const char* path) {
    if (!_ready) return 0;

    FsFile f;
    if (!f.open(path, O_RDONLY)) return 0;
    size_t sz = static_cast<size_t>(f.size());
    f.close();
    return sz;
}

// ============================================================
// File I/O
// ============================================================

bool FileManager::readFile(const char* path, String& out) {
    if (!_ready) return false;

    FsFile f;
    if (!f.open(path, O_RDONLY)) {
        Serial.printf("[FileManager] readFile: cannot open %s\n", path);
        return false;
    }

    uint64_t sz = f.size();
    if (sz == 0) {
        out = String();
        f.close();
        return true;
    }

    // Reserve space upfront to avoid repeated heap reallocations.
    // Cap at NOTES_MAX_LENGTH as a sanity guard; larger files are
    // still read, but callers should be prepared for large strings.
    out.reserve(static_cast<unsigned int>(sz));
    out = String();

    // Stream in 512-byte chunks.
    static constexpr size_t CHUNK = 512;
    char buf[CHUNK + 1];
    int  n;

    while ((n = f.read(buf, CHUNK)) > 0) {
        buf[n] = '\0';
        out += buf;
    }

    f.close();

    if (n < 0) {
        Serial.printf("[FileManager] readFile: read error on %s\n", path);
        return false;
    }

    return true;
}

bool FileManager::writeFile(const char* path, const String& content) {
    if (!_ready) return false;

    // Ensure parent directory exists before opening the file.
    if (!_ensureParentDirs(path)) {
        Serial.printf("[FileManager] writeFile: cannot create dirs for %s\n", path);
        return false;
    }

    FsFile f;
    // O_WRONLY | O_CREAT | O_TRUNC — create or truncate.
    if (!f.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
        Serial.printf("[FileManager] writeFile: cannot open %s\n", path);
        return false;
    }

    size_t toWrite = content.length();
    size_t written = f.write(content.c_str(), toWrite);
    f.close();

    if (written != toWrite) {
        Serial.printf("[FileManager] writeFile: partial write on %s (%u/%u)\n",
                      path, written, toWrite);
        return false;
    }

    return true;
}

bool FileManager::appendFile(const char* path, const String& content) {
    if (!_ready) return false;

    if (!_ensureParentDirs(path)) {
        Serial.printf("[FileManager] appendFile: cannot create dirs for %s\n", path);
        return false;
    }

    FsFile f;
    // O_WRONLY | O_CREAT | O_APPEND
    if (!f.open(path, O_WRONLY | O_CREAT | O_APPEND)) {
        Serial.printf("[FileManager] appendFile: cannot open %s\n", path);
        return false;
    }

    size_t toWrite = content.length();
    size_t written = f.write(content.c_str(), toWrite);
    f.close();

    if (written != toWrite) {
        Serial.printf("[FileManager] appendFile: partial write on %s (%u/%u)\n",
                      path, written, toWrite);
        return false;
    }

    return true;
}

bool FileManager::deleteFile(const char* path) {
    if (!_ready) return false;

    if (!_sd.exists(path)) {
        Serial.printf("[FileManager] deleteFile: not found %s\n", path);
        return false;
    }

    if (!_sd.remove(path)) {
        Serial.printf("[FileManager] deleteFile: remove failed %s\n", path);
        return false;
    }

    return true;
}

bool FileManager::rename(const char* from, const char* to) {
    if (!_ready) return false;

    if (!_sd.exists(from)) {
        Serial.printf("[FileManager] rename: source not found %s\n", from);
        return false;
    }

    if (!_ensureParentDirs(to)) {
        Serial.printf("[FileManager] rename: cannot create dirs for %s\n", to);
        return false;
    }

    if (!_sd.rename(from, to)) {
        Serial.printf("[FileManager] rename: failed %s -> %s\n", from, to);
        return false;
    }

    return true;
}

// ============================================================
// Directory operations
// ============================================================

bool FileManager::makeDir(const char* dir) {
    if (!_ready) return false;

    if (_sd.exists(dir)) return true;   // already present

    if (!_sd.mkdir(dir, true)) {        // true = create parents
        Serial.printf("[FileManager] makeDir: failed %s\n", dir);
        return false;
    }

    return true;
}

bool FileManager::listDir(const char* dir,
                          std::vector<String>& names,
                          const char* ext)
{
    if (!_ready) return false;

    names.clear();

    FsFile d;
    if (!d.open(dir)) {
        Serial.printf("[FileManager] listDir: cannot open %s\n", dir);
        return false;
    }

    if (!d.isDir()) {
        Serial.printf("[FileManager] listDir: not a directory %s\n", dir);
        d.close();
        return false;
    }

    FsFile entry;
    char   nameBuf[256];

    while (entry.openNext(&d, O_RDONLY)) {
        // Skip directories — only return plain files.
        if (entry.isDir()) {
            entry.close();
            continue;
        }

        entry.getName(nameBuf, sizeof(nameBuf));

        // Skip hidden / system files (names starting with '.').
        if (nameBuf[0] == '.') {
            entry.close();
            continue;
        }

        // Optional extension filter (case-insensitive).
        if (ext != nullptr && !_hasSuffix(nameBuf, ext)) {
            entry.close();
            continue;
        }

        names.emplace_back(nameBuf);
        entry.close();
    }

    d.close();

    // Sort alphabetically (simple case-insensitive strcmp).
    std::sort(names.begin(), names.end(),
              [](const String& a, const String& b) {
                  return strcasecmp(a.c_str(), b.c_str()) < 0;
              });

    return true;
}

// ============================================================
// Private helpers
// ============================================================

bool FileManager::_ensureParentDirs(const char* path) {
    // Find the last '/' — everything before it is the parent directory.
    const char* slash = strrchr(path, '/');
    if (slash == nullptr || slash == path) {
        // Path is in root or has no directory component — nothing to do.
        return true;
    }

    size_t len = static_cast<size_t>(slash - path);
    if (len >= _PATH_BUF) return false;  // path too long

    strncpy(_pathBuf, path, len);
    _pathBuf[len] = '\0';

    if (_sd.exists(_pathBuf)) return true;

    return _sd.mkdir(_pathBuf, true);
}

bool FileManager::_hasSuffix(const char* name, const char* suffix) {
    size_t nLen = strlen(name);
    size_t sLen = strlen(suffix);
    if (sLen > nLen) return false;

    const char* tail = name + (nLen - sLen);
    for (size_t i = 0; i < sLen; ++i) {
        if (tolower(static_cast<unsigned char>(tail[i])) !=
            tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}
