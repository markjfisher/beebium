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

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/piconet/Win32SerialPort.hpp"
#include "piconet/NamedPipePair.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <thread>

using namespace beebium::piconet;
using beebium::piconet::test::NamedPipePair;

namespace {

constexpr int kPipeTimeoutMs = 2000;

// Small helpers for synchronous OVERLAPPED I/O on the pipe server side
// (mirrors what FakePiconetDeviceOnPipe's pumper does, but inlined for
// the low-level tests here).
bool server_write(HANDLE server, std::span<const std::uint8_t> bytes,
                  int timeout_ms) {
    HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return false;
    OVERLAPPED ov{};
    ov.hEvent = event;
    DWORD written = 0;
    BOOL ok = ::WriteFile(server, bytes.data(),
                          static_cast<DWORD>(bytes.size()), &written, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING) {
            ::CloseHandle(event);
            return false;
        }
        DWORD waited = ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
        if (waited != WAIT_OBJECT_0 ||
            !::GetOverlappedResult(server, &ov, &written, TRUE)) {
            ::CancelIoEx(server, &ov);
            ::CloseHandle(event);
            return false;
        }
    }
    ::CloseHandle(event);
    return written == bytes.size();
}

int server_read(HANDLE server, std::span<std::uint8_t> buffer,
                int timeout_ms) {
    HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return -1;
    OVERLAPPED ov{};
    ov.hEvent = event;
    DWORD bytes = 0;
    BOOL ok = ::ReadFile(server, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &bytes, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING) {
            ::CloseHandle(event);
            return -1;
        }
        DWORD waited = ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
        if (waited != WAIT_OBJECT_0) {
            ::CancelIoEx(server, &ov);
            ::GetOverlappedResult(server, &ov, &bytes, TRUE);
            ::CloseHandle(event);
            return 0;
        }
        if (!::GetOverlappedResult(server, &ov, &bytes, TRUE)) {
            ::CloseHandle(event);
            return -1;
        }
    }
    ::CloseHandle(event);
    return static_cast<int>(bytes);
}

}  // namespace

TEST_CASE("Win32SerialPort open of nonexistent COM port leaves is_open false",
          "[piconet][serial][win32]") {
    // COM255 is effectively guaranteed absent on CI runners and dev boxes.
    Win32SerialPort port("COM255");
    CHECK_FALSE(port.is_open());
    CHECK_FALSE(port.open_error().empty());
}

TEST_CASE("Win32SerialPort opens a named pipe endpoint (graceful non-COM configure)",
          "[piconet][serial][win32]") {
    NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.slave_path());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    CHECK(port.device_path() == pair.slave_path());
}

TEST_CASE("Win32SerialPort read with no data returns would_block within ~100ms",
          "[piconet][serial][win32]") {
    NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.slave_path());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    std::array<std::uint8_t, 16> buf{};
    auto start = std::chrono::steady_clock::now();
    auto result = port.read({buf.data(), buf.size()});
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(result.would_block);
    CHECK_FALSE(result.error);
    CHECK(result.bytes == 0);
    // 100ms internal timeout + generous CI slack.
    CHECK(elapsed < std::chrono::milliseconds(300));
}

TEST_CASE("Win32SerialPort reads bytes the pipe server wrote",
          "[piconet][serial][win32]") {
    NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.slave_path());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    HANDLE server = reinterpret_cast<HANDLE>(pair.server_handle_raw());
    const std::uint8_t payload[] = {'P', 'I', 'C', 'O', '\n'};
    REQUIRE(server_write(server, {payload, sizeof(payload)}, kPipeTimeoutMs));

    std::array<std::uint8_t, 32> buf{};
    std::size_t got = 0;
    for (int attempt = 0; attempt < 20 && got < sizeof(payload); ++attempt) {
        auto r = port.read({buf.data() + got, buf.size() - got});
        if (r.error) FAIL("Win32SerialPort::read returned error mid-test");
        got += r.bytes;
    }
    REQUIRE(got == sizeof(payload));
    for (std::size_t i = 0; i < sizeof(payload); ++i) {
        CHECK(buf[i] == payload[i]);
    }
}

TEST_CASE("Win32SerialPort write is observed on the pipe server side",
          "[piconet][serial][win32]") {
    NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.slave_path());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    HANDLE server = reinterpret_cast<HANDLE>(pair.server_handle_raw());
    const std::uint8_t payload[] = {'S', 'T', 'A', 'T', 'U', 'S', '\r'};
    auto wr = port.write({payload, sizeof(payload)});
    CHECK_FALSE(wr.error);
    CHECK(wr.bytes == sizeof(payload));

    std::array<std::uint8_t, 32> buf{};
    int n = server_read(server, {buf.data(), buf.size()}, kPipeTimeoutMs);
    REQUIRE(n == static_cast<int>(sizeof(payload)));
    for (std::size_t i = 0; i < sizeof(payload); ++i) {
        CHECK(buf[i] == payload[i]);
    }
}

TEST_CASE("Win32SerialPort::close wakes a blocked reader quickly",
          "[piconet][serial][win32]") {
    NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.slave_path());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    std::atomic<bool> reader_returned{false};
    std::atomic<bool> reader_saw_error{false};
    std::thread reader([&]() {
        std::array<std::uint8_t, 16> buf{};
        // Loop until read() signals error. After close() exchanges the
        // handle, the reader's next iteration loads kInvalidHandle and
        // returns error=true; until then successive 100ms timeouts
        // harmlessly tick. Cap at 50 iterations (~5s worst case) so a
        // misbehaving close() cannot hang the test forever.
        for (int i = 0; i < 50; ++i) {
            auto r = port.read({buf.data(), buf.size()});
            if (r.error) {
                reader_saw_error.store(true);
                break;
            }
        }
        reader_returned.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start = std::chrono::steady_clock::now();
    port.close();
    for (int i = 0; i < 50 && !reader_returned.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    reader.join();

    CHECK(reader_returned.load());
    CHECK(reader_saw_error.load());
    CHECK_FALSE(port.is_open());
    CHECK(elapsed < std::chrono::milliseconds(500));
}

TEST_CASE("Win32SerialPort::close is idempotent", "[piconet][serial][win32]") {
    NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.slave_path());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    port.close();
    port.close();
    CHECK_FALSE(port.is_open());

    std::array<std::uint8_t, 4> buf{};
    auto rr = port.read({buf.data(), buf.size()});
    CHECK(rr.error);

    const std::uint8_t b = 'x';
    auto wr = port.write({&b, 1});
    CHECK(wr.error);
}

TEST_CASE("Win32SerialPort reports error when the pipe peer disconnects",
          "[piconet][serial][win32]") {
    NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.slave_path());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    HANDLE server = reinterpret_cast<HANDLE>(pair.server_handle_raw());
    ::DisconnectNamedPipe(server);

    bool saw_error = false;
    std::array<std::uint8_t, 16> buf{};
    for (int i = 0; i < 20; ++i) {
        auto r = port.read({buf.data(), buf.size()});
        if (r.error) {
            saw_error = true;
            break;
        }
    }
    CHECK(saw_error);
}

#endif  // _WIN32
