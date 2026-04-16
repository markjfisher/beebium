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

// Phase 7b parity tests: the same Mc6854 + FourWayHandshake +
// PiconetBackend + FakePiconetDevice scenarios from
// test_piconet_integration.cpp, but with a PosixSerialPort and a
// PTY in the middle. Proves that PosixSerialPort doesn't introduce
// I/O bugs versus the in-process integration -- the same outcomes
// must hold whether PiconetBackend talks to the fake directly or
// through the kernel's PTY layer.
//
// POSIX-only.

#ifndef _WIN32

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/FourWayHandshake.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/econet/piconet/PosixSerialPort.hpp"

#include "piconet/FakePiconetDeviceOnPty.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace beebium;
using namespace beebium::piconet;
using beebium::piconet::test::FakePiconetDeviceOnPty;

namespace {

NetworkFrame make_raw_frame(std::vector<std::uint8_t> data) {
    NetworkFrame frame;
    frame.type = FrameType::RawFrame;
    frame.data = std::move(data);
    return frame;
}

void tick_n(FourWayHandshake& hs, int n) {
    for (int i = 0; i < n; ++i) hs.tick();
}

template <typename Pred>
bool wait_for(Pred&& predicate,
              std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

std::optional<NetworkFrame> wait_for_handshake_frame(
    FourWayHandshake& hs,
    std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto f = hs.receive_frame()) return f;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}

// Build the full chain: PTY-bridged fake + PosixSerialPort opening the
// slave end + PiconetBackend wrapping it. Returns the pieces the test
// needs (the bridged fake for injection, and the constructed backend).
struct PtyChain {
    std::unique_ptr<FakePiconetDeviceOnPty> bridge;
    std::unique_ptr<PiconetBackend>          backend;
};

PtyChain make_pty_chain(std::uint8_t initial_station = 32) {
    PtyChain chain;
    chain.bridge = std::make_unique<FakePiconetDeviceOnPty>();
    REQUIRE(chain.bridge->is_open());
    auto serial = std::make_unique<PosixSerialPort>(chain.bridge->slave_path());
    REQUIRE(serial->is_open());
    chain.backend = std::make_unique<PiconetBackend>(
        PiconetConfig{chain.bridge->slave_path(), initial_station},
        std::move(serial));
    return chain;
}

}  // namespace

TEST_CASE("Piconet PTY integration: constructor handshake reaches the fake via PTY",
          "[piconet][pty][integration]") {
    auto chain = make_pty_chain(/*initial_station=*/32);
    auto& fake = chain.bridge->device();

    // Constructor wrote SET_STATION 32 and SET_MODE LISTEN to PosixSerialPort.
    // Those bytes flow through the PTY to the fake. Wait briefly for the
    // pumper thread to deliver them.
    REQUIRE(wait_for([&]() { return fake.station() == 32 && fake.mode() == Mode::Listen; }));
}

TEST_CASE("Piconet PTY integration: scout+data produces correct TX at the fake via PTY",
          "[piconet][pty][integration]") {
    auto chain = make_pty_chain(32);
    auto& fake = chain.bridge->device();
    REQUIRE(wait_for([&]() { return fake.mode() == Mode::Listen; }));

    FourWayHandshake hs(*chain.backend);
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    REQUIRE(hs.receive_frame().has_value());
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 'H', 'I'}));

    REQUIRE(wait_for([&]() { return fake.tx_command_count() == 1; }));
    CHECK(fake.last_tx_dest_stn() == 254);
    CHECK(fake.last_tx_dest_net() == 0);
    CHECK(fake.last_tx_ctrl() == 0x80);
    CHECK(fake.last_tx_port() == 0x99);
    CHECK(fake.last_tx_data() == std::vector<std::uint8_t>{'H', 'I'});
}

TEST_CASE("Piconet PTY integration: TX_RESULT OK from fake closes the handshake",
          "[piconet][pty][integration]") {
    auto chain = make_pty_chain(32);
    auto& fake = chain.bridge->device();
    REQUIRE(wait_for([&]() { return fake.mode() == Mode::Listen; }));

    FourWayHandshake hs(*chain.backend);
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    REQUIRE(hs.receive_frame().has_value());
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 'H', 'I'}));
    REQUIRE(wait_for([&]() { return fake.tx_command_count() == 1; }));

    // The fake produced TX_RESULT OK; bytes flow back through PTY to
    // PiconetBackend's reader thread which enqueues an Ack. Poll until
    // FourWayHandshake delivers a frame to the Beeb-side.
    REQUIRE(wait_for([&]() { return hs.receive_frame().has_value(); },
                     std::chrono::seconds(3)));
}

TEST_CASE("Piconet PTY integration: broadcast TX produces BCAST at the fake",
          "[piconet][pty][integration]") {
    auto chain = make_pty_chain(32);
    auto& fake = chain.bridge->device();
    REQUIRE(wait_for([&]() { return fake.mode() == Mode::Listen; }));

    FourWayHandshake hs(*chain.backend);
    hs.send_frame(make_raw_frame({0xFF, 0xFF, 32, 0, 0x9C, 0x80, 'B', 'C'}));

    REQUIRE(wait_for([&]() { return fake.bcast_command_count() == 1; }));
    const auto& bcast = fake.last_bcast_data();
    REQUIRE(bcast.size() == 4);
    CHECK(bcast[0] == 0x9C);  // wire ctrl with high bit set
    CHECK(bcast[1] == 0x80);
    CHECK(bcast[2] == 'B');
    CHECK(bcast[3] == 'C');
}

TEST_CASE("Piconet PTY integration: inbound RX_TRANSMIT injection becomes a scout",
          "[piconet][pty][integration]") {
    auto chain = make_pty_chain(32);
    auto& fake = chain.bridge->device();
    REQUIRE(wait_for([&]() { return fake.mode() == Mode::Listen; }));

    FourWayHandshake hs(*chain.backend);

    // Inject an inbound unicast at the fake; the resulting RX_TRANSMIT
    // event flows through the PTY to PiconetBackend's reader, which
    // queues a Unicast NetworkFrame. FourWayHandshake then constructs
    // the scout for the Beeb to consume.
    fake.inject_inbound_unicast(/*src_stn=*/254, /*src_net=*/0,
                                /*ctrl=*/0x80, /*port=*/0x99,
                                /*scout_extra=*/{},
                                /*data=*/{'H', 'I'});

    auto scout = wait_for_handshake_frame(hs, std::chrono::seconds(3));
    REQUIRE(scout.has_value());
    REQUIRE(scout->type == FrameType::RawFrame);
    REQUIRE(scout->data.size() >= 6);
    CHECK(scout->data[0] == 32);
    CHECK(scout->data[2] == 254);
    CHECK((scout->data[4] & 0x7F) == 0x00);
    CHECK(scout->data[5] == 0x99);
}

TEST_CASE("Piconet PTY integration: on_station_id_changed reaches the fake via PTY",
          "[piconet][pty][integration]") {
    auto chain = make_pty_chain(32);
    auto& fake = chain.bridge->device();
    REQUIRE(wait_for([&]() { return fake.station() == 32; }));

    chain.backend->on_station_id_changed(99);
    REQUIRE(wait_for([&]() { return fake.station() == 99; }));
}

#endif  // !_WIN32
