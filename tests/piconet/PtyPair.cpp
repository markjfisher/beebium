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

#include "PtyPair.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>

namespace beebium::piconet::test {

PtyPair::PtyPair() {
    int fd = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0) {
        std::cerr << "PtyPair: posix_openpt failed: " << std::strerror(errno) << "\n";
        return;
    }
    if (::grantpt(fd) != 0) {
        std::cerr << "PtyPair: grantpt failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return;
    }
    if (::unlockpt(fd) != 0) {
        std::cerr << "PtyPair: unlockpt failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return;
    }
    const char* path = ::ptsname(fd);
    if (!path) {
        std::cerr << "PtyPair: ptsname failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return;
    }
    slave_path_ = path;
    master_fd_ = fd;
}

PtyPair::~PtyPair() {
    if (master_fd_ >= 0) {
        ::close(master_fd_);
    }
}

}  // namespace beebium::piconet::test

#endif  // !_WIN32
