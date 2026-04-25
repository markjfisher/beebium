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

TEST_CASE("PiconetBackend::set_mode bumps backend_status_sequence on Listen <-> non-Listen transitions",
          "[piconet][backend][lifecycle]") {
    // is_connected() reflects (serial_open AND current_mode_ == Listen), so
    // any Listen <-> non-Listen change is visible to EconetService clients
    // via WatchEconetStatus. The poll loop pushes on EconetSocket::status_sequence()
    // advancing, which folds in NetworkBackend::backend_status_sequence().
    // Without the bump, WatchEconetStatus misses the change and macOS
    // "Disable" leaves the header stuck on "Connected" (github issue #35).
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/null", 32}, std::move(mock_owner));
    REQUIRE(backend.is_connected());
    const auto seq_after_ctor = backend.backend_status_sequence();

    // Listen -> Stop: is_connected() flips, sequence must advance.
    backend.set_mode(piconet::Mode::Stop);
    CHECK_FALSE(backend.is_connected());
    CHECK(backend.backend_status_sequence() > seq_after_ctor);

    // Stop -> Stop: no change in is_connected(), no bump.
    const auto seq_after_stop = backend.backend_status_sequence();
    backend.set_mode(piconet::Mode::Stop);
    CHECK(backend.backend_status_sequence() == seq_after_stop);

    // Stop -> Listen: flips back, sequence advances again.
    backend.set_mode(piconet::Mode::Listen);
    CHECK(backend.is_connected());
    CHECK(backend.backend_status_sequence() > seq_after_stop);

    // Listen -> Monitor: both non-Listen from is_connected's perspective is
    // coming, but Listen -> Monitor is still a Listen <-> non-Listen transition.
    const auto seq_after_listen = backend.backend_status_sequence();
    backend.set_mode(piconet::Mode::Monitor);
    CHECK_FALSE(backend.is_connected());
    CHECK(backend.backend_status_sequence() > seq_after_listen);

    // Monitor -> Stop: both non-Listen. is_connected() stays false, no bump.
    const auto seq_after_monitor = backend.backend_status_sequence();
    backend.set_mode(piconet::Mode::Stop);
    CHECK(backend.backend_status_sequence() == seq_after_monitor);
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

TEST_CASE("PiconetBackend::request_reopen swaps serial and sends SET_STATION + SET_MODE STOP",
          "[piconet][backend][lifecycle][reopen]") {
    // Keep a pointer to the replacement mock so the test can inspect the
    // writes issued on the reopen path.
    MockPiconetSerial* replacement = nullptr;
    auto factory = [&](const std::string& /*path*/)
        -> std::unique_ptr<SerialPort> {
        auto mock = std::make_unique<MockPiconetSerial>();
        replacement = mock.get();
        return mock;
    };

    auto initial_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/old", /*initial_station=*/64},
                           std::move(initial_owner),
                           factory);

    REQUIRE(backend.config().device_path == "/dev/old");

    backend.request_reopen("/dev/new");
    // Reopen happens at the top of receive_frame(); drive one tick.
    (void)backend.receive_frame();

    REQUIRE(backend.config().device_path == "/dev/new");
    REQUIRE(backend.open_error_message().empty());
    REQUIRE(backend.is_serial_open());
    REQUIRE(backend.mode() == piconet::Mode::Stop);

    // Replacement mock should have seen SET_STATION <initial_station>
    // followed by SET_MODE STOP. The factory's mock is freshly created,
    // so write_count reflects only the reopen-time writes.
    REQUIRE(replacement != nullptr);
    REQUIRE(replacement->write_count() == 2);
    CHECK(replacement->write_as_string(0) == "SET_STATION 64\r");
    CHECK(replacement->write_as_string(1) == "SET_MODE STOP\r");
}

TEST_CASE("PiconetBackend::request_reopen records open_error_message on factory-returned closed port",
          "[piconet][backend][lifecycle][reopen]") {
    auto factory = [](const std::string& /*path*/)
        -> std::unique_ptr<SerialPort> {
        auto mock = std::make_unique<MockPiconetSerial>();
        mock->set_open(false);  // Simulate "open failed" at the OS level.
        return mock;
    };

    auto initial_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/old", 32},
                           std::move(initial_owner),
                           factory);

    backend.request_reopen("/dev/absent");
    (void)backend.receive_frame();

    CHECK(backend.config().device_path == "/dev/absent");
    CHECK_FALSE(backend.is_serial_open());
    CHECK_FALSE(backend.open_error_message().empty());
    // MockPiconetSerial does not supply an OS error, so the backend
    // falls back to the "unknown error" placeholder.
    CHECK(backend.open_error_message() == "unknown error");
}

TEST_CASE("PiconetBackend::request_reopen is a no-op without a SerialFactory",
          "[piconet][backend][lifecycle][reopen]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* initial_mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/old", 32},
                           std::move(mock_owner));
    const auto initial_write_count = initial_mock->write_count();

    backend.request_reopen("/dev/new");
    (void)backend.receive_frame();

    // Device path stays unchanged; no writes against the initial serial
    // above what the constructor already produced.
    CHECK(backend.config().device_path == "/dev/old");
    CHECK(initial_mock->write_count() == initial_write_count);
}

TEST_CASE("PiconetBackend::request_reopen coalesces successive requests",
          "[piconet][backend][lifecycle][reopen]") {
    std::vector<std::string> factory_calls;
    auto factory = [&](const std::string& path)
        -> std::unique_ptr<SerialPort> {
        factory_calls.push_back(path);
        return std::make_unique<MockPiconetSerial>();
    };

    auto initial_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/old", 32},
                           std::move(initial_owner),
                           factory);

    backend.request_reopen("/dev/intermediate");
    backend.request_reopen("/dev/final");
    (void)backend.receive_frame();

    // Only the latest request is consumed; the intermediate one was
    // superseded before the emulation thread woke up to process it.
    CHECK(backend.config().device_path == "/dev/final");
    REQUIRE(factory_calls.size() == 1);
    CHECK(factory_calls[0] == "/dev/final");
}
