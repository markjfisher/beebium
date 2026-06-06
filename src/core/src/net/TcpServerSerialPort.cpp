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

#include <beebium/net/TcpServerSerialPort.hpp>

#ifndef _WIN32
#include <netinet/tcp.h>  // TCP_NODELAY (POSIX); Windows gets it via ws2tcpip.h
#endif

#include <algorithm>
#include <cstdint>
#include <string>

namespace beebium::net {

namespace {

#if defined(__linux__)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

socket_t to_socket(std::intptr_t v) { return static_cast<socket_t>(v); }
std::intptr_t to_intptr(socket_t s) { return static_cast<std::intptr_t>(s); }

// Apply the per-connection socket options the serial bridge wants.
void configure_stream(socket_t fd) {
    set_nonblocking(fd);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_NOSIGPIPE
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                 reinterpret_cast<const char*>(&one), sizeof(one));
#endif
}

}  // namespace

TcpServerSerialPort::TcpServerSerialPort(const std::string& bind_addr,
                                         std::uint16_t port) {
    ensure_winsock_initialized();
    listen_on(bind_addr, port);
}

TcpServerSerialPort::~TcpServerSerialPort() {
    socket_t lf = to_socket(listen_fd_.exchange(-1));
    if (lf != kInvalidSocket) close_socket(lf);
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ != -1) {
        close_socket(to_socket(client_fd_));
        client_fd_ = -1;
    }
}

void TcpServerSerialPort::listen_on(const std::string& bind_addr,
                                    std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const std::string port_str = std::to_string(port);
    if (::getaddrinfo(bind_addr.c_str(), port_str.c_str(), &hints, &results) != 0 ||
        results == nullptr) {
        open_error_ = "cannot resolve bind address " + bind_addr + ":" + port_str
                      + ": " + socket_error_string();
        return;
    }

    std::string last_error;
    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        socket_t fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == kInvalidSocket) {
            last_error = socket_error_string();
            continue;
        }
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof(one));
        if (::bind(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) != 0 ||
            ::listen(fd, 1) != 0) {
            last_error = socket_error_string();
            close_socket(fd);
            continue;
        }
        set_nonblocking(fd);
        listen_fd_.store(to_intptr(fd));
        open_.store(true);
        ::freeaddrinfo(results);
        return;
    }

    ::freeaddrinfo(results);
    open_error_ = "cannot bind " + bind_addr + ":" + port_str
                  + (last_error.empty() ? "" : ": " + last_error);
}

void TcpServerSerialPort::drop_client(std::intptr_t fd_copy) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ == fd_copy && fd_copy != -1) {
        ::shutdown(to_socket(fd_copy), kShutdownBoth);
        close_socket(to_socket(fd_copy));
        client_fd_ = -1;
    }
}

serial::ReadResult TcpServerSerialPort::read(std::span<std::uint8_t> buffer) {
    if (!open_.load()) {
        return {0, false, true};
    }
    socket_t lf = to_socket(listen_fd_.load());
    if (lf == kInvalidSocket) {
        return {0, false, true};
    }

    socket_t client;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client = to_socket(client_fd_);
    }

    // Watch the listener (for a new/rejected connection) and the client (data).
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(lf, &rset);
    socket_t maxfd = lf;
    if (client != kInvalidSocket) {
        FD_SET(client, &rset);
        maxfd = std::max(lf, client);
    }
    timeval tv{};
    tv.tv_usec = 100 * 1000;
    int sel = ::select(static_cast<int>(maxfd) + 1, &rset, nullptr, nullptr, &tv);
    if (sel < 0) {
        return would_block(last_socket_error()) ? serial::ReadResult{0, true, false}
                                                 : serial::ReadResult{0, false, true};
    }
    if (sel == 0) {
        return {0, true, false};
    }

    if (FD_ISSET(lf, &rset)) {
        socket_t c = ::accept(lf, nullptr, nullptr);
        if (c != kInvalidSocket) {
            configure_stream(c);
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_fd_ == -1) {
                client_fd_ = to_intptr(c);  // first client: adopt it
            } else {
                ::shutdown(c, kShutdownBoth);  // already serving one: reject
                close_socket(c);
            }
        }
        return {0, true, false};
    }

    if (client != kInvalidSocket && FD_ISSET(client, &rset)) {
        auto got = ::recv(client, reinterpret_cast<char*>(buffer.data()),
                          static_cast<int>(buffer.size()), 0);
        if (got > 0) {
            return {static_cast<std::size_t>(got), false, false};
        }
        if (got < 0 && would_block(last_socket_error())) {
            return {0, true, false};
        }
        drop_client(to_intptr(client));  // client closed/errored: keep serving
        return {0, true, false};
    }

    return {0, true, false};
}

serial::WriteResult TcpServerSerialPort::write(std::span<const std::uint8_t> bytes) {
    socket_t client;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client = to_socket(client_fd_);
    }
    if (client == kInvalidSocket) {
        return {bytes.size(), false};  // no client attached: discard, not an error
    }
    auto sent = ::send(client, reinterpret_cast<const char*>(bytes.data()),
                       static_cast<int>(bytes.size()), kSendFlags);
    if (sent < 0) {
        if (would_block(last_socket_error())) return {0, false};
        drop_client(to_intptr(client));  // client gone; the server stays open
        return {0, false};
    }
    return {static_cast<std::size_t>(sent), false};
}

bool TcpServerSerialPort::is_open() const {
    return open_.load() && to_socket(listen_fd_.load()) != kInvalidSocket;
}

bool TcpServerSerialPort::is_connected() const {
    std::lock_guard<std::mutex> lock(client_mutex_);
    return client_fd_ != -1;
}

void TcpServerSerialPort::close() {
    open_.store(false);
    socket_t lf = to_socket(listen_fd_.load());
    if (lf != kInvalidSocket) {
        ::shutdown(lf, kShutdownBoth);  // wake a reader parked in accept()/select()
    }
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ != -1) {
        ::shutdown(to_socket(client_fd_), kShutdownBoth);
    }
}

std::uint16_t TcpServerSerialPort::local_port() const {
    socket_t lf = to_socket(listen_fd_.load());
    if (lf == kInvalidSocket) return 0;
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(lf, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    if (addr.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port);
    }
    return ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
}

std::string_view TcpServerSerialPort::open_error() const noexcept {
    return open_error_;
}

}  // namespace beebium::net
