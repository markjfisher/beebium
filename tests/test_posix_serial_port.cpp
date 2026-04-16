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

#ifndef _WIN32

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/piconet/PosixSerialPort.hpp"

#include <array>
#include <chrono>
#include <cstdint>

using namespace beebium::piconet;

TEST_CASE("PosixSerialPort opens /dev/null and reports is_open", "[piconet][serial]") {
    PosixSerialPort port("/dev/null");
    CHECK(port.is_open());
    CHECK(port.device_path() == "/dev/null");
}

TEST_CASE("PosixSerialPort open of nonexistent path leaves is_open false",
          "[piconet][serial]") {
    PosixSerialPort port("/no/such/device/path/exists");
    CHECK_FALSE(port.is_open());
}

TEST_CASE("PosixSerialPort read on /dev/null returns EOF (error)",
          "[piconet][serial]") {
    // /dev/null read returns EOF immediately. Our wrapper maps EOF to
    // error=true so the reader thread can exit (vs. would_block which
    // would loop indefinitely).
    PosixSerialPort port("/dev/null");
    REQUIRE(port.is_open());
    std::array<std::uint8_t, 16> buf{};
    auto result = port.read({buf.data(), buf.size()});
    CHECK(result.bytes == 0);
    CHECK(result.error);
}

TEST_CASE("PosixSerialPort read on /dev/zero returns bytes (not would_block)",
          "[piconet][serial]") {
    PosixSerialPort port("/dev/zero");
    REQUIRE(port.is_open());
    std::array<std::uint8_t, 8> buf{};
    auto result = port.read({buf.data(), buf.size()});
    CHECK(result.bytes == buf.size());
    CHECK_FALSE(result.error);
    CHECK_FALSE(result.would_block);
    for (auto b : buf) CHECK(b == 0x00);
}

TEST_CASE("PosixSerialPort write to /dev/null reports the requested byte count",
          "[piconet][serial]") {
    PosixSerialPort port("/dev/null");
    REQUIRE(port.is_open());
    const std::uint8_t payload[] = {'h', 'i', '\n'};
    auto result = port.write({payload, sizeof(payload)});
    CHECK(result.bytes == sizeof(payload));
    CHECK_FALSE(result.error);
}

TEST_CASE("PosixSerialPort close transitions is_open to false; subsequent ops fail",
          "[piconet][serial]") {
    PosixSerialPort port("/dev/null");
    REQUIRE(port.is_open());
    port.close();
    CHECK_FALSE(port.is_open());

    std::array<std::uint8_t, 4> buf{};
    auto rr = port.read({buf.data(), buf.size()});
    CHECK(rr.error);

    const std::uint8_t b = 'x';
    auto wr = port.write({&b, 1});
    CHECK(wr.error);
}

TEST_CASE("PosixSerialPort close is idempotent", "[piconet][serial]") {
    PosixSerialPort port("/dev/null");
    port.close();
    port.close();  // Must not double-close the fd.
    CHECK_FALSE(port.is_open());
}

TEST_CASE("PosixSerialPort read on a tty-less device with no data returns within timeout",
          "[piconet][serial]") {
    // /dev/null returns EOF; this is mostly a sanity check that read()
    // does not hang. The 100ms internal select timeout bounds the wait.
    PosixSerialPort port("/dev/null");
    REQUIRE(port.is_open());
    std::array<std::uint8_t, 4> buf{};

    auto start = std::chrono::steady_clock::now();
    port.read({buf.data(), buf.size()});
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should be near-instant for /dev/null EOF; allow generous slack
    // for CI noise.
    CHECK(elapsed < std::chrono::milliseconds(150));
}

#endif  // !_WIN32
