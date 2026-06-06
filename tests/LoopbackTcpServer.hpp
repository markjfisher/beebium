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

#pragma once

// SocketPlatform.hpp first so <winsock2.h> wins the include race on Windows.
#include <beebium/net/SocketPlatform.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace beebium::test {

// An in-process TCP echo server on 127.0.0.1, for exercising the TCP transport
// and the network-serial endpoints without an external peer (tcpser, ser2net).
// Binds an ephemeral port (so parallel test runs never collide), accepts one
// client at a time, and echoes received bytes back -- optionally transformed,
// so an IP232/RFC2217 test can stand in a faithful server-side framing instead
// of a raw echo.
//
// Deterministic by construction: the bound port is known the moment the
// constructor returns (no startup sleep), and close_client() drives a clean,
// observable peer disconnect.
class LoopbackTcpServer {
public:
    // Maps a received chunk to the bytes echoed back. Default: identity (echo).
    using Transform = std::function<std::vector<std::uint8_t>(
        const std::vector<std::uint8_t>&)>;

    explicit LoopbackTcpServer(Transform transform = {})
        : transform_(std::move(transform)) {
        net::ensure_winsock_initialized();
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // ephemeral
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_fd_, 4);
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &len);
        port_ = ntohs(bound.sin_port);
        thread_ = std::thread([this] { run(); });
    }

    ~LoopbackTcpServer() {
        stop_.store(true);
        ::shutdown(listen_fd_, net::kShutdownBoth);  // wake a blocked accept
        close_client();                              // wake a blocked echo_loop
        if (thread_.joinable()) thread_.join();
        net::close_socket(listen_fd_);
    }

    LoopbackTcpServer(const LoopbackTcpServer&) = delete;
    LoopbackTcpServer& operator=(const LoopbackTcpServer&) = delete;

    std::uint16_t port() const { return port_; }

    bool has_client() {
        std::lock_guard<std::mutex> lock(client_mutex_);
        return client_fd_ != net::kInvalidSocket;
    }

    // Simulate a peer disconnect: half-close the current client so its echo_loop
    // observes EOF and the connected TcpClientSerialPort sees the drop.
    void close_client() {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (client_fd_ != net::kInvalidSocket) {
            ::shutdown(client_fd_, net::kShutdownBoth);
        }
    }

private:
    void run() {
        while (!stop_.load()) {
            fd_set rset;
            FD_ZERO(&rset);
            FD_SET(listen_fd_, &rset);
            timeval tv{};
            tv.tv_usec = 100 * 1000;
            int sel = ::select(static_cast<int>(listen_fd_) + 1, &rset, nullptr,
                               nullptr, &tv);
            if (stop_.load()) break;
            if (sel <= 0) continue;
            net::socket_t c = ::accept(listen_fd_, nullptr, nullptr);
            if (c == net::kInvalidSocket) continue;
#ifdef SO_NOSIGPIPE
            int one = 1;
            ::setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE,
                         reinterpret_cast<const char*>(&one), sizeof(one));
#endif
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                client_fd_ = c;
            }
            echo_loop(c);
            std::lock_guard<std::mutex> lock(client_mutex_);
            net::close_socket(c);
            client_fd_ = net::kInvalidSocket;
        }
    }

    void echo_loop(net::socket_t c) {
        std::vector<std::uint8_t> buf(512);
        while (!stop_.load()) {
            fd_set rset;
            FD_ZERO(&rset);
            FD_SET(c, &rset);
            timeval tv{};
            tv.tv_usec = 100 * 1000;
            int sel = ::select(static_cast<int>(c) + 1, &rset, nullptr, nullptr, &tv);
            if (sel < 0) break;
            if (sel == 0) continue;
            auto got = ::recv(c, reinterpret_cast<char*>(buf.data()),
                              static_cast<int>(buf.size()), 0);
            if (got <= 0) break;  // peer closed or error
            std::vector<std::uint8_t> chunk(buf.begin(), buf.begin() + got);
            std::vector<std::uint8_t> out = transform_ ? transform_(chunk) : chunk;
            std::size_t off = 0;
            while (off < out.size() && !stop_.load()) {
#if defined(__linux__)
                constexpr int flags = MSG_NOSIGNAL;
#else
                constexpr int flags = 0;
#endif
                auto s = ::send(c, reinterpret_cast<const char*>(out.data()) + off,
                                static_cast<int>(out.size() - off), flags);
                if (s <= 0) return;  // peer gone
                off += static_cast<std::size_t>(s);
            }
        }
    }

    Transform transform_;
    net::socket_t listen_fd_ = net::kInvalidSocket;
    std::mutex client_mutex_;
    net::socket_t client_fd_ = net::kInvalidSocket;
    std::atomic<bool> stop_{false};
    std::uint16_t port_ = 0;
    std::thread thread_;
};

}  // namespace beebium::test
