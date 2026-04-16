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

// Hardware contract tests that REQUIRE a real Econet wire with at least one
// peer station available. Skipped unless BOTH:
//   BEEBIUM_PICONET_DEVICE=/dev/tty.usbmodem...   (which device to use)
//   BEEBIUM_PICONET_NETWORK_AVAILABLE=1           (operator confirmation)
// are set. The operator must arrange the wire setup before running.
//
// Optional:
//   BEEBIUM_PICONET_PEER_STATION=<n>   (default: 254 -- typical fileserver)
//   BEEBIUM_PICONET_PEER_NET=<n>       (default: 1 -- the wire's network number;
//                                       PiEconetBridge typically uses 1)
//   BEEBIUM_PICONET_UNUSED_STATION=<n> (default: 99 -- a station not on the wire)
//   BEEBIUM_PICONET_OUR_STATION=<n>    (default: 32 -- our temporary station)
//
// These tests bypass PiconetBackend and talk directly via PosixSerialPort
// + the protocol library, so they exercise the wire format end-to-end
// without involving the reader-thread state machine.

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
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

using namespace beebium::piconet;

namespace {

const char* device_path_env() { return std::getenv("BEEBIUM_PICONET_DEVICE"); }
const char* network_available_env() { return std::getenv("BEEBIUM_PICONET_NETWORK_AVAILABLE"); }

bool network_tests_enabled() {
    return device_path_env() && network_available_env() &&
           std::string(network_available_env()) == "1";
}

std::uint8_t env_uint8(const char* name, std::uint8_t fallback) {
    const char* v = std::getenv(name);
    if (!v) return fallback;
    try {
        int n = std::stoi(v);
        if (n < 0 || n > 255) return fallback;
        return static_cast<std::uint8_t>(n);
    } catch (...) {
        return fallback;
    }
}

void send_command(PosixSerialPort& port, std::string_view line) {
    auto bytes = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
    auto result = port.write(bytes);
    REQUIRE_FALSE(result.error);
    REQUIRE(result.bytes == line.size());
}

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

// Wait for a window of `quiet_duration` during which no event of any kind
// arrives. Returns true if the window passed quietly. The deadline guards
// against pathological background traffic that would never let us assert
// "nothing arrived".
bool wait_for_quiet(PosixSerialPort& port,
                    std::chrono::milliseconds quiet_duration) {
    std::array<std::uint8_t, 256> buf{};
    auto deadline = std::chrono::steady_clock::now() + quiet_duration * 4;
    auto last_activity = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() < deadline) {
        auto result = port.read({buf.data(), buf.size()});
        if (result.would_block) {
            if (std::chrono::steady_clock::now() - last_activity >= quiet_duration) {
                return true;
            }
            continue;
        }
        if (result.error) return false;
        if (result.bytes > 0) last_activity = std::chrono::steady_clock::now();
    }
    return false;
}

void drain_events(PosixSerialPort& port,
                  std::chrono::milliseconds drain_time = std::chrono::milliseconds(150)) {
    std::array<std::uint8_t, 256> buf{};
    auto deadline = std::chrono::steady_clock::now() + drain_time;
    while (std::chrono::steady_clock::now() < deadline) {
        port.read({buf.data(), buf.size()});
    }
}

// Open the device, snapshot original station/mode, restore on destruction.
struct WiredFixture {
    PosixSerialPort port;
    std::uint8_t original_station = DEFAULT_STATION;
    Mode original_mode = Mode::Stop;

    explicit WiredFixture(std::uint8_t our_station)
        : port(device_path_env() ? device_path_env() : "") {
        REQUIRE(port.is_open());
        drain_events(port);

        send_command(port, format_status());
        auto status = wait_for_event<StatusEvent>(port);
        REQUIRE(status.has_value());
        original_station = status->station;
        original_mode = status->mode;

        // Configure for tests: our station, LISTEN mode (so RX events flow).
        send_command(port, format_set_station(our_station));
        send_command(port, format_set_mode(Mode::Listen));
        drain_events(port, std::chrono::milliseconds(50));
    }

    ~WiredFixture() {
        // Best-effort restore. If the device is wedged we still close the
        // port via PosixSerialPort's destructor.
        send_command(port, format_set_station(original_station));
        send_command(port, format_set_mode(original_mode));
    }
};

}  // namespace

TEST_CASE("Piconet network: TX to a station that does not exist returns NO_SCOUT_ACK",
          "[piconet-hardware-network]") {
    if (!network_tests_enabled()) {
        SKIP("Set BEEBIUM_PICONET_DEVICE and BEEBIUM_PICONET_NETWORK_AVAILABLE=1 "
             "after attaching the Piconet to a real Econet wire.");
    }
    const std::uint8_t our_station    = env_uint8("BEEBIUM_PICONET_OUR_STATION",   32);
    const std::uint8_t unused_station = env_uint8("BEEBIUM_PICONET_UNUSED_STATION", 99);
    WiredFixture h(our_station);

    const std::uint8_t peer_net = env_uint8("BEEBIUM_PICONET_PEER_NET", 1);
    // Send a TX to a station that nobody on the wire is listening for.
    // Per piconet/board/src/econet.c lines 186-188, the firmware times out
    // _wait_ack after ~200ms and returns NO_SCOUT_ACK.
    const std::vector<std::uint8_t> data{0xAA};
    send_command(h.port, format_tx(unused_station, peer_net,
                                    /*ctrl=*/0x80, /*port=*/0x99,
                                    data, /*scout_extra=*/{}));

    // Allow generous time for the firmware's internal timeout plus USB
    // round-trip; firmware default is 200ms.
    auto result = wait_for_event<TxResultEvent>(h.port,
                                                std::chrono::seconds(2));
    REQUIRE(result.has_value());
    INFO("Got TX_RESULT " << static_cast<int>(result->result));
    CHECK(result->result == TxResult::NoScoutAck);
}

TEST_CASE("Piconet network: TX to the fileserver completes the wire handshake",
          "[piconet-hardware-network][needs-station-registered]") {
    // Requires: BEEBIUM_PICONET_OUR_STATION must be a station that the
    // peer (e.g. PiEconetBridge) is configured to know about. PiEconetBridge
    // filters incoming frames at the kernel-module level by its station
    // set; scouts from unknown source stations are dropped before the
    // user-space bridge sees them, so the peer cannot scout-ack us.
    //
    // To enable this test against PiEconetBridge, add an entry to
    // /etc/econet-gpio/econet-hpbridge.cfg for the station you'll use,
    // e.g. EXPOSE STATION 1.32 ON PORT *:32769 (or the right syntax for
    // your bridge version), then restart econet-hpbridge.
    //
    // Tagged [needs-station-registered] so it can be excluded by default
    // when the bridge-side configuration isn't known to include the test
    // station.
    if (!network_tests_enabled()) {
        SKIP("network not available");
    }
    const std::uint8_t our_station  = env_uint8("BEEBIUM_PICONET_OUR_STATION",  32);
    const std::uint8_t peer_station = env_uint8("BEEBIUM_PICONET_PEER_STATION", 254);
    const std::uint8_t peer_net     = env_uint8("BEEBIUM_PICONET_PEER_NET",     0);
    WiredFixture h(our_station);

    // Send a probe to the fileserver's standard port. Fileservers may
    // reject the request (we're sending nonsense) but the wire-level
    // four-way handshake should at minimum complete the scout-ack
    // exchange. The firmware reports OK if the data ack also arrives.
    // If the fileserver rejects the request mid-handshake we may get
    // NO_DATA_ACK -- both prove the wire works.
    //
    // Acorn convention: dest_net=0 means "this network", so a station on
    // any single-net wire should be reachable via 0.<stn>. Override
    // BEEBIUM_PICONET_PEER_NET if the bridge requires explicit net
    // numbering.
    const std::vector<std::uint8_t> data{0x00, 0x00, 0x00, 0x00};  // padding
    send_command(h.port, format_tx(peer_station, peer_net,
                                    /*ctrl=*/0x80, /*port=*/0x99,
                                    data, /*scout_extra=*/{}));

    auto result = wait_for_event<TxResultEvent>(h.port,
                                                std::chrono::seconds(2));
    REQUIRE(result.has_value());
    INFO("Got TX_RESULT " << static_cast<int>(result->result));
    // The peer is on the wire and knows our station, so we must NOT see
    // NO_SCOUT_ACK. If NO_SCOUT_ACK fires here, the most likely cause is
    // that BEEBIUM_PICONET_OUR_STATION is not registered in the bridge's
    // station set (PiEconetBridge filters unknown sources before
    // user-space sees them).
    CHECK(result->result != TxResult::NoScoutAck);
    // Anything else is acceptable: OK, NO_DATA_ACK (peer rejected the
    // payload), TIMEOUT (unlikely but tolerable). LINE_JAMMED would
    // indicate a real wire problem.
    CHECK(result->result != TxResult::LineJammed);
}

TEST_CASE("Piconet network: BCAST returns TX_RESULT OK (fire-and-forget on the wire)",
          "[piconet-hardware-network]") {
    if (!network_tests_enabled()) {
        SKIP("network not available");
    }
    const std::uint8_t our_station = env_uint8("BEEBIUM_PICONET_OUR_STATION", 32);
    WiredFixture h(our_station);

    // Wire BCAST data: [ctrl|0x80, port, payload]. Use a benign port that
    // no one will react to.
    const std::vector<std::uint8_t> wire_data{0x9C, 0xBE, 'B', 'C'};
    send_command(h.port, format_bcast(wire_data));

    auto result = wait_for_event<TxResultEvent>(h.port,
                                                std::chrono::seconds(2));
    REQUIRE(result.has_value());
    INFO("Got TX_RESULT " << static_cast<int>(result->result));
    // The firmware reports the result of ADLC frame transmission only;
    // there's no scout-ack wait for broadcasts. OK is the expected value.
    CHECK(result->result == TxResult::Ok);
}

// NOTE: an outbound MachinePeek test (TX with port=0, ctrl=0x88) would be
// valuable but requires a peer that implements MachinePeek. In practice
// fileserver software like PiEconetBridge does NOT respond to MachinePeek
// (it's an immediate-operation feature typically handled by the receiving
// station's Econet hardware/OS, not by application-level fileservers).
// Against PiEconetBridge at station 254 we observe NO_SCOUT_ACK, which
// is correct firmware behaviour given an unresponsive peer.
//
// To add MachinePeek coverage in the future, attach a real BBC or a
// BeebEm-equipped host to the wire and target it via
// BEEBIUM_PICONET_MACHINE_PEEK_STATION=<n>.

TEST_CASE("Piconet network: SET_MODE STOP suppresses inbound RX events",
          "[piconet-hardware-network]") {
    if (!network_tests_enabled()) {
        SKIP("network not available");
    }
    const std::uint8_t our_station = env_uint8("BEEBIUM_PICONET_OUR_STATION", 32);
    WiredFixture h(our_station);

    // Switch to STOP mode and verify there's a quiet window. Per
    // piconet/board/src/piconet.c lines 357-359 the firmware skips RX
    // checks when in STOP mode, so any traffic on the wire (broadcasts,
    // fileserver chatter) should not produce events.
    send_command(h.port, format_set_mode(Mode::Stop));
    drain_events(h.port, std::chrono::milliseconds(100));

    // Expect a 500ms quiet window. If the fixture's wire is busy enough
    // that no quiet window fits, this test reports a false failure --
    // adjust the duration if real-world testing shows that's an issue.
    const bool quiet = wait_for_quiet(h.port, std::chrono::milliseconds(500));
    CHECK(quiet);
}

#endif  // !_WIN32
