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

// Production Win32 SerialPort implementation. Used by PiconetBackend to talk
// to a real Piconet USB-CDC device on COMn (Windows). Mirrors
// PosixSerialPort's read/write/is_open/close contract one-for-one so the
// existing FakePiconetDevice-backed behavioural tests remain the
// specification.

#ifdef _WIN32

#include "beebium/econet/piconet/SerialPort.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace beebium::piconet {

class Win32SerialPort : public SerialPort {
public:
    // Opens device_path as a non-blocking, raw, 115200 8N1 serial port.
    //
    // The path may be a bare COM name ("COM3", "COM12") or an already
    // qualified device path ("\\.\COM3", "\\.\pipe\..." for tests). Bare
    // COM names are normalised with a \\.\ prefix so that COM10+ work
    // (the Win32 DOS-device namespace requires the prefix beyond COM9).
    //
    // On failure is_open() returns false and open_error() carries a
    // human-readable explanation. The constructor does not throw,
    // matching PosixSerialPort's posture.
    explicit Win32SerialPort(const std::string& device_path);

    ~Win32SerialPort() override;

    Win32SerialPort(const Win32SerialPort&) = delete;
    Win32SerialPort& operator=(const Win32SerialPort&) = delete;

    // Non-blocking read with up to 100ms internal timeout. The timeout is
    // enforced via WaitForSingleObject on the OVERLAPPED event; on
    // timeout CancelIoEx aborts the pending ReadFile and GetOverlappedResult
    // drains the result. Mirrors the 100ms select() cadence in
    // PosixSerialPort so the reader thread's shutdown-flag latency is
    // bounded identically.
    ReadResult read(std::span<std::uint8_t> buffer) override;

    // Synchronous write with a bounded total timeout configured via
    // SetCommTimeouts. Returns the number of bytes accepted by the OS;
    // partial writes are possible at error boundaries, matching
    // PosixSerialPort's return shape.
    WriteResult write(std::span<const std::uint8_t> bytes) override;

    bool is_open() const override;

    // Idempotent. CancelIoEx wakes any pending read; CloseHandle is
    // deferred to the destructor so an in-flight drain cannot land on a
    // closed handle. PiconetBackend's destructor joins the reader thread
    // before destroying the SerialPort, so the deferred close is safe.
    void close() override;

    const std::string& device_path() const { return device_path_; }

    // Empty on success; otherwise a FormatMessage rendering of the
    // GetLastError() code from the failing CreateFile (or post-open
    // configuration). Surfaced through PiconetBackend to the Extension UI
    // Indicator so the user sees why the port did not open.
    std::string_view open_error() const noexcept { return open_error_; }

private:
    // Platform-specific members are deliberately kept as opaque integer
    // types so this header does not transitively pull in <windows.h> into
    // every consumer of beebium::piconet::SerialPort. The .cpp file
    // reinterpret_casts as needed.
    static constexpr std::uintptr_t kInvalidHandle = static_cast<std::uintptr_t>(-1);

    std::string device_path_;
    std::string open_error_;
    // Handle is stored as uintptr_t (not HANDLE / void*) for
    // std::atomic<> portability across MSVC permissive-. Casts at the
    // boundary. INVALID_HANDLE_VALUE maps to kInvalidHandle.
    std::atomic<std::uintptr_t> handle_raw_{kInvalidHandle};
    std::atomic<std::uintptr_t> deferred_close_handle_{kInvalidHandle};
    // Manual-reset OVERLAPPED events for read and write. Created in the
    // constructor, destroyed in the destructor.
    std::uintptr_t read_event_raw_{0};
    std::uintptr_t write_event_raw_{0};
};

}  // namespace beebium::piconet

#endif  // _WIN32
