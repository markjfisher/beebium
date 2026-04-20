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

// Production POSIX SerialPort implementation. Used by PiconetBackend to talk
// to a real Piconet USB-CDC device on /dev/tty.usbmodem* (macOS) or
// /dev/ttyACM* (Linux). Windows is not supported here -- a separate
// Win32SerialPort would live alongside this file when needed.

#ifndef _WIN32

#include "beebium/econet/piconet/SerialPort.hpp"

#include <atomic>
#include <string>

namespace beebium::piconet {

class PosixSerialPort : public SerialPort {
public:
    // Opens device_path as a non-blocking, raw, 115200 8N1 serial port. If
    // the open or termios configuration fails, is_open() returns false and
    // an explanatory message goes to stderr (the constructor does not throw,
    // matching AunBackend's posture).
    explicit PosixSerialPort(const std::string& device_path);

    ~PosixSerialPort() override;

    PosixSerialPort(const PosixSerialPort&) = delete;
    PosixSerialPort& operator=(const PosixSerialPort&) = delete;

    // Non-blocking read with up to 100ms internal timeout via select().
    ReadResult read(std::span<std::uint8_t> buffer) override;

    // Synchronous write. Returns the number of bytes accepted by the OS.
    // Per the SerialPort threading contract, only the emulation thread
    // calls this, so no internal mutex is required.
    WriteResult write(std::span<const std::uint8_t> bytes) override;

    bool is_open() const override { return fd_.load(std::memory_order_relaxed) >= 0; }

    void close() override;

    const std::string& device_path() const { return device_path_; }

    // Empty when the open succeeded; otherwise the strerror() text for
    // the errno set by the failing open() syscall (e.g. "No such file
    // or directory", "Permission denied"). Useful for surfacing a
    // human-readable diagnosis through the Extension UI Indicator.
    std::string_view open_error() const noexcept { return open_error_; }

private:
    std::string device_path_;
    std::string open_error_;
    // Atomic so that the reader thread observing close() (called from the
    // emulation thread / destructor) sees the transition consistently
    // without needing a mutex. Reads/writes use the value at call time;
    // a close() midway through a syscall is handled by the OS returning
    // EBADF / EIO, which we map to ReadResult{error=true} / WriteResult{error=true}.
    std::atomic<int> fd_{-1};
};

}  // namespace beebium::piconet

#endif  // !_WIN32
