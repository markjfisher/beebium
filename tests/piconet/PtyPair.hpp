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

// POSIX pseudo-terminal pair. Owns the master fd and the slave path
// string. Used by PTY-bridged Piconet integration tests so that the
// PiconetBackend can open a real serial-style device path via
// PosixSerialPort while a FakePiconetDevice drives the master end --
// exercising the full POSIX I/O stack with a controllable peer.
//
// POSIX-only.

#ifndef _WIN32

#include <string>

namespace beebium::piconet::test {

class PtyPair {
public:
    // Opens a new pty pair: posix_openpt + grantpt + unlockpt + ptsname.
    // On failure (rare on macOS/Linux) is_open() returns false and a
    // diagnostic is written to stderr; constructor does not throw.
    PtyPair();

    ~PtyPair();

    PtyPair(const PtyPair&) = delete;
    PtyPair& operator=(const PtyPair&) = delete;

    bool is_open() const { return master_fd_ >= 0; }
    int master_fd() const { return master_fd_; }
    const std::string& slave_path() const { return slave_path_; }

private:
    int master_fd_ = -1;
    std::string slave_path_;
};

}  // namespace beebium::piconet::test

#endif  // !_WIN32
