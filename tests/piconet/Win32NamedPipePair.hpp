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

#ifdef _WIN32

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Test helper: a one-instance bidirectional named pipe that stands in for a
// real COM port at the other end of Win32SerialPort. The server handle
// lives with the helper (the test uses it to inject / observe bytes); the
// client side is opened by Win32SerialPort via CreateFile on pipe_name().
//
// Equivalent to tests/piconet/PtyPair.{hpp,cpp} on POSIX, but built on
// CreateNamedPipe + ConnectNamedPipe + OVERLAPPED I/O. One-instance
// (nMaxInstances=1) keeps the model simple: each pair backs exactly one
// Win32SerialPort.

namespace piconet_test {

class Win32NamedPipePair {
public:
    Win32NamedPipePair();
    ~Win32NamedPipePair();

    Win32NamedPipePair(const Win32NamedPipePair&) = delete;
    Win32NamedPipePair& operator=(const Win32NamedPipePair&) = delete;

    // Byte-stream pipe path in the Win32 namespace, e.g.
    // "\\\\.\\pipe\\beebium-piconet-test-12345-0". Pass this to
    // Win32SerialPort's constructor -- the \\.\ prefix is already present,
    // so normalise_com_path() inside the port passes it through untouched.
    const std::string& pipe_name() const { return pipe_name_; }

    // True if the server end was created successfully. False if
    // CreateNamedPipe or CreateEvent failed; test should SKIP in that case.
    bool is_open() const;

    // Blocks up to timeout_ms waiting for the client side (Win32SerialPort)
    // to connect. Returns true on success. Call this after constructing the
    // Win32SerialPort on pipe_name().
    bool wait_for_client(int timeout_ms);

    // Blocking write to the server side -- bytes flow through the kernel
    // pipe buffer and are readable by Win32SerialPort::read(). Returns
    // true if all bytes were accepted within timeout_ms.
    bool server_write(std::span<const std::uint8_t> bytes, int timeout_ms);

    // Blocking read from the server side -- returns bytes that
    // Win32SerialPort::write() pushed into the pipe. Returns number of
    // bytes actually read (0 on timeout, <0 on error).
    int server_read(std::span<std::uint8_t> buffer, int timeout_ms);

    // Explicit server-side disconnect -- makes Win32SerialPort::read() on
    // the client end see ERROR_BROKEN_PIPE (mapped to error=true).
    void disconnect();

private:
    std::string pipe_name_;
    std::uintptr_t server_handle_raw_{0};   // HANDLE; 0 == uninitialised
    std::uintptr_t connect_event_raw_{0};   // Manual-reset event for ConnectNamedPipe OVERLAPPED
    std::uintptr_t read_event_raw_{0};
    std::uintptr_t write_event_raw_{0};
    bool connect_pending_{false};
};

}  // namespace piconet_test

#endif  // _WIN32
