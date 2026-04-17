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

// Phase 8 multi-emulator integration test.
//
// Two Beebium-style stacks
//   FourWayHandshake -> PiconetBackend -> AunBridgePiconetDevice
// peered on UDP loopback. Validates that the full Piconet stack works
// end-to-end across two instances when there's a real (UDP) network
// underneath, without any real Piconet hardware.

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include "beebium/econet/FourWayHandshake.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"

#include "piconet/AunBridgePiconetDevice.hpp"
#include "piconet/BorrowedSerialPort.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace beebium;
using namespace beebium::piconet;
using beebium::piconet::test::AunBridgePiconetDevice;
using beebium::piconet::test::BorrowedSerialPort;

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

constexpr std::uint32_t loopback_ip() {
    return htonl(INADDR_LOOPBACK);
}

// Two-Beebium peer setup: instance A at station 32, instance B at
// station 254 (or whatever's passed). Each gets an AunBridgePiconetDevice
// bound to an OS-assigned UDP port; once bound, each adds the other as a
// peer. The PiconetBackend then opens its bridge as the SerialPort.
struct PeerPair {
    std::unique_ptr<AunBridgePiconetDevice> a_bridge;
    std::unique_ptr<AunBridgePiconetDevice> b_bridge;
    std::unique_ptr<PiconetBackend>          a_backend;
    std::unique_ptr<PiconetBackend>          b_backend;
};

PeerPair make_peer_pair(std::uint8_t a_stn = 32, std::uint8_t b_stn = 254) {
    PeerPair p;
    // Net 0 throughout for both instances -- matches Piconet's hardcoded
    // src_net = 0 in outgoing frames.
    p.a_bridge = std::make_unique<AunBridgePiconetDevice>(0, a_stn, 0);
    p.b_bridge = std::make_unique<AunBridgePiconetDevice>(0, b_stn, 0);
    REQUIRE(p.a_bridge->is_open());
    REQUIRE(p.b_bridge->is_open());

    p.a_bridge->add_peer(0, b_stn, loopback_ip(), p.b_bridge->local_port());
    p.b_bridge->add_peer(0, a_stn, loopback_ip(), p.a_bridge->local_port());

    // The PeerPair owns each AunBridgePiconetDevice via unique_ptr; the
    // PiconetBackends borrow them via BorrowedSerialPort so the bridges
    // can outlive the backends if a test needs to inspect or reuse them
    // after the backends are destroyed.
    p.a_backend = std::make_unique<PiconetBackend>(
        PiconetConfig{"<aun-bridge-A>", a_stn},
        std::make_unique<BorrowedSerialPort>(p.a_bridge.get()));
    p.b_backend = std::make_unique<PiconetBackend>(
        PiconetConfig{"<aun-bridge-B>", b_stn},
        std::make_unique<BorrowedSerialPort>(p.b_bridge.get()));

    return p;
}

}  // namespace

TEST_CASE("Piconet AUN bridge: STATUS reports the sentinel firmware version",
          "[piconet][aun-bridge]") {
    AunBridgePiconetDevice bridge(0, 32, 0);
    REQUIRE(bridge.is_open());

    // Send STATUS, read the response. Use the SerialPort interface
    // directly (no PiconetBackend), so this is a pure protocol-level
    // assertion that the sentinel version is being emitted.
    const std::string cmd = "STATUS\r";
    bridge.write(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(cmd.data()), cmd.size()));

    std::string line;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        std::array<std::uint8_t, 256> buf{};
        auto rr = bridge.read({buf.data(), buf.size()});
        if (rr.bytes > 0) line.append(reinterpret_cast<const char*>(buf.data()), rr.bytes);
        if (line.find('\n') != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(line.find(AunBridgePiconetDevice::SENTINEL_FIRMWARE_VERSION) != std::string::npos);
}

TEST_CASE("Piconet AUN bridge: A's Unicast reaches B's emulated NFS as a scout",
          "[piconet][aun-bridge][integration]") {
    auto peers = make_peer_pair(/*a_stn=*/32, /*b_stn=*/254);

    // Build a FourWayHandshake on top of B so the inbound Unicast can be
    // turned into a wire scout for B's "NFS" to consume.
    FourWayHandshake hs_b(*peers.b_backend);

    // A's "NFS" sends a scout+data via FourWayHandshake. We drive it
    // directly with raw frames (just like test_piconet_integration.cpp).
    FourWayHandshake hs_a(*peers.a_backend);
    hs_a.send_frame(make_raw_frame({254, 0, 32, 0, 0x80, 0x99}));
    tick_n(hs_a, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    REQUIRE(hs_a.receive_frame().has_value());
    hs_a.send_frame(make_raw_frame({254, 0, 32, 0, 'H', 'I'}));

    // The Unicast travels as a UDP packet to B. B's bridge poller picks
    // it up and emits an RX_TRANSMIT serial event. B's PiconetBackend
    // reader parses it and queues a Unicast NetworkFrame. B's
    // FourWayHandshake then constructs a scout for B's NFS.
    auto scout = wait_for_handshake_frame(hs_b, std::chrono::seconds(3));
    REQUIRE(scout.has_value());
    REQUIRE(scout->type == FrameType::RawFrame);
    REQUIRE(scout->data.size() >= 6);
    CHECK(scout->data[0] == 254);  // dest = B
    CHECK(scout->data[2] == 32);   // src = A
    CHECK((scout->data[4] & 0x7F) == 0x00);
    CHECK(scout->data[5] == 0x99);

    // Deliver the data frame: B sends scout-ack, FourWayHandshake holds
    // until the data-frame timer fires.
    hs_b.send_frame(make_raw_frame({254, 0, 32, 0, 0x80 | 0x80, 0x99}));
    tick_n(hs_b, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    auto data = hs_b.receive_frame();
    REQUIRE(data.has_value());
    REQUIRE(data->data.size() == 6);
    CHECK(data->data[4] == 'H');
    CHECK(data->data[5] == 'I');
}

TEST_CASE("Piconet AUN bridge: A's broadcast reaches B as RX_BROADCAST",
          "[piconet][aun-bridge][integration]") {
    auto peers = make_peer_pair(32, 254);
    FourWayHandshake hs_b(*peers.b_backend);
    FourWayHandshake hs_a(*peers.a_backend);

    // Wire broadcast: dest=0xFF, dest_net=0xFF, ctrl=0x9C, port=0x80,
    // payload "BC".
    hs_a.send_frame(make_raw_frame({0xFF, 0xFF, 32, 0, 0x9C, 0x80, 'B', 'C'}));

    // Broadcast arrives at B as a Broadcast NetworkFrame. FourWayHandshake's
    // RX-in-idle path delivers it as a RawFrame to the Beeb side.
    auto frame = wait_for_handshake_frame(hs_b, std::chrono::seconds(3));
    REQUIRE(frame.has_value());
    REQUIRE(frame->type == FrameType::RawFrame);
    REQUIRE(frame->data.size() >= 6);
    CHECK(frame->data[0] == 0xFF);  // wire dest
    CHECK(frame->data[2] == 32);    // wire src
    CHECK((frame->data[4] & 0x7F) == 0x1C);  // function code (high bit stripped)
    CHECK(frame->data[5] == 0x80);
    REQUIRE(frame->data.size() == 8);
    CHECK(frame->data[6] == 'B');
    CHECK(frame->data[7] == 'C');
}
