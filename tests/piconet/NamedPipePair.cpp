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

#include "NamedPipePair.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <iostream>

namespace beebium::piconet::test {

namespace {

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

NamedPipePair::NamedPipePair() {
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
        1,
        4096, 4096,
        0,
        nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        std::cerr << "NamedPipePair: CreateNamedPipeW failed, err="
                  << ::GetLastError() << "\n";
        return;
    }

    HANDLE connect_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!connect_event) {
        std::cerr << "NamedPipePair: CreateEventW failed, err="
                  << ::GetLastError() << "\n";
        ::CloseHandle(server);
        return;
    }

    OVERLAPPED connect_ov{};
    connect_ov.hEvent = connect_event;
    BOOL connected_sync = ::ConnectNamedPipe(server, &connect_ov);
    if (connected_sync) {
        ::SetEvent(connect_event);
        connect_pending_ = false;
    } else {
        DWORD err = ::GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            ::SetEvent(connect_event);
            connect_pending_ = false;
        } else if (err == ERROR_IO_PENDING) {
            connect_pending_ = true;
        } else {
            std::cerr << "NamedPipePair: ConnectNamedPipe failed, err="
                      << err << "\n";
            ::CloseHandle(server);
            ::CloseHandle(connect_event);
            return;
        }
    }

    server_handle_raw_ = reinterpret_cast<std::uintptr_t>(server);
    connect_event_raw_ = reinterpret_cast<std::uintptr_t>(connect_event);
}

NamedPipePair::~NamedPipePair() {
    if (server_handle_raw_ != 0) {
        HANDLE server = reinterpret_cast<HANDLE>(server_handle_raw_);
        ::DisconnectNamedPipe(server);
        ::CloseHandle(server);
    }
    if (connect_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(connect_event_raw_));
    }
}

bool NamedPipePair::wait_for_client(int timeout_ms) {
    if (!is_open()) return false;
    HANDLE event = reinterpret_cast<HANDLE>(connect_event_raw_);
    DWORD waited = ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
    if (waited != WAIT_OBJECT_0) return false;
    if (connect_pending_) {
        HANDLE server = reinterpret_cast<HANDLE>(server_handle_raw_);
        OVERLAPPED dummy{};
        dummy.hEvent = event;
        DWORD ignored = 0;
        ::GetOverlappedResult(server, &dummy, &ignored, FALSE);
        connect_pending_ = false;
    }
    return true;
}

}  // namespace beebium::piconet::test

#endif  // _WIN32
