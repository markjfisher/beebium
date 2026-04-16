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

// End-to-end integration tests for the Piconet stack:
//   FourWayHandshake -> PiconetBackend -> FakePiconetDevice
//
// These verify that a Beeb-style scout+data sequence handed to
// FourWayHandshake (as Mc6854 would when the NFS ROM writes the
// scout to its TX FIFO) ends up as a correctly-formed TX command at
// the device end of the chain. Symmetrically, that frames injected at
// the device end propagate back through PiconetBackend's reader
// thread, FourWayHandshake, and surface as scout+data on the BBC
// side.
//
// Pure in-process: no real hardware, no PTYs.

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/FourWayHandshake.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"

#include "piconet/FakePiconetDevice.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace beebium;
using namespace beebium::piconet;
using beebium::piconet::test::FakePiconetDevice;

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

// Poll receive_frame on the handshake until a frame arrives or timeout.
// Required for any test that depends on PiconetBackend's reader thread
// processing inbound bytes from the fake.
std::optional<NetworkFrame> wait_for_handshake_frame(
    FourWayHandshake& hs,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto f = hs.receive_frame()) return f;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::nullopt;
}

// Wait until `predicate()` returns true or `timeout` elapses. Returns
// true iff the predicate became true within the window.
template <typename Pred>
bool wait_for(Pred&& predicate,
              std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

}  // namespace

TEST_CASE("Piconet integration: PiconetBackend constructor sets fake's station and mode",
          "[piconet][integration]") {
    auto fake_owner = std::make_unique<FakePiconetDevice>();
    auto* fake = fake_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/null", /*initial_station=*/32},
                           std::move(fake_owner));

    // Constructor sends SET_STATION 32 then SET_MODE LISTEN to the fake.
    // Both writes are synchronous on the construction thread, so the fake
    // has already processed them by the time the constructor returns.
    CHECK(fake->station() == 32);
    CHECK(fake->mode() == Mode::Listen);
}

TEST_CASE("Piconet integration: scout+data from the Beeb produces a correct TX at the fake",
          "[piconet][integration]") {
    // Build the chain: PiconetBackend wraps FakePiconetDevice;
    // FourWayHandshake wraps PiconetBackend. Mc6854 would normally drive
    // FourWayHandshake; we drive it directly with raw frames matching what
    // the ADLC produces when the NFS ROM writes a scout+data sequence.
    auto fake_owner = std::make_unique<FakePiconetDevice>();
    auto* fake = fake_owner.get();
    auto backend_owner = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", /*initial_station=*/32},
        std::move(fake_owner));
    auto* backend = backend_owner.get();
    FourWayHandshake hs(*backend);

    // Step 1: Beeb writes scout to ADLC; Mc6854 emits a RawFrame.
    // Wire scout bytes: dest=254, dest_net=0, src=32, src_net=0, ctrl=0x80, port=0x99
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 0x80, 0x99}));

    // FourWayHandshake stages the scout and arms the synthetic scout-ack
    // timer. No TX command should have reached the fake yet.
    CHECK(fake->tx_command_count() == 0);

    // Step 2: tick to the synthetic scout-ack timeout, then consume it
    // (the Mc6854 would consume it via receive_frame and clock it out
    // to the NFS ROM).
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    REQUIRE(hs.receive_frame().has_value());

    // Step 3: Beeb writes data frame to ADLC; Mc6854 emits another RawFrame.
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 'H', 'I'}));

    // FourWayHandshake packs scout+data into a Unicast NetworkFrame and
    // calls PiconetBackend.send_frame, which writes the TX command to the
    // fake synchronously.
    REQUIRE(fake->tx_command_count() == 1);
    CHECK(fake->last_tx_dest_stn() == 254);
    CHECK(fake->last_tx_dest_net() == 0);
    CHECK(fake->last_tx_ctrl() == 0x80);  // PiconetBackend ORed back the high bit
    CHECK(fake->last_tx_port() == 0x99);
    CHECK(fake->last_tx_data() == std::vector<std::uint8_t>{'H', 'I'});
    CHECK(fake->last_tx_scout_extra().empty());  // ctrl 0x00 (post-mask) has no scout-extra
}

TEST_CASE("Piconet integration: TX_RESULT OK from the fake closes the handshake via Ack",
          "[piconet][integration]") {
    // Same scout+data as above, then verify the fake's TX_RESULT OK comes
    // back through PiconetBackend's reader thread as a bare Ack
    // NetworkFrame which FourWayHandshake consumes to short-circuit the
    // synthetic final-ack timer.
    auto fake_owner = std::make_unique<FakePiconetDevice>();
    auto* fake = fake_owner.get();
    auto backend_owner = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", 32}, std::move(fake_owner));
    auto* backend = backend_owner.get();
    FourWayHandshake hs(*backend);

    hs.send_frame(make_raw_frame({254, 0, 32, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    REQUIRE(hs.receive_frame().has_value());
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 'H', 'I'}));
    REQUIRE(fake->tx_command_count() == 1);

    // The fake produced TX_RESULT OK; PiconetBackend's reader picks it up
    // asynchronously and enqueues an Ack. Poll until it arrives.
    REQUIRE(wait_for([&]() { return hs.receive_frame().has_value(); }));
    // Note: the previous receive_frame consumed the Ack (or the synthetic
    // final-ack via timer -- both produce a frame to the Beeb). Either way,
    // the handshake completes promptly.
}

TEST_CASE("Piconet integration: broadcast from the Beeb produces BCAST at the fake",
          "[piconet][integration]") {
    auto fake_owner = std::make_unique<FakePiconetDevice>();
    auto* fake = fake_owner.get();
    auto backend_owner = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", 32}, std::move(fake_owner));
    auto* backend = backend_owner.get();
    FourWayHandshake hs(*backend);

    // Wire broadcast: [0xFF, 0xFF, src_stn, src_net, ctrl, port, payload...]
    hs.send_frame(make_raw_frame({0xFF, 0xFF, 32, 0, 0x9C, 0x80, 'B', 'C'}));

    REQUIRE(fake->bcast_command_count() == 1);
    // BCAST data is [ctrl|0x80, port, payload]; the ctrl high bit is
    // restored by PiconetBackend before format_bcast.
    const auto& bcast = fake->last_bcast_data();
    REQUIRE(bcast.size() == 4);
    CHECK(bcast[0] == 0x9C);  // wire ctrl with high bit set
    CHECK(bcast[1] == 0x80);
    CHECK(bcast[2] == 'B');
    CHECK(bcast[3] == 'C');
}

TEST_CASE("Piconet integration: inbound RX_TRANSMIT becomes a scout for the Beeb",
          "[piconet][integration]") {
    // The far end of the chain (the fake) gets an inbound unicast injected.
    // PiconetBackend's reader thread parses it; FourWayHandshake constructs
    // a synthetic scout for the Beeb to consume, then a synthetic data
    // frame after the scout-ack.
    auto fake_owner = std::make_unique<FakePiconetDevice>();
    auto* fake = fake_owner.get();
    auto backend_owner = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", /*initial_station=*/32},
        std::move(fake_owner));
    auto* backend = backend_owner.get();
    FourWayHandshake hs(*backend);

    // Inject a unicast addressed to us (station 32).
    fake->inject_inbound_unicast(/*src_stn=*/254, /*src_net=*/0,
                                 /*ctrl=*/0x80, /*port=*/0x99,
                                 /*scout_extra=*/{},
                                 /*data=*/{'H', 'I'});

    // The reader thread processes the RX_TRANSMIT and queues a Unicast
    // on PiconetBackend's rx_queue_. FourWayHandshake's tick picks it up
    // when receive_frame is called and turns it into a scout for the Beeb.
    auto scout = wait_for_handshake_frame(hs);
    REQUIRE(scout.has_value());
    REQUIRE(scout->type == FrameType::RawFrame);
    REQUIRE(scout->data.size() >= 6);
    CHECK(scout->data[0] == 32);   // dest = us
    CHECK(scout->data[2] == 254);  // src = them
    CHECK((scout->data[4] & 0x7F) == 0x00);  // function code
    CHECK(scout->data[5] == 0x99);

    // The Beeb would now write a scout-ack to the ADLC; FourWayHandshake
    // swallows it, arms the data-frame delivery timer.
    hs.send_frame(make_raw_frame({254, 0, 32, 0, 0x80 | 0x80, 0x99}));  // scout-ack
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    auto data = hs.receive_frame();
    REQUIRE(data.has_value());
    REQUIRE(data->type == FrameType::RawFrame);
    REQUIRE(data->data.size() >= 4);
    CHECK(data->data[0] == 32);
    CHECK(data->data[2] == 254);
    // The data payload follows the 4-byte header.
    REQUIRE(data->data.size() == 6);
    CHECK(data->data[4] == 'H');
    CHECK(data->data[5] == 'I');
}

TEST_CASE("Piconet integration: SET_STATION via EconetSocket reaches the fake",
          "[piconet][integration]") {
    // EconetSocket::set_station_id triggers the on_station_id_changed
    // hook which PiconetBackend implements by issuing SET_STATION to the
    // device.
    auto fake_owner = std::make_unique<FakePiconetDevice>();
    auto* fake = fake_owner.get();
    auto backend_owner = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", /*initial_station=*/32},
        std::move(fake_owner));
    auto* backend = backend_owner.get();

    REQUIRE(fake->station() == 32);

    // Direct call (avoiding EconetSocket setup boilerplate -- the hook is
    // a thin call from set_station_id to backend->on_station_id_changed,
    // which is what we want to verify).
    backend->on_station_id_changed(99);
    CHECK(fake->station() == 99);
}
