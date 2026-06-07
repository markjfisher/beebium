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

// SocketPlatform.hpp must precede everything so <winsock2.h> wins the race.
#include <beebium/net/SocketPlatform.hpp>

#include <beebium/net/TcpClientSerialPort.hpp>

#ifndef _WIN32
#include <netinet/tcp.h>  // TCP_NODELAY (POSIX); Windows gets it via ws2tcpip.h
#endif

#include <cstdint>
#include <string>

namespace beebium::net {

namespace {

// Send flags that suppress SIGPIPE where the platform offers them as a flag.
#if defined(__linux__)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

socket_t to_socket(std::intptr_t v) { return static_cast<socket_t>(v); }
std::intptr_t to_intptr(socket_t s) { return static_cast<std::intptr_t>(s); }

}  // namespace

TcpClientSerialPort::TcpClientSerialPort(const std::string& host, uint16_t port,
                                         std::chrono::milliseconds connect_timeout) {
    ensure_winsock_initialized();
    connect(host, port, connect_timeout);
}

TcpClientSerialPort::~TcpClientSerialPort() {
    // close() only shuts the socket down (so a blocked reader returns); the
    // descriptor is released here, after the owner has joined its I/O threads,
    // so no select() can be racing a reused descriptor.
    socket_t fd = to_socket(fd_.exchange(-1));
    if (fd != kInvalidSocket) {
        close_socket(fd);
    }
}

void TcpClientSerialPort::connect(const std::string& host, uint16_t port,
                                  std::chrono::milliseconds timeout) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;  // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &results) != 0 ||
        results == nullptr) {
        open_error_ = "cannot resolve " + host + ":" + port_str + ": "
                      + socket_error_string();
        return;
    }

    std::string last_error;
    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        socket_t fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == kInvalidSocket) {
            last_error = socket_error_string();
            continue;
        }
        if (!set_nonblocking(fd)) {
            last_error = socket_error_string();
            close_socket(fd);
            continue;
        }

        int rc = ::connect(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
        if (rc != 0 && connect_in_progress(last_socket_error())) {
            // Wait for the non-blocking connect to complete (or time out).
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            timeval tv{};
            tv.tv_sec = static_cast<long>(timeout.count() / 1000);
            tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
            int sel = ::select(static_cast<int>(fd) + 1, nullptr, &wset, nullptr, &tv);
            if (sel > 0) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                             reinterpret_cast<char*>(&so_error), &len);
                rc = (so_error == 0) ? 0 : -1;
                if (so_error != 0) last_error = std::strerror(so_error);
            } else {
                rc = -1;
                last_error = (sel == 0) ? "connection timed out" : socket_error_string();
            }
        } else if (rc != 0) {
            last_error = socket_error_string();
        }

        if (rc == 0) {
            // Connected. Disable Nagle so small serial writes go out promptly.
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                         reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_NOSIGPIPE
            // macOS/BSD: suppress SIGPIPE on send to a closed peer.
            ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                         reinterpret_cast<const char*>(&one), sizeof(one));
#endif
            fd_.store(to_intptr(fd));
            open_.store(true);
            ::freeaddrinfo(results);
            return;
        }
        close_socket(fd);
    }

    ::freeaddrinfo(results);
    open_error_ = "cannot connect to " + host + ":" + port_str
                  + (last_error.empty() ? "" : ": " + last_error);
}

serial::ReadResult TcpClientSerialPort::read(std::span<std::uint8_t> buffer) {
    socket_t fd = to_socket(fd_.load());
    if (fd == kInvalidSocket || !open_.load()) {
        return {0, false, true};  // closed
    }

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    timeval tv{};
    tv.tv_usec = 100 * 1000;  // 100ms, matching the HostSerialPort contract
    int sel = ::select(static_cast<int>(fd) + 1, &rset, nullptr, nullptr, &tv);
    if (sel < 0) {
        if (would_block(last_socket_error())) return {0, true, false};  // EINTR-ish
        return {0, false, true};
    }
    if (sel == 0) {
        return {0, true, false};  // timeout: nothing to read
    }

    auto got = ::recv(fd, reinterpret_cast<char*>(buffer.data()),
                      static_cast<int>(buffer.size()), 0);
    if (got == 0) {
        open_.store(false);  // peer closed
        return {0, false, true};
    }
    if (got < 0) {
        if (would_block(last_socket_error())) return {0, true, false};
        open_.store(false);
        return {0, false, true};
    }
    return {static_cast<std::size_t>(got), false, false};
}

serial::WriteResult TcpClientSerialPort::write(std::span<const std::uint8_t> bytes) {
    socket_t fd = to_socket(fd_.load());
    if (fd == kInvalidSocket || !open_.load()) {
        return {0, true};
    }
    auto sent = ::send(fd, reinterpret_cast<const char*>(bytes.data()),
                       static_cast<int>(bytes.size()), kSendFlags);
    if (sent < 0) {
        if (would_block(last_socket_error())) return {0, false};  // EAGAIN: retry later
        open_.store(false);
        return {0, true};
    }
    return {static_cast<std::size_t>(sent), false};
}

bool TcpClientSerialPort::is_open() const {
    return open_.load() && to_socket(fd_.load()) != kInvalidSocket;
}

void TcpClientSerialPort::close() {
    std::lock_guard<std::mutex> lock(close_mutex_);
    open_.store(false);
    socket_t fd = to_socket(fd_.load());
    if (fd != kInvalidSocket) {
        // Half-close to wake a reader blocked in select(); the descriptor itself
        // is released in the destructor so it stays valid for any in-flight
        // select() on another thread.
        ::shutdown(fd, kShutdownBoth);
    }
}

std::string_view TcpClientSerialPort::open_error() const noexcept {
    return open_error_;
}

}  // namespace beebium::net
