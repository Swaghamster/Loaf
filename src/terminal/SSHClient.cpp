#include "SSHClient.h"

// ── LibSSH-ESP32 (libssh2 port) ───────────────────────────────────────────
// Install: https://github.com/ewpa/LibSSH-ESP32
// If the library is not yet installed this file will fail to compile with
// a "libssh2.h not found" error — add the dependency to platformio.ini.
#include <libssh2.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

static bool s_libssh2Ready = false;

// ── connect ───────────────────────────────────────────────────────────────
bool SSHClient::connect(const SSHConfig& cfg) {
    if (!s_libssh2Ready) {
        if (libssh2_init(0) != 0) {
            snprintf(_err, sizeof(_err), "libssh2_init failed");
            return false;
        }
        s_libssh2Ready = true;
    }

    // Resolve host
    struct hostent* he = lwip_gethostbyname(cfg.host.c_str());
    if (!he) {
        snprintf(_err, sizeof(_err), "DNS failed for %s", cfg.host.c_str());
        return false;
    }

    // Open TCP socket
    _sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (_sock < 0) {
        snprintf(_err, sizeof(_err), "socket() failed");
        return false;
    }

    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(cfg.port);
    memcpy(&sin.sin_addr, he->h_addr, he->h_length);

    if (lwip_connect(_sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        snprintf(_err, sizeof(_err), "TCP connect failed (%s:%u)", cfg.host.c_str(), cfg.port);
        lwip_close(_sock); _sock = -1;
        return false;
    }

    // SSH session
    _session = libssh2_session_init();
    if (!_session) {
        snprintf(_err, sizeof(_err), "session_init failed");
        lwip_close(_sock); _sock = -1;
        return false;
    }

    libssh2_session_set_blocking((LIBSSH2_SESSION*)_session, 1);

    if (libssh2_session_handshake((LIBSSH2_SESSION*)_session, _sock) != 0) {
        snprintf(_err, sizeof(_err), "SSH handshake failed");
        goto cleanup;
    }

    // Password authentication
    if (libssh2_userauth_password((LIBSSH2_SESSION*)_session,
                                   cfg.user.c_str(), cfg.pass.c_str()) != 0) {
        snprintf(_err, sizeof(_err), "Auth failed (wrong user/pass?)");
        goto cleanup;
    }

    // Open shell channel
    _channel = libssh2_channel_open_session((LIBSSH2_SESSION*)_session);
    if (!_channel) {
        snprintf(_err, sizeof(_err), "channel_open failed");
        goto cleanup;
    }

    // Request PTY
    libssh2_channel_request_pty((LIBSSH2_CHANNEL*)_channel, "xterm-256color");

    // Start interactive shell
    if (libssh2_channel_shell((LIBSSH2_CHANNEL*)_channel) != 0) {
        snprintf(_err, sizeof(_err), "shell request failed");
        libssh2_channel_free((LIBSSH2_CHANNEL*)_channel);
        _channel = nullptr;
        goto cleanup;
    }

    // Switch to non-blocking for normal use
    libssh2_session_set_blocking((LIBSSH2_SESSION*)_session, 0);
    _connected = true;
    return true;

cleanup:
    libssh2_session_disconnect((LIBSSH2_SESSION*)_session, "error");
    libssh2_session_free((LIBSSH2_SESSION*)_session);
    _session = nullptr;
    lwip_close(_sock); _sock = -1;
    return false;
}

// ── disconnect ────────────────────────────────────────────────────────────
void SSHClient::disconnect() {
    if (_channel) {
        libssh2_channel_close((LIBSSH2_CHANNEL*)_channel);
        libssh2_channel_free((LIBSSH2_CHANNEL*)_channel);
        _channel = nullptr;
    }
    if (_session) {
        libssh2_session_disconnect((LIBSSH2_SESSION*)_session, "Loaf terminal closed");
        libssh2_session_free((LIBSSH2_SESSION*)_session);
        _session = nullptr;
    }
    if (_sock >= 0) {
        lwip_close(_sock);
        _sock = -1;
    }
    _connected = false;
}

// ── write ─────────────────────────────────────────────────────────────────
int SSHClient::write(const char* data, size_t len) {
    if (!_connected || !_channel) return -1;
    libssh2_session_set_blocking((LIBSSH2_SESSION*)_session, 1);
    ssize_t n = libssh2_channel_write((LIBSSH2_CHANNEL*)_channel, data, len);
    libssh2_session_set_blocking((LIBSSH2_SESSION*)_session, 0);
    if (n < 0) { _connected = false; return -1; }
    return (int)n;
}

// ── read ──────────────────────────────────────────────────────────────────
int SSHClient::read(char* buf, size_t maxLen) {
    if (!_connected || !_channel) return -1;
    ssize_t n = libssh2_channel_read((LIBSSH2_CHANNEL*)_channel, buf, maxLen);
    if (n == LIBSSH2_ERROR_EAGAIN) return 0;
    if (n < 0) { _connected = false; return -1; }
    if (libssh2_channel_eof((LIBSSH2_CHANNEL*)_channel)) _connected = false;
    return (int)n;
}
