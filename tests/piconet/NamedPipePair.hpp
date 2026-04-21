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

// Win32 equivalent of PtyPair: a one-instance bidirectional named pipe
// that stands in for a real COM port at the other end of Win32SerialPort.
//
// The server handle lives with this object (the pumper thread of
// FakePiconetDeviceOnPipe reads/writes it, or test code pokes bytes
// directly into it); the client side is opened by Win32SerialPort via
// CreateFile on slave_path(). That path starts with "\\.\pipe\..." so
// Win32SerialPort::normalise_com_path passes it through untouched.
//
// Windows-only -- see PtyPair.hpp for the POSIX analogue.

#ifdef _WIN32

#include <cstdint>
#include <string>

namespace beebium::piconet::test {

class NamedPipePair {
public:
    // Creates the pipe server (CreateNamedPipeW with FILE_FLAG_OVERLAPPED)
    // and arms ConnectNamedPipe so the client side can connect as soon as
    // Win32SerialPort opens slave_path(). On failure (unusual) is_open()
    // returns false and a diagnostic is written to stderr; constructor
    // does not throw.
    NamedPipePair();

    ~NamedPipePair();

    NamedPipePair(const NamedPipePair&) = delete;
    NamedPipePair& operator=(const NamedPipePair&) = delete;

    bool is_open() const { return server_handle_raw_ != 0; }

    // Windows-namespace pipe path for the client side, e.g.
    // "\\\\.\\pipe\\beebium-piconet-test-12345-0". Named slave_path() to
    // match PtyPair's API so the two platform implementations are drop-in
    // substitutes for each other behind a typedef.
    const std::string& slave_path() const { return pipe_name_; }

    // The pipe's server-side HANDLE as a raw uintptr_t. Callers
    // reinterpret_cast to HANDLE; keeping it typeless here avoids
    // pulling <windows.h> into this header.
    std::uintptr_t server_handle_raw() const { return server_handle_raw_; }

    // Blocks up to timeout_ms waiting for Win32SerialPort on the client
    // end to finish connecting. Returns true on success. Callers should
    // invoke this once right after constructing the client-side
    // Win32SerialPort and before exercising I/O.
    bool wait_for_client(int timeout_ms);

private:
    std::string pipe_name_;
    std::uintptr_t server_handle_raw_{0};
    std::uintptr_t connect_event_raw_{0};
    bool connect_pending_{false};
};

}  // namespace beebium::piconet::test

#endif  // _WIN32
