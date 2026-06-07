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

#pragma once

// Production POSIX HostSerialPort implementation: opens an existing device path
// (a real serial port such as /dev/ttyUSB0, or a pseudo-terminal slave such
// as /dev/pts/N) as a non-blocking raw tty.

#ifndef _WIN32

#include "beebium/serial/HostSerialPort.hpp"

#include <atomic>
#include <string>

namespace beebium::serial {

class PosixSerialPort : public HostSerialPort {
public:
    // Opens device_path as a non-blocking, raw, <baud_rate> 8N1 serial port.
    // If the open or termios configuration fails, is_open() returns false and
    // open_error() carries the strerror() text. The constructor does not throw.
    //
    // baud_rate is applied via termios for real serial hardware; for a
    // pseudo-terminal the line speed is irrelevant to data transfer.
    explicit PosixSerialPort(const std::string& device_path, int baud_rate = 115200);

    ~PosixSerialPort() override;

    PosixSerialPort(const PosixSerialPort&) = delete;
    PosixSerialPort& operator=(const PosixSerialPort&) = delete;

    ReadResult read(std::span<std::uint8_t> buffer) override;
    WriteResult write(std::span<const std::uint8_t> bytes) override;
    bool is_open() const override { return fd_.load(std::memory_order_relaxed) >= 0; }
    void close() override;
    void set_break(bool asserted) override;

    const std::string& device_path() const { return device_path_; }
    std::string_view open_error() const noexcept override { return open_error_; }

private:
    std::string device_path_;
    std::string open_error_;
    std::atomic<int> fd_{-1};
};

}  // namespace beebium::serial

#endif  // !_WIN32
