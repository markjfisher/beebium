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

#ifdef _WIN32

#include "Win32NamedPipePair.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <iostream>

namespace piconet_test {

namespace {

// Per-process monotonic counter so parallel tests in the same binary get
// distinct pipe names.
std::atomic<unsigned> g_pipe_counter{0};

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int needed = ::MultiByteToWideChar(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

}  // namespace

Win32NamedPipePair::Win32NamedPipePair() {
    char name_buf[128];
    unsigned seq = g_pipe_counter.fetch_add(1, std::memory_order_relaxed);
    std::snprintf(name_buf, sizeof(name_buf),
                  "\\\\.\\pipe\\beebium-piconet-test-%lu-%u",
                  static_cast<unsigned long>(::GetCurrentProcessId()), seq);
    pipe_name_ = name_buf;

    std::wstring wide = utf8_to_wide(pipe_name_);
    HANDLE server = ::CreateNamedPipeW(
        wide.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,                  // nMaxInstances: one server, one client
        4096, 4096,
        0,                  // default timeout (unused; we pass our own)
        nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        std::cerr << "Win32NamedPipePair: CreateNamedPipeW failed, err="
                  << ::GetLastError() << "\n";
        return;
    }

    HANDLE connect_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE read_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE write_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!connect_event || !read_event || !write_event) {
        std::cerr << "Win32NamedPipePair: CreateEvent failed, err="
                  << ::GetLastError() << "\n";
        ::CloseHandle(server);
        if (connect_event) ::CloseHandle(connect_event);
        if (read_event) ::CloseHandle(read_event);
        if (write_event) ::CloseHandle(write_event);
        return;
    }

    // Arm the ConnectNamedPipe so the pipe is ready for the client to
    // open without racing.
    OVERLAPPED connect_ov{};
    connect_ov.hEvent = connect_event;
    BOOL connected_sync = ::ConnectNamedPipe(server, &connect_ov);
    if (connected_sync) {
        // Immediate success (rare for OVERLAPPED).
        ::SetEvent(connect_event);
        connect_pending_ = false;
    } else {
        DWORD err = ::GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            // Client already opened between CreateNamedPipe and
            // ConnectNamedPipe. Signal the event so wait_for_client can
            // observe completion.
            ::SetEvent(connect_event);
            connect_pending_ = false;
        } else if (err == ERROR_IO_PENDING) {
            connect_pending_ = true;
        } else {
            std::cerr << "Win32NamedPipePair: ConnectNamedPipe failed, err="
                      << err << "\n";
            ::CloseHandle(server);
            ::CloseHandle(connect_event);
            ::CloseHandle(read_event);
            ::CloseHandle(write_event);
            return;
        }
    }

    server_handle_raw_ = reinterpret_cast<std::uintptr_t>(server);
    connect_event_raw_ = reinterpret_cast<std::uintptr_t>(connect_event);
    read_event_raw_ = reinterpret_cast<std::uintptr_t>(read_event);
    write_event_raw_ = reinterpret_cast<std::uintptr_t>(write_event);
}

Win32NamedPipePair::~Win32NamedPipePair() {
    if (server_handle_raw_ != 0) {
        HANDLE server = reinterpret_cast<HANDLE>(server_handle_raw_);
        ::DisconnectNamedPipe(server);
        ::CloseHandle(server);
    }
    if (connect_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(connect_event_raw_));
    }
    if (read_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(read_event_raw_));
    }
    if (write_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(write_event_raw_));
    }
}

bool Win32NamedPipePair::is_open() const {
    return server_handle_raw_ != 0;
}

bool Win32NamedPipePair::wait_for_client(int timeout_ms) {
    if (!is_open()) return false;
    HANDLE event = reinterpret_cast<HANDLE>(connect_event_raw_);
    DWORD waited = ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
    if (waited != WAIT_OBJECT_0) {
        return false;
    }
    if (connect_pending_) {
        // Drain the OVERLAPPED completion cleanly.
        HANDLE server = reinterpret_cast<HANDLE>(server_handle_raw_);
        OVERLAPPED dummy{};
        dummy.hEvent = event;
        DWORD ignored = 0;
        ::GetOverlappedResult(server, &dummy, &ignored, FALSE);
        connect_pending_ = false;
    }
    return true;
}

bool Win32NamedPipePair::server_write(std::span<const std::uint8_t> bytes,
                                      int timeout_ms) {
    if (!is_open()) return false;
    HANDLE server = reinterpret_cast<HANDLE>(server_handle_raw_);
    HANDLE event = reinterpret_cast<HANDLE>(write_event_raw_);
    std::size_t total = 0;
    while (total < bytes.size()) {
        OVERLAPPED ov{};
        ::ResetEvent(event);
        ov.hEvent = event;
        DWORD chunk = static_cast<DWORD>(bytes.size() - total);
        DWORD written = 0;
        BOOL ok = ::WriteFile(server, bytes.data() + total, chunk,
                              &written, &ov);
        if (!ok) {
            DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return false;
            }
            DWORD waited = ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
            if (waited != WAIT_OBJECT_0) {
                ::CancelIoEx(server, &ov);
                ::GetOverlappedResult(server, &ov, &written, TRUE);
                return false;
            }
            if (!::GetOverlappedResult(server, &ov, &written, TRUE)) {
                return false;
            }
        }
        if (written == 0) return false;
        total += static_cast<std::size_t>(written);
    }
    return true;
}

int Win32NamedPipePair::server_read(std::span<std::uint8_t> buffer,
                                    int timeout_ms) {
    if (!is_open()) return -1;
    HANDLE server = reinterpret_cast<HANDLE>(server_handle_raw_);
    HANDLE event = reinterpret_cast<HANDLE>(read_event_raw_);
    OVERLAPPED ov{};
    ::ResetEvent(event);
    ov.hEvent = event;
    DWORD bytes = 0;
    BOOL ok = ::ReadFile(server, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &bytes, &ov);
    if (ok) {
        return static_cast<int>(bytes);
    }
    DWORD err = ::GetLastError();
    if (err != ERROR_IO_PENDING) {
        return -1;
    }
    DWORD waited = ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
    if (waited != WAIT_OBJECT_0) {
        ::CancelIoEx(server, &ov);
        ::GetOverlappedResult(server, &ov, &bytes, TRUE);
        return 0;
    }
    if (!::GetOverlappedResult(server, &ov, &bytes, TRUE)) {
        return -1;
    }
    return static_cast<int>(bytes);
}

void Win32NamedPipePair::disconnect() {
    if (server_handle_raw_ == 0) return;
    HANDLE server = reinterpret_cast<HANDLE>(server_handle_raw_);
    ::DisconnectNamedPipe(server);
}

}  // namespace piconet_test

#endif  // _WIN32
