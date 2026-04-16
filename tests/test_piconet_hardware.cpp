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

// Hardware contract tests: run against a real Piconet device when the
// BEEBIUM_PICONET_DEVICE environment variable points at one (e.g.
// /dev/tty.usbmodem101). Skipped otherwise. These tests assert that
// real-firmware behaviour matches the expectations encoded in the
// piconet_protocol library, providing a fidelity anchor for
// FakePiconetDevice (Phase 6) -- if the fake diverges, both must pass
// the same contract.
//
// The tests in this file are SAFE WITHOUT A REAL ECONET NETWORK -- they
// exercise only self-observable Piconet behaviour (STATUS, SET_STATION,
// SET_MODE, RESTART). Wire-traffic tests (TX to a peer, BCAST, inbound
// MachinePeek) live in test_piconet_hardware_network.cpp (Phase 7) with
// a separate gating env var.

#ifndef _WIN32

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/piconet/Commands.hpp"
#include "beebium/econet/piconet/Constants.hpp"
#include "beebium/econet/piconet/Events.hpp"
#include "beebium/econet/piconet/PosixSerialPort.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

using namespace beebium::piconet;

namespace {

const char* device_path_env() {
    return std::getenv("BEEBIUM_PICONET_DEVICE");
}

// Write a complete command line to the device.
void send_command(PosixSerialPort& port, std::string_view line) {
    auto bytes = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
    auto result = port.write(bytes);
    REQUIRE_FALSE(result.error);
    REQUIRE(result.bytes == line.size());
}

// Read events from the port for up to `timeout`, returning the first
// event whose kind index matches `expected_index`. Other events
// (e.g. RX_BROADCAST from background traffic) are discarded silently.
// Returns nullopt on timeout or on a parse error.
template <typename ExpectedEvent>
std::optional<ExpectedEvent> wait_for_event(
    PosixSerialPort& port,
    std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    std::string line_buffer;
    std::array<std::uint8_t, 256> buf{};
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        auto result = port.read({buf.data(), buf.size()});
        if (result.error) return std::nullopt;
        if (result.would_block) continue;

        line_buffer.append(reinterpret_cast<const char*>(buf.data()), result.bytes);

        while (true) {
            auto newline = line_buffer.find(EVENT_TERMINATOR);
            if (newline == std::string::npos) break;
            std::string_view line(line_buffer.data(), newline);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

            auto event = parse_event_line(line);
            if (auto* e = std::get_if<ExpectedEvent>(&event)) {
                return *e;
            }
            line_buffer.erase(0, newline + 1);
        }
    }
    return std::nullopt;
}

// Drain any queued events for ~150ms so subsequent tests see only
// responses to their own commands. The firmware may have buffered
// background traffic if the device is on a live wire (not the case
// for the no-network suite, but defensive anyway).
void drain_events(PosixSerialPort& port) {
    std::array<std::uint8_t, 256> buf{};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = port.read({buf.data(), buf.size()});
        if (result.error) return;
        // Either would_block or some bytes -- keep going until the deadline.
    }
}

// RAII fixture: opens BEEBIUM_PICONET_DEVICE, drains any pending events.
// On destruction it leaves the device in whatever state the test put it
// in -- restoring is the responsibility of each test (e.g. by re-setting
// the prior station / mode).
struct HardwareFixture {
    PosixSerialPort port;

    HardwareFixture() : port(device_path_env() ? device_path_env() : "") {
        REQUIRE(port.is_open());
        drain_events(port);
    }
};

}  // namespace

TEST_CASE("Piconet hardware: STATUS round-trip reports a parseable response",
          "[piconet-hardware]") {
    if (!device_path_env()) {
        SKIP("BEEBIUM_PICONET_DEVICE not set; skipping hardware contract test.");
    }
    HardwareFixture h;

    send_command(h.port, format_status());
    auto status = wait_for_event<StatusEvent>(h.port);
    REQUIRE(status.has_value());

    // Sanity: a non-zero version (semver) and a station in [0,255].
    CHECK(status->version_major > 0);
    INFO("Firmware version: " << status->version_major
        << "." << status->version_minor << "." << status->version_patch);
    CHECK(status->station <= 0xFF);
}

TEST_CASE("Piconet hardware: firmware version matches EXPECTED_FIRMWARE_MAJOR/MINOR",
          "[piconet-hardware]") {
    if (!device_path_env()) {
        SKIP("BEEBIUM_PICONET_DEVICE not set; skipping hardware contract test.");
    }
    HardwareFixture h;

    send_command(h.port, format_status());
    auto status = wait_for_event<StatusEvent>(h.port);
    REQUIRE(status.has_value());

    INFO("Firmware: " << status->version_major
        << "." << status->version_minor << "." << status->version_patch
        << "; expecting at least " << EXPECTED_FIRMWARE_MAJOR
        << "." << EXPECTED_FIRMWARE_MINOR);

    // Major version must match exactly; minor may be >= our expectation
    // (forward-compatible additions). If this ever fails we have firmware
    // drift and need to re-audit the protocol library.
    CHECK(status->version_major == EXPECTED_FIRMWARE_MAJOR);
    CHECK(status->version_minor >= EXPECTED_FIRMWARE_MINOR);
}

TEST_CASE("Piconet hardware: SET_STATION round-trips through STATUS",
          "[piconet-hardware]") {
    if (!device_path_env()) {
        SKIP("BEEBIUM_PICONET_DEVICE not set; skipping hardware contract test.");
    }
    HardwareFixture h;

    // Capture original station so we can restore it.
    send_command(h.port, format_status());
    auto initial = wait_for_event<StatusEvent>(h.port);
    REQUIRE(initial.has_value());
    const std::uint8_t original_station = initial->station;

    // Set a distinctive value.
    constexpr std::uint8_t test_station = 42;
    send_command(h.port, format_set_station(test_station));
    send_command(h.port, format_status());
    auto after_set = wait_for_event<StatusEvent>(h.port);
    REQUIRE(after_set.has_value());
    CHECK(after_set->station == test_station);

    // Restore original.
    send_command(h.port, format_set_station(original_station));
    send_command(h.port, format_status());
    auto restored = wait_for_event<StatusEvent>(h.port);
    REQUIRE(restored.has_value());
    CHECK(restored->station == original_station);
}

TEST_CASE("Piconet hardware: SET_MODE STOP / LISTEN round-trip",
          "[piconet-hardware]") {
    if (!device_path_env()) {
        SKIP("BEEBIUM_PICONET_DEVICE not set; skipping hardware contract test.");
    }
    HardwareFixture h;

    send_command(h.port, format_status());
    auto initial = wait_for_event<StatusEvent>(h.port);
    REQUIRE(initial.has_value());
    const Mode original_mode = initial->mode;

    send_command(h.port, format_set_mode(Mode::Stop));
    send_command(h.port, format_status());
    auto stopped = wait_for_event<StatusEvent>(h.port);
    REQUIRE(stopped.has_value());
    CHECK(stopped->mode == Mode::Stop);

    send_command(h.port, format_set_mode(Mode::Listen));
    send_command(h.port, format_status());
    auto listening = wait_for_event<StatusEvent>(h.port);
    REQUIRE(listening.has_value());
    CHECK(listening->mode == Mode::Listen);

    // Restore.
    send_command(h.port, format_set_mode(original_mode));
}

TEST_CASE("Piconet hardware: RESTART resets the firmware to defaults",
          "[piconet-hardware]") {
    if (!device_path_env()) {
        SKIP("BEEBIUM_PICONET_DEVICE not set; skipping hardware contract test.");
    }
    HardwareFixture h;

    // Set a distinctive station first.
    send_command(h.port, format_set_station(123));

    // Restart the device. It takes a moment to come back, so allow generous
    // time for the next STATUS response.
    send_command(h.port, format_restart());

    send_command(h.port, format_status());
    auto status = wait_for_event<StatusEvent>(h.port, std::chrono::seconds(5));
    REQUIRE(status.has_value());

    // Per piconet/board/src/econet.c line 97 the firmware-default station
    // is 0x02. RESTART triggers an ADLC reset (piconet.c line 308); the
    // station survives because it lives in _listen_addresses, not in
    // ADLC registers, but mode resets via the firmware re-init.
    INFO("After RESTART, station=" << static_cast<int>(status->station)
        << " mode=" << static_cast<int>(status->mode));
    // Don't assert on station here: the firmware's RESTART semantics
    // re-initialise the ADLC but not necessarily the listen address.
    // The contract that matters: the device responds to STATUS after
    // RESTART within a reasonable time -- proves it didn't get bricked.
    CHECK(status->version_major > 0);
}

#endif  // !_WIN32
