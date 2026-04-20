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

#include "beebium/econet/piconet/PosixSerialPort.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace beebium::piconet {

namespace {

// Configure a tty fd for raw 115200 8N1 (cfmakeraw + cfsetspeed), with
// VMIN=0, VTIME=0 so reads don't block (we use select() for timing).
//
// Non-tty devices (e.g. /dev/null in unit tests) cause tcgetattr/tcsetattr
// to fail; we log and proceed without termios configuration. The fd is
// still usable for read()/write(), which is enough for plumbing tests.
// On a real Piconet device this would never happen.
void configure_tty(int fd, const std::string& path) {
    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        return;  // Not a tty -- proceed without termios setup.
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;      // 8 data bits
    tty.c_cflag &= ~CRTSCTS;  // No hardware flow control
    tty.c_cflag |=  CREAD | CLOCAL;

    tty.c_cc[VMIN]  = 0;  // Don't block waiting for a min byte count.
    tty.c_cc[VTIME] = 0;  // Pure non-blocking; select() handles timing.

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "PosixSerialPort: tcsetattr(" << path
                  << ") failed: " << std::strerror(errno) << "\n";
        // Still proceed: the fd is open and the subsequent read/write may
        // work in unit tests. For real Piconet devices this is unlikely.
    }
}

}  // namespace

PosixSerialPort::PosixSerialPort(const std::string& device_path)
    : device_path_(device_path) {
    int fd = ::open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        // Capture the errno text into open_error_ before anything else
        // can clobber errno, so PiconetEconetTransportExtension (and
        // ultimately PiconetUi via the Indicator) can surface the
        // diagnosis to the user.
        open_error_ = std::strerror(errno);
        std::cerr << "PosixSerialPort: open(" << device_path
                  << ") failed: " << open_error_ << "\n";
        return;
    }
    configure_tty(fd, device_path);
    fd_.store(fd, std::memory_order_relaxed);
}

PosixSerialPort::~PosixSerialPort() {
    close();
}

ReadResult PosixSerialPort::read(std::span<std::uint8_t> buffer) {
    int fd = fd_.load(std::memory_order_relaxed);
    if (fd < 0) {
        return ReadResult{0, false, true};
    }

    // 100ms wait via select(). The reader thread checks shutdown_ after
    // each return, so this bounds the join latency on destruction.
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;  // 100ms

    int ready = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (ready < 0) {
        if (errno == EINTR) return ReadResult{0, true, false};  // treat as would_block
        return ReadResult{0, false, true};
    }
    if (ready == 0) {
        return ReadResult{0, true, false};
    }

    ssize_t n = ::read(fd, buffer.data(), buffer.size());
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ReadResult{0, true, false};
        }
        return ReadResult{0, false, true};
    }
    if (n == 0) {
        // EOF -- typical for /dev/null on read; pty hangup; cable removed.
        return ReadResult{0, false, true};
    }
    return ReadResult{static_cast<std::size_t>(n), false, false};
}

WriteResult PosixSerialPort::write(std::span<const std::uint8_t> bytes) {
    int fd = fd_.load(std::memory_order_relaxed);
    if (fd < 0) {
        return WriteResult{0, true};
    }
    std::size_t total = 0;
    while (total < bytes.size()) {
        ssize_t n = ::write(fd, bytes.data() + total, bytes.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return WriteResult{total, true};
        }
        if (n == 0) {
            return WriteResult{total, true};
        }
        total += static_cast<std::size_t>(n);
    }
    return WriteResult{total, false};
}

void PosixSerialPort::close() {
    int fd = fd_.exchange(-1, std::memory_order_relaxed);
    if (fd >= 0) {
        ::close(fd);
    }
}

}  // namespace beebium::piconet

#endif  // !_WIN32
