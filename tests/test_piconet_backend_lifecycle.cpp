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

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/PiconetBackend.hpp"

#include "piconet/MockPiconetSerial.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

using namespace beebium;
using namespace beebium::piconet;
using beebium::piconet::test::MockPiconetSerial;

TEST_CASE("PiconetBackend constructor sends SET_STATION then SET_MODE LISTEN in order",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/null", /*initial_station=*/32},
                           std::move(mock_owner));

    REQUIRE(mock->write_count() == 2);
    CHECK(mock->write_as_string(0) == "SET_STATION 32\r");
    CHECK(mock->write_as_string(1) == "SET_MODE LISTEN\r");
}

TEST_CASE("PiconetBackend constructor with a closed port does not start the reader thread",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    mock->set_open(false);  // Simulate device that failed to open.
    PiconetBackend backend(PiconetConfig{"/dev/null", 32},
                           std::move(mock_owner));

    CHECK_FALSE(backend.is_connected());
    // No SET_STATION / SET_MODE writes should have been attempted.
    CHECK(mock->write_count() == 0);
    // Receive returns nullopt; reader thread is not running.
    CHECK_FALSE(backend.receive_frame().has_value());
}

TEST_CASE("PiconetBackend on_station_id_changed re-issues SET_STATION",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/null", 32}, std::move(mock_owner));
    mock->clear_writes();  // Forget the construction-time handshake.

    backend.on_station_id_changed(42);
    REQUIRE(mock->write_count() == 1);
    CHECK(mock->write_as_string(0) == "SET_STATION 42\r");

    backend.on_station_id_changed(254);
    REQUIRE(mock->write_count() == 2);
    CHECK(mock->write_as_string(1) == "SET_STATION 254\r");
}

TEST_CASE("PiconetBackend on_station_id_changed is a no-op after the port closes",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/null", 32}, std::move(mock_owner));
    mock->clear_writes();
    mock->close();
    backend.on_station_id_changed(99);
    CHECK(mock->write_count() == 0);
}

TEST_CASE("PiconetBackend destructor joins the reader thread within 200ms",
          "[piconet][backend][lifecycle]") {
    // No incoming bytes are staged. The reader's read() call will block
    // (returning would_block) for up to its internal timeout. The
    // destructor must close the serial port to unblock the reader and
    // join promptly. Without timed reads or a close-driven exit, this
    // test would hang.
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto backend = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", 32}, std::move(mock_owner));

    auto start = std::chrono::steady_clock::now();
    backend.reset();  // Triggers destructor.
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed < std::chrono::milliseconds(200));
}

TEST_CASE("PiconetBackend can be constructed and destructed many times in a row",
          "[piconet][backend][lifecycle]") {
    // Smoke test for thread leakage / double-close. If destructor cleanup
    // were buggy, this would either hang or surface a thread sanitiser
    // error.
    for (int i = 0; i < 5; ++i) {
        auto mock_owner = std::make_unique<MockPiconetSerial>();
        PiconetBackend backend(PiconetConfig{"/dev/null", 32},
                               std::move(mock_owner));
        // Just construct and destruct.
    }
    SUCCEED("constructed and destructed 5 times without hang or crash");
}
