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
#include "piconet/Win32NamedPipePair.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace beebium::piconet;
using piconet_test::Win32NamedPipePair;

namespace {

constexpr int kPipeTimeoutMs = 2000;

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
    Win32NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.pipe_name());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    CHECK(port.device_path() == pair.pipe_name());
}

TEST_CASE("Win32SerialPort read with no data returns would_block within ~100ms",
          "[piconet][serial][win32]") {
    Win32NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.pipe_name());
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
    Win32NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.pipe_name());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    const std::uint8_t payload[] = {'P', 'I', 'C', 'O', '\n'};
    REQUIRE(pair.server_write({payload, sizeof(payload)}, kPipeTimeoutMs));

    std::array<std::uint8_t, 32> buf{};
    // Poll a few times because the pipe write may take a tick to surface.
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
    Win32NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.pipe_name());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    const std::uint8_t payload[] = {'S', 'T', 'A', 'T', 'U', 'S', '\r'};
    auto wr = port.write({payload, sizeof(payload)});
    CHECK_FALSE(wr.error);
    CHECK(wr.bytes == sizeof(payload));

    std::array<std::uint8_t, 32> buf{};
    int n = pair.server_read({buf.data(), buf.size()}, kPipeTimeoutMs);
    REQUIRE(n == static_cast<int>(sizeof(payload)));
    for (std::size_t i = 0; i < sizeof(payload); ++i) {
        CHECK(buf[i] == payload[i]);
    }
}

TEST_CASE("Win32SerialPort::close wakes a blocked reader quickly",
          "[piconet][serial][win32]") {
    Win32NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.pipe_name());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    std::atomic<bool> reader_returned{false};
    std::atomic<bool> reader_saw_error{false};
    std::thread reader([&]() {
        std::array<std::uint8_t, 16> buf{};
        // Loop briefly so close() must be what makes read() return error
        // (not just a 100ms timeout).
        for (int i = 0; i < 200; ++i) {
            auto r = port.read({buf.data(), buf.size()});
            if (r.error) {
                reader_saw_error.store(true);
                break;
            }
            if (!port.is_open()) break;
        }
        reader_returned.store(true);
    });

    // Give the reader a moment to block inside read().
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start = std::chrono::steady_clock::now();
    port.close();
    // Wait bounded to catch the "close should wake the reader promptly"
    // property; the reader's own 100ms timeout would also work but we
    // want to see it faster when CancelIoEx fires.
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
    Win32NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.pipe_name());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    port.close();
    port.close();  // Must not crash or double-close the handle.
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
    Win32NamedPipePair pair;
    REQUIRE(pair.is_open());

    Win32SerialPort port(pair.pipe_name());
    REQUIRE(port.is_open());
    REQUIRE(pair.wait_for_client(kPipeTimeoutMs));

    pair.disconnect();

    // The client end will eventually surface ERROR_BROKEN_PIPE on the
    // next read. Loop a few times because the driver takes a tick to
    // propagate the disconnect through the handle.
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
