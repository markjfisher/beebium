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

// Production Win32 HostSerialPort implementation: opens an existing device path
// (a real COM port, or a named pipe in tests) as a non-blocking raw port.
// Mirrors PosixSerialPort's read/write/is_open/close contract one-for-one.

#ifdef _WIN32

#include "beebium/serial/HostSerialPort.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace beebium::serial {

class Win32SerialPort : public HostSerialPort {
public:
    // Opens device_path as a non-blocking, raw, <baud_rate> 8N1 serial port.
    // The path may be a bare COM name ("COM3") or a qualified device path
    // ("\\.\COM3", "\\.\pipe\..."). On failure is_open() returns false and
    // open_error() carries the diagnosis. The constructor does not throw.
    explicit Win32SerialPort(const std::string& device_path, int baud_rate = 115200);

    ~Win32SerialPort() override;

    Win32SerialPort(const Win32SerialPort&) = delete;
    Win32SerialPort& operator=(const Win32SerialPort&) = delete;

    ReadResult read(std::span<std::uint8_t> buffer) override;
    WriteResult write(std::span<const std::uint8_t> bytes) override;
    bool is_open() const override;
    void close() override;

    const std::string& device_path() const { return device_path_; }
    std::string_view open_error() const noexcept override { return open_error_; }

private:
    static constexpr std::uintptr_t kInvalidHandle = static_cast<std::uintptr_t>(-1);

    std::string device_path_;
    std::string open_error_;
    std::atomic<std::uintptr_t> handle_raw_{kInvalidHandle};
    std::atomic<std::uintptr_t> deferred_close_handle_{kInvalidHandle};
    std::uintptr_t read_event_raw_{0};
    std::uintptr_t write_event_raw_{0};
};

}  // namespace beebium::serial

#endif  // _WIN32
