#include "FileManager.h"
#include "../../include/config.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <algorithm>  // std::sort
#include <cctype>     // tolower
#include <cstring>    // strlen, strcasecmp

// Convenience alias for the SDCardManager singleton.
#define SdMan SDCardManager::getInstance()

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

    if (!SdMan.begin()) {
        Serial.println(F("[FileManager] SD init failed"));
        _ready = false;
        return false;
    }

    // Create required application directories if they don't exist yet.
    SdMan.ensureDirectoryExists(SD_BOOKS_DIR);
    SdMan.ensureDirectoryExists(SD_NOTES_DIR);
    SdMan.ensureDirectoryExists(SD_PROGRESS_DIR);

    _ready = true;
    Serial.println(F("[FileManager] SD ready"));
    return true;
}

// ============================================================
// Queries
// ============================================================

bool FileManager::exists(const char* path) {
    if (!_ready) return false;
    return SdMan.exists(path);
}

size_t FileManager::fileSize(const char* path) {
    if (!_ready) return 0;

    FsFile f = SdMan.open(path, O_RDONLY);
    if (!f) return 0;
    size_t sz = static_cast<size_t>(f.size());
    f.close();
    return sz;
}

// ============================================================
// File I/O
// ============================================================

bool FileManager::readFile(const char* path, String& out) {
    if (!_ready) return false;

    out = SdMan.readFile(path);

    // readFile returns an empty string both for "empty file" and "not found".
    // Distinguish the two cases with an explicit exists() check.
    if (out.isEmpty() && !SdMan.exists(path)) {
        Serial.printf("[FileManager] readFile: cannot open %s\n", path);
        return false;
    }

    return true;
}

bool FileManager::writeFile(const char* path, const String& content) {
    if (!_ready) return false;

    if (!SdMan.writeFile(path, content)) {
        Serial.printf("[FileManager] writeFile: failed on %s\n", path);
        return false;
    }

    return true;
}

bool FileManager::appendFile(const char* path, const String& content) {
    if (!_ready) return false;

    FsFile f = SdMan.open(path, O_WRONLY | O_APPEND | O_CREAT);
    if (!f) {
        Serial.printf("[FileManager] appendFile: cannot open %s\n", path);
        return false;
    }

    size_t toWrite = content.length();
    size_t written = f.write(content.c_str(), toWrite);
    f.close();

    if (written != toWrite) {
        Serial.printf("[FileManager] appendFile: partial write on %s (%u/%u)\n",
                      path, (unsigned)written, (unsigned)toWrite);
        return false;
    }

    return true;
}

bool FileManager::deleteFile(const char* path) {
    if (!_ready) return false;

    if (!SdMan.exists(path)) {
        Serial.printf("[FileManager] deleteFile: not found %s\n", path);
        return false;
    }

    if (!SdMan.remove(path)) {
        Serial.printf("[FileManager] deleteFile: remove failed %s\n", path);
        return false;
    }

    return true;
}

bool FileManager::rename(const char* from, const char* to) {
    if (!_ready) return false;

    if (!SdMan.exists(from)) {
        Serial.printf("[FileManager] rename: source not found %s\n", from);
        return false;
    }

    if (!SdMan.rename(from, to)) {
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

    if (!SdMan.ensureDirectoryExists(dir)) {
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

    std::vector<String> all = SdMan.listFiles(dir);

    for (const String& name : all) {
        // Skip hidden / system files (names starting with '.').
        if (name.length() == 0 || name[0] == '.') continue;

        // Optional extension filter (case-insensitive).
        if (ext != nullptr && !_hasSuffix(name.c_str(), ext)) continue;

        names.push_back(name);
    }

    // Sort alphabetically (case-insensitive).
    std::sort(names.begin(), names.end(),
              [](const String& a, const String& b) {
                  return strcasecmp(a.c_str(), b.c_str()) < 0;
              });

    return true;
}

// ============================================================
// Private helpers
// ============================================================

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
