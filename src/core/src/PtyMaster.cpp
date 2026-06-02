// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#include "beebium/serial/PtyMaster.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace beebium::serial {

PtyMaster::PtyMaster(const std::string& symlink_path)
    : symlink_path_(symlink_path) {
    int fd = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0) {
        open_error_ = std::strerror(errno);
        std::cerr << "PtyMaster: posix_openpt failed: " << open_error_ << "\n";
        return;
    }
    if (::grantpt(fd) != 0 || ::unlockpt(fd) != 0) {
        open_error_ = std::strerror(errno);
        std::cerr << "PtyMaster: grantpt/unlockpt failed: " << open_error_ << "\n";
        ::close(fd);
        return;
    }
    const char* name = ::ptsname(fd);
    if (!name) {
        open_error_ = std::strerror(errno);
        std::cerr << "PtyMaster: ptsname failed: " << open_error_ << "\n";
        ::close(fd);
        return;
    }
    slave_path_ = name;

    // Make the master non-blocking; read() uses select() for timing.
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    advertised_path_ = slave_path_;
    if (!symlink_path_.empty()) {
        ::unlink(symlink_path_.c_str());  // replace any stale symlink
        if (::symlink(slave_path_.c_str(), symlink_path_.c_str()) == 0) {
            advertised_path_ = symlink_path_;
        } else {
            std::cerr << "PtyMaster: symlink(" << symlink_path_
                      << ") failed: " << std::strerror(errno)
                      << " -- advertising slave path directly\n";
        }
    }

    fd_.store(fd, std::memory_order_relaxed);
}

PtyMaster::~PtyMaster() {
    close();
}

ReadResult PtyMaster::read(std::span<std::uint8_t> buffer) {
    int fd = fd_.load(std::memory_order_relaxed);
    if (fd < 0) {
        return ReadResult{0, false, true};
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms

    int ready = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (ready < 0) {
        if (errno == EINTR) return ReadResult{0, true, false};
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
        // EIO here usually means no slave is currently open. Treat as
        // would_block rather than a fatal error so the endpoint keeps
        // running while a client connects/disconnects.
        if (errno == EIO) {
            return ReadResult{0, true, false};
        }
        return ReadResult{0, false, true};
    }
    if (n == 0) {
        return ReadResult{0, true, false};  // no slave attached yet
    }
    return ReadResult{static_cast<std::size_t>(n), false, false};
}

WriteResult PtyMaster::write(std::span<const std::uint8_t> bytes) {
    int fd = fd_.load(std::memory_order_relaxed);
    if (fd < 0) {
        return WriteResult{0, true};
    }
    std::size_t total = 0;
    while (total < bytes.size()) {
        ssize_t n = ::write(fd, bytes.data() + total, bytes.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO) {
                // Buffer full or no slave attached: report progress, no fatal.
                return WriteResult{total, false};
            }
            return WriteResult{total, true};
        }
        if (n == 0) {
            return WriteResult{total, true};
        }
        total += static_cast<std::size_t>(n);
    }
    return WriteResult{total, false};
}

void PtyMaster::close() {
    int fd = fd_.exchange(-1, std::memory_order_relaxed);
    if (fd >= 0) {
        ::close(fd);
    }
    if (!symlink_path_.empty()) {
        ::unlink(symlink_path_.c_str());
    }
}

}  // namespace beebium::serial

#endif  // !_WIN32
