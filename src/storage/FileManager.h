#pragma once

#include <Arduino.h>
#include <vector>

// ============================================================
// FileManager
// Thin wrapper around SDCardManager (community SDK singleton)
// that provides the file-system operations needed by the Loaf
// app layer.
//
// Usage:
//   if (!FileManager::instance().init()) { /* handle error */ }
//   String txt;
//   FileManager::instance().readFile("/books/mybook.txt", txt);
// ============================================================

class FileManager {
public:
    // ---------------------------------------------------------
    // Singleton access
    // ---------------------------------------------------------
    static FileManager& instance();

    // Prevent copy/move
    FileManager(const FileManager&)            = delete;
    FileManager& operator=(const FileManager&) = delete;
    FileManager(FileManager&&)                 = delete;
    FileManager& operator=(FileManager&&)      = delete;

    // ---------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------

    /// Initialise the SD card via SDCardManager and create
    /// required application directories.  Returns true on success.
    bool init();

    /// Returns true if the SD card was successfully initialised.
    bool isReady() const { return _ready; }

    // ---------------------------------------------------------
    // Queries
    // ---------------------------------------------------------

    /// Returns true if path (file or directory) exists on the SD.
    bool exists(const char* path);

    /// Returns the size of file at path, or 0 if not found.
    size_t fileSize(const char* path);

    // ---------------------------------------------------------
    // File I/O
    // ---------------------------------------------------------

    /// Read the entire contents of path into out.
    /// Returns false if the file cannot be opened or read.
    bool readFile(const char* path, String& out);

    /// Overwrite path with content (creates the file if needed).
    /// Returns false on failure.
    bool writeFile(const char* path, const String& content);

    /// Append content to path (creates the file if needed).
    /// Returns false on failure.
    bool appendFile(const char* path, const String& content);

    /// Delete the file at path.
    /// Returns false if the file does not exist or cannot be removed.
    bool deleteFile(const char* path);

    /// Rename / move a file or directory.
    /// Returns false on failure.
    bool rename(const char* from, const char* to);

    // ---------------------------------------------------------
    // Directory operations
    // ---------------------------------------------------------

    /// Create directory dir (including any missing parent dirs).
    /// Returns true if the directory exists after the call.
    bool makeDir(const char* dir);

    /// List the immediate children of dir into names.
    /// If ext is non-null (e.g. ".txt"), only entries whose names
    /// end with that extension (case-insensitive) are included.
    /// Directories and hidden files are excluded.
    /// Returns false if dir cannot be listed.
    bool listDir(const char* dir,
                 std::vector<String>& names,
                 const char* ext = nullptr);

private:
    FileManager() = default;

    // Case-insensitive suffix check helper.
    static bool _hasSuffix(const char* name, const char* suffix);

    bool _ready = false;
};
