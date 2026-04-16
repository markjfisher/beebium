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

#ifndef _WIN32

#include "FakePiconetDeviceOnPty.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <span>
#include <sys/select.h>
#include <unistd.h>

namespace beebium::piconet::test {

FakePiconetDeviceOnPty::FakePiconetDeviceOnPty() {
    if (!pty_.is_open()) {
        return;  // pty open failed; pumper thread not started
    }
    // Make master fd non-blocking so the pumper can poll both directions
    // with a single select() and not get stuck.
    int flags = ::fcntl(pty_.master_fd(), F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(pty_.master_fd(), F_SETFL, flags | O_NONBLOCK);
    }
    pumper_ = std::thread([this] { pumper_loop(); });
}

FakePiconetDeviceOnPty::~FakePiconetDeviceOnPty() {
    shutdown_.store(true, std::memory_order_relaxed);
    if (pumper_.joinable()) {
        pumper_.join();
    }
    // pty_ destructor closes the master fd.
}

void FakePiconetDeviceOnPty::pumper_loop() {
    std::array<std::uint8_t, 256> buf{};
    while (!shutdown_.load(std::memory_order_relaxed)) {
        // Direction A: master -> fake (commands written by PiconetBackend
        // arrive here via the PTY; we forward them into the fake by
        // calling its write() method).
        ssize_t n = ::read(pty_.master_fd(), buf.data(), buf.size());
        if (n > 0) {
            fake_.write(std::span<const std::uint8_t>{buf.data(),
                                                      static_cast<std::size_t>(n)});
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            // Master fd closed or error; exit the loop.
            return;
        }

        // Direction B: fake -> master (events queued by the fake get
        // pulled out via its read() and pushed onto the pty master, where
        // they emerge on the slave side as serial bytes for PiconetBackend's
        // reader to consume).
        auto rr = fake_.read({buf.data(), buf.size()});
        if (rr.error) return;
        if (rr.bytes > 0) {
            std::size_t total = 0;
            while (total < rr.bytes) {
                ssize_t w = ::write(pty_.master_fd(), buf.data() + total, rr.bytes - total);
                if (w > 0) {
                    total += static_cast<std::size_t>(w);
                } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // PTY slave hasn't drained its buffer yet; wait briefly.
                    fd_set wfds;
                    FD_ZERO(&wfds);
                    FD_SET(pty_.master_fd(), &wfds);
                    timeval tv{0, 50000};  // 50ms
                    ::select(pty_.master_fd() + 1, nullptr, &wfds, nullptr, &tv);
                } else if (w < 0 && errno == EINTR) {
                    continue;
                } else {
                    return;  // unrecoverable write error
                }
            }
        }

        // If neither direction had data, sleep briefly via select() on
        // the master fd to avoid busy-spinning.
        if (rr.would_block && (n < 0 || n == 0)) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(pty_.master_fd(), &rfds);
            timeval tv{0, 5000};  // 5ms -- balance responsiveness vs CPU
            ::select(pty_.master_fd() + 1, &rfds, nullptr, nullptr, &tv);
        }
    }
}

}  // namespace beebium::piconet::test

#endif  // !_WIN32
