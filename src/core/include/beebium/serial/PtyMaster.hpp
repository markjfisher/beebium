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

// Creates a POSIX pseudo-terminal and owns the master end as a SerialPort.
//
// Beebium calls posix_openpt/grantpt/unlockpt/ptsname to create the pair,
// reads/writes the master fd, and advertises the slave path (e.g. /dev/pts/7)
// so any external serial client can attach to it -- no socat or pre-existing
// device required. Optionally a stable symlink to the slave is created so the
// path doesn't change between runs.

#ifndef _WIN32

#include "beebium/serial/SerialPort.hpp"

#include <atomic>
#include <string>

namespace beebium::serial {

class PtyMaster : public SerialPort {
public:
    // Create a new pty. If symlink_path is non-empty, a symlink pointing at
    // the slave device is created there (an existing symlink is replaced) so
    // clients can use a stable path. On failure is_open() returns false and
    // open_error() carries the diagnosis; the constructor does not throw.
    explicit PtyMaster(const std::string& symlink_path = "");

    ~PtyMaster() override;

    PtyMaster(const PtyMaster&) = delete;
    PtyMaster& operator=(const PtyMaster&) = delete;

    ReadResult read(std::span<std::uint8_t> buffer) override;
    WriteResult write(std::span<const std::uint8_t> bytes) override;
    bool is_open() const override { return fd_.load(std::memory_order_relaxed) >= 0; }
    void close() override;

    // The kernel-assigned slave device path (e.g. /dev/pts/7).
    const std::string& slave_path() const { return slave_path_; }

    // The path clients should attach to: the symlink if one was created,
    // otherwise the slave path.
    const std::string& advertised_path() const { return advertised_path_; }

    std::string_view open_error() const noexcept override { return open_error_; }

private:
    std::string slave_path_;
    std::string symlink_path_;       // empty if no symlink requested
    std::string advertised_path_;
    std::string open_error_;
    std::atomic<int> fd_{-1};
};

}  // namespace beebium::serial

#endif  // !_WIN32
