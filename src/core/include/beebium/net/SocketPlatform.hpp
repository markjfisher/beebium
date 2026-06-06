// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#ifndef BEEBIUM_NET_SOCKET_PLATFORM_HPP
#define BEEBIUM_NET_SOCKET_PLATFORM_HPP

// Thin cross-platform shim over the Berkeley/Winsock socket API: the type and
// error-handling differences (SOCKET vs int, WSAGetLastError vs errno,
// closesocket vs close, WSAStartup) collected behind a small inline interface.
//
// Shared by the network-backed serial extensions (ip232-serial, the future
// rfc2217 client/server) and any other TCP code in core. The AUN UDP backend
// has its own private copy of these helpers for now; consolidating it onto this
// header is a later, separate refactor.
//
// Include this BEFORE any <windows.h> so <winsock2.h> wins the include race.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <cerrno>
#include <cstring>
#include <string>

namespace beebium::net {

#ifdef _WIN32
using socket_t = SOCKET;
inline constexpr socket_t kInvalidSocket = INVALID_SOCKET;
inline constexpr int kShutdownBoth = SD_BOTH;
#else
using socket_t = int;
inline constexpr socket_t kInvalidSocket = -1;
inline constexpr int kShutdownBoth = SHUT_RDWR;
#endif

#ifdef _WIN32

// One-time Winsock startup, via a function-local static (thread-safe init,
// torn down at process exit). Inline so a single shared instance exists across
// all translation units that include this header.
struct WinsockInitializer {
    WinsockInitializer() {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
    }
    ~WinsockInitializer() { WSACleanup(); }
};

inline void ensure_winsock_initialized() { static WinsockInitializer init; }

inline int last_socket_error() { return WSAGetLastError(); }

inline std::string socket_error_string() {
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(WSAGetLastError()), 0, buf,
                   sizeof(buf), nullptr);
    return buf;
}

inline void close_socket(socket_t s) { ::closesocket(s); }

inline bool set_nonblocking(socket_t s) {
    u_long mode = 1;
    return ::ioctlsocket(s, FIONBIO, &mode) == 0;
}

// connect() on a non-blocking Windows socket reports "in progress" as
// WSAEWOULDBLOCK rather than EINPROGRESS.
inline bool would_block(int err) { return err == WSAEWOULDBLOCK; }
inline bool connect_in_progress(int err) { return err == WSAEWOULDBLOCK; }

#else

inline void ensure_winsock_initialized() {}  // no-op on POSIX

inline int last_socket_error() { return errno; }

inline std::string socket_error_string() { return std::strerror(errno); }

inline void close_socket(socket_t s) { ::close(s); }

inline bool set_nonblocking(socket_t s) {
    int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}

inline bool would_block(int err) { return err == EWOULDBLOCK || err == EAGAIN; }
inline bool connect_in_progress(int err) { return err == EINPROGRESS; }

#endif

}  // namespace beebium::net

#endif  // BEEBIUM_NET_SOCKET_PLATFORM_HPP
