#pragma once
// ============================================================
// SSHClient.h  —  Thin SSH client for Loaf terminal
//
// Requires LibSSH-ESP32 (libssh2 port):
//   https://github.com/ewpa/LibSSH-ESP32
// Add to platformio.ini lib_deps:
//   https://github.com/ewpa/LibSSH-ESP32.git
//
// Also depends on WiFi being connected before calling connect().
// ============================================================

#include <Arduino.h>

struct SSHConfig {
    String   host;
    uint16_t port    = 22;
    String   user;
    String   pass;
};

class SSHClient {
public:
    bool  connect(const SSHConfig& cfg);
    void  disconnect();
    bool  isConnected() const { return _connected; }

    // Returns bytes written, 0 on EAGAIN, -1 on error.
    int   write(const char* data, size_t len);
    // Returns bytes read, 0 if nothing available, -1 on error/EOF.
    int   read(char* buf, size_t maxLen);

    const char* lastError() const { return _err; }

private:
    bool  _connected = false;
    char  _err[80]   = {};

    // Held as void* to keep libssh2 headers out of this header.
    void* _session   = nullptr;   // LIBSSH2_SESSION*
    void* _channel   = nullptr;   // LIBSSH2_CHANNEL*
    int   _sock      = -1;
};
