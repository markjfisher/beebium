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

// FakePiconetDevice exposed via a Win32 named pipe. Bytes flow in both
// directions through a single pumper thread using OVERLAPPED I/O on the
// pipe server handle.
//
// This is the Windows analogue of FakePiconetDeviceOnPty -- tests that
// want to exercise the full Win32 I/O stack (PiconetBackend <->
// Win32SerialPort <-> kernel pipe <-> pumper thread <-> FakePiconetDevice)
// use this instead of FakePiconetDeviceOnPty.

#ifdef _WIN32

#include "FakePiconetDevice.hpp"
#include "NamedPipePair.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

namespace beebium::piconet::test {

class FakePiconetDeviceOnPipe {
public:
    FakePiconetDeviceOnPipe();
    ~FakePiconetDeviceOnPipe();

    FakePiconetDeviceOnPipe(const FakePiconetDeviceOnPipe&) = delete;
    FakePiconetDeviceOnPipe& operator=(const FakePiconetDeviceOnPipe&) = delete;

    bool is_open() const { return pipe_.is_open(); }

    // Windows-namespace pipe path (e.g. "\\\\.\\pipe\\beebium-piconet-test-...").
    // PiconetBackend opens this via Win32SerialPort. Named slave_path() for
    // drop-in substitution with FakePiconetDeviceOnPty's API.
    const std::string& slave_path() const { return pipe_.slave_path(); }

    FakePiconetDevice& device() { return fake_; }

private:
    void pumper_loop();

    NamedPipePair pipe_;
    FakePiconetDevice fake_;
    std::atomic<bool> shutdown_{false};
    std::thread pumper_;
    std::uintptr_t read_event_raw_{0};
    std::uintptr_t write_event_raw_{0};
};

}  // namespace beebium::piconet::test

#endif  // _WIN32
