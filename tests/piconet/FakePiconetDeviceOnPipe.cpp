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

#include "FakePiconetDeviceOnPipe.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <span>

namespace beebium::piconet::test {

FakePiconetDeviceOnPipe::FakePiconetDeviceOnPipe() {
    if (!pipe_.is_open()) {
        return;
    }
    HANDLE read_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE write_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!read_event || !write_event) {
        std::cerr << "FakePiconetDeviceOnPipe: CreateEventW failed, err="
                  << ::GetLastError() << "\n";
        if (read_event) ::CloseHandle(read_event);
        if (write_event) ::CloseHandle(write_event);
        return;
    }
    read_event_raw_ = reinterpret_cast<std::uintptr_t>(read_event);
    write_event_raw_ = reinterpret_cast<std::uintptr_t>(write_event);
    pumper_ = std::thread([this] { pumper_loop(); });
}

FakePiconetDeviceOnPipe::~FakePiconetDeviceOnPipe() {
    shutdown_.store(true, std::memory_order_relaxed);
    // Cancel any pending I/O so the pumper thread wakes up promptly.
    HANDLE server = reinterpret_cast<HANDLE>(pipe_.server_handle_raw());
    if (server) {
        ::CancelIoEx(server, nullptr);
    }
    if (pumper_.joinable()) {
        pumper_.join();
    }
    if (read_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(read_event_raw_));
    }
    if (write_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(write_event_raw_));
    }
    // pipe_ destructor disconnects + closes the server handle.
}

void FakePiconetDeviceOnPipe::pumper_loop() {
    HANDLE server = reinterpret_cast<HANDLE>(pipe_.server_handle_raw());
    HANDLE read_event = reinterpret_cast<HANDLE>(read_event_raw_);
    HANDLE write_event = reinterpret_cast<HANDLE>(write_event_raw_);

    std::array<std::uint8_t, 256> buf{};

    // Block up to a couple of seconds waiting for Win32SerialPort on the
    // client end to connect. If the test tears us down before connecting,
    // we'll observe shutdown_ and fall through.
    constexpr int kConnectTimeoutMs = 3000;
    pipe_.wait_for_client(kConnectTimeoutMs);

    while (!shutdown_.load(std::memory_order_relaxed)) {
        // Direction A: client -> fake. Commands written by PiconetBackend
        // via Win32SerialPort arrive on the pipe server side; forward
        // them into the fake's write() method.
        OVERLAPPED read_ov{};
        ::ResetEvent(read_event);
        read_ov.hEvent = read_event;
        DWORD bytes = 0;
        BOOL ok = ::ReadFile(server, buf.data(),
                             static_cast<DWORD>(buf.size()), &bytes, &read_ov);
        DWORD read_completed_bytes = 0;
        if (ok) {
            read_completed_bytes = bytes;
        } else {
            DWORD err = ::GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD waited = ::WaitForSingleObject(read_event, 50);
                if (waited == WAIT_TIMEOUT) {
                    ::CancelIoEx(server, &read_ov);
                }
                DWORD ov_bytes = 0;
                if (::GetOverlappedResult(server, &read_ov, &ov_bytes, TRUE)) {
                    read_completed_bytes = ov_bytes;
                } else {
                    DWORD oerr = ::GetLastError();
                    if (oerr != ERROR_OPERATION_ABORTED) {
                        // Pipe broken or client closed: pumper exits.
                        return;
                    }
                    // Our own cancel; drain partial bytes if any.
                    read_completed_bytes = ov_bytes;
                }
            } else if (err == ERROR_BROKEN_PIPE ||
                       err == ERROR_PIPE_NOT_CONNECTED) {
                return;
            } else {
                std::cerr << "FakePiconetDeviceOnPipe: ReadFile err=" << err << "\n";
                return;
            }
        }

        if (read_completed_bytes > 0) {
            fake_.write(std::span<const std::uint8_t>{
                buf.data(), static_cast<std::size_t>(read_completed_bytes)});
        }

        // Direction B: fake -> client. Drain any events the fake has
        // queued and push them onto the pipe.
        auto rr = fake_.read({buf.data(), buf.size()});
        if (rr.error) return;
        if (rr.bytes > 0) {
            std::size_t total = 0;
            while (total < rr.bytes) {
                OVERLAPPED write_ov{};
                ::ResetEvent(write_event);
                write_ov.hEvent = write_event;
                DWORD chunk = static_cast<DWORD>(rr.bytes - total);
                DWORD written = 0;
                BOOL wok = ::WriteFile(server, buf.data() + total, chunk,
                                       &written, &write_ov);
                if (!wok) {
                    DWORD werr = ::GetLastError();
                    if (werr != ERROR_IO_PENDING) {
                        return;
                    }
                    DWORD waited = ::WaitForSingleObject(write_event, 500);
                    if (waited != WAIT_OBJECT_0) {
                        ::CancelIoEx(server, &write_ov);
                        ::GetOverlappedResult(server, &write_ov, &written, TRUE);
                        return;
                    }
                    if (!::GetOverlappedResult(server, &write_ov, &written, TRUE)) {
                        return;
                    }
                }
                if (written == 0) return;
                total += static_cast<std::size_t>(written);
            }
        }

        // If neither direction produced bytes this tick, take a brief
        // sleep so we don't busy-spin. This mirrors the 5ms idle wait
        // in FakePiconetDeviceOnPty's pumper.
        if (read_completed_bytes == 0 && rr.would_block) {
            ::Sleep(5);
        }
    }
}

}  // namespace beebium::piconet::test

#endif  // _WIN32
