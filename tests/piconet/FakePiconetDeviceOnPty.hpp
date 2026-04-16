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

// FakePiconetDevice exposed via a POSIX pseudo-terminal. The fake's
// SerialPort interface is bridged byte-for-byte to a PTY master fd
// by a dedicated pumper thread. The PTY slave path can be opened by
// PiconetBackend via PosixSerialPort, exercising the full POSIX I/O
// stack with a controllable peer.
//
// This is the parity-test infrastructure for Phase 7b: the same
// integration tests that pass with FakePiconetDevice as a direct
// SerialPort should also pass when PiconetBackend reads/writes via
// a real PosixSerialPort connected to the same fake through a PTY.
//
// POSIX-only.

#ifndef _WIN32

#include "FakePiconetDevice.hpp"
#include "PtyPair.hpp"

#include <atomic>
#include <thread>

namespace beebium::piconet::test {

class FakePiconetDeviceOnPty {
public:
    FakePiconetDeviceOnPty();
    ~FakePiconetDeviceOnPty();

    FakePiconetDeviceOnPty(const FakePiconetDeviceOnPty&) = delete;
    FakePiconetDeviceOnPty& operator=(const FakePiconetDeviceOnPty&) = delete;

    bool is_open() const { return pty_.is_open(); }

    // The slave-side device path (e.g. "/dev/pts/N" on Linux,
    // "/dev/ttysNNN" on macOS). PiconetBackend opens this via
    // PosixSerialPort.
    const std::string& slave_path() const { return pty_.slave_path(); }

    // Direct access to the fake for test injection and inspection
    // (set_firmware_version, inject_inbound_*, etc.). Test code should
    // remember the fake itself runs on the *master* end of the pty;
    // bytes you put on it via inject_inbound_* will be pumped onto the
    // pty master fd, then arrive on the slave side as serial bytes
    // for PiconetBackend's reader thread.
    FakePiconetDevice& device() { return fake_; }

private:
    void pumper_loop();

    PtyPair pty_;
    FakePiconetDevice fake_;
    std::atomic<bool> shutdown_{false};
    std::thread pumper_;
};

}  // namespace beebium::piconet::test

#endif  // !_WIN32
