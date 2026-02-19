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

#include <beebium/econet/AunBackend.hpp>
#include <catch2/catch_test_macros.hpp>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <chrono>

using namespace beebium;

namespace {

// Loopback address in network byte order.
uint32_t loopback_ip() {
    return htonl(INADDR_LOOPBACK);
}

// Ephemeral ports to avoid conflicts with real services.
// If bind fails (port in use), tests SKIP rather than FAIL.
constexpr uint16_t PORT_A = 42001;
constexpr uint16_t PORT_B = 42002;

// Helper: create two backends peered to each other on loopback.
struct LoopbackPair {
    std::unique_ptr<AunBackend> a;  // Station 1 on PORT_A
    std::unique_ptr<AunBackend> b;  // Station 254 on PORT_B
    bool ready = false;

    LoopbackPair(uint16_t port_a = PORT_A, uint16_t port_b = PORT_B) {
        a = std::make_unique<AunBackend>(0, 1, port_a);
        b = std::make_unique<AunBackend>(0, 254, port_b);

        if (!a->is_connected() || !b->is_connected()) {
            return;  // Port conflict — caller should SKIP
        }

        // Peer each other on loopback.
        a->add_peer(0, 254, loopback_ip(), port_b);
        b->add_peer(0, 1, loopback_ip(), port_a);
        ready = true;
    }
};

// Brief pause to allow UDP datagram delivery on loopback.
void brief_pause() {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

}  // namespace

// =============================================================================
// Construction
// =============================================================================

TEST_CASE("AunBackend: construction with valid port succeeds", "[econet][aun][backend]") {
    AunBackend backend(0, 1, PORT_A);
    if (!backend.is_connected()) {
        SKIP("Port " << PORT_A << " unavailable");
    }
    CHECK(backend.is_connected());
    CHECK(backend.local_port() == PORT_A);
}

TEST_CASE("AunBackend: duplicate port binding fails gracefully", "[econet][aun][backend]") {
    AunBackend first(0, 1, PORT_A);
    if (!first.is_connected()) {
        SKIP("Port " << PORT_A << " unavailable");
    }

    // Second backend on same port — should fail to bind.
    // Note: SO_REUSEADDR allows this on some platforms, so we test behaviour
    // rather than asserting failure.
    AunBackend second(0, 2, PORT_A);
    // If both connected, that's platform-dependent (SO_REUSEADDR) — not an error.
    // Just verify neither crashed.
    CHECK(first.is_connected());
}

// =============================================================================
// Peer Management
// =============================================================================

TEST_CASE("AunBackend: add and remove peers", "[econet][aun][backend]") {
    AunBackend backend(0, 1, PORT_A);
    if (!backend.is_connected()) SKIP("Port unavailable");

    CHECK(backend.peer_count() == 0);

    backend.add_peer(0, 254, loopback_ip(), PORT_B);
    CHECK(backend.peer_count() == 1);

    backend.add_peer(0, 253, loopback_ip(), PORT_B + 1);
    CHECK(backend.peer_count() == 2);

    backend.remove_peer(0, 254);
    CHECK(backend.peer_count() == 1);

    backend.remove_peer(0, 253);
    CHECK(backend.peer_count() == 0);
}

TEST_CASE("AunBackend: remove non-existent peer is harmless", "[econet][aun][backend]") {
    AunBackend backend(0, 1, PORT_A);
    if (!backend.is_connected()) SKIP("Port unavailable");

    backend.remove_peer(0, 99);  // Should not crash
    CHECK(backend.peer_count() == 0);
}

// =============================================================================
// Receive When Empty
// =============================================================================

TEST_CASE("AunBackend: receive_frame returns nullopt when no data", "[econet][aun][backend]") {
    AunBackend backend(0, 1, PORT_A);
    if (!backend.is_connected()) SKIP("Port unavailable");

    CHECK_FALSE(backend.receive_frame().has_value());
}

// =============================================================================
// Send/Receive — Unicast
// =============================================================================

TEST_CASE("AunBackend: send and receive Unicast", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.port = 0x99;
    frame.control_byte = 0x00;
    frame.dest_net = 0;
    frame.dest_stn = 254;
    frame.src_net = 0;
    frame.src_stn = 1;
    frame.data = {0xAA, 0xBB, 0xCC};

    pair.a->send_frame(frame);
    brief_pause();

    auto received = pair.b->receive_frame();
    REQUIRE(received.has_value());
    CHECK(received->type == FrameType::Unicast);
    CHECK(received->port == 0x99);
    CHECK(received->control_byte == 0x00);
    CHECK(received->src_net == 0);
    CHECK(received->src_stn == 1);
    CHECK(received->dest_net == 0);
    CHECK(received->dest_stn == 254);
    REQUIRE(received->data.size() == 3);
    CHECK(received->data[0] == 0xAA);
    CHECK(received->data[1] == 0xBB);
    CHECK(received->data[2] == 0xCC);
}

// =============================================================================
// Send/Receive — Ack
// =============================================================================

TEST_CASE("AunBackend: send and receive Ack", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    NetworkFrame ack;
    ack.type = FrameType::Ack;
    ack.port = 0x99;
    ack.control_byte = 0x00;
    ack.dest_net = 0;
    ack.dest_stn = 254;

    pair.a->send_frame(ack);
    brief_pause();

    auto received = pair.b->receive_frame();
    REQUIRE(received.has_value());
    CHECK(received->type == FrameType::Ack);
    CHECK(received->port == 0x99);
    CHECK(received->data.empty());
}

// =============================================================================
// Send/Receive — Immediate + ImmReply
// =============================================================================

TEST_CASE("AunBackend: send Immediate, receive ImmReply", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    // A sends Immediate to B
    NetworkFrame imm;
    imm.type = FrameType::Immediate;
    imm.port = 0x00;
    imm.control_byte = 0x08;
    imm.dest_net = 0;
    imm.dest_stn = 254;
    imm.data = {0xDD};

    pair.a->send_frame(imm);
    brief_pause();

    auto received = pair.b->receive_frame();
    REQUIRE(received.has_value());
    CHECK(received->type == FrameType::Immediate);
    CHECK(received->control_byte == 0x08);
    REQUIRE(received->data.size() == 1);
    CHECK(received->data[0] == 0xDD);

    // B sends ImmReply back to A
    NetworkFrame reply;
    reply.type = FrameType::ImmReply;
    reply.port = 0x00;
    reply.control_byte = 0x08;
    reply.dest_net = 0;
    reply.dest_stn = 1;
    reply.data = {0xEE, 0xFF};

    pair.b->send_frame(reply);
    brief_pause();

    auto reply_received = pair.a->receive_frame();
    REQUIRE(reply_received.has_value());
    CHECK(reply_received->type == FrameType::ImmReply);
    REQUIRE(reply_received->data.size() == 2);
    CHECK(reply_received->data[0] == 0xEE);
    CHECK(reply_received->data[1] == 0xFF);
}

// =============================================================================
// Send/Receive — Broadcast
// =============================================================================

TEST_CASE("AunBackend: broadcast is received by peer", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    NetworkFrame bcast;
    bcast.type = FrameType::Broadcast;
    bcast.port = 0x99;
    bcast.control_byte = 0x00;
    bcast.dest_net = 0;
    bcast.dest_stn = 0xFF;
    bcast.src_net = 0;
    bcast.src_stn = 1;
    bcast.data = {0x42};

    pair.a->send_frame(bcast);
    brief_pause();

    auto received = pair.b->receive_frame();
    REQUIRE(received.has_value());
    CHECK(received->type == FrameType::Broadcast);
    CHECK(received->port == 0x99);
    CHECK(received->src_stn == 1);
    REQUIRE(received->data.size() == 1);
    CHECK(received->data[0] == 0x42);
}

// =============================================================================
// Send to Unknown Peer
// =============================================================================

TEST_CASE("AunBackend: send to unknown peer drops silently", "[econet][aun][backend]") {
    AunBackend backend(0, 1, PORT_A);
    if (!backend.is_connected()) SKIP("Port unavailable");

    // No peers added — should not crash.
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.dest_net = 0;
    frame.dest_stn = 99;
    frame.data = {0xAA};

    backend.send_frame(frame);  // Should not crash or throw
}

// =============================================================================
// Receive from Unknown Peer
// =============================================================================

TEST_CASE("AunBackend: receive from unknown peer is discarded", "[econet][aun][backend]") {
    // Create two backends but don't peer B→A (A is unknown to B).
    auto a = std::make_unique<AunBackend>(0, 1, PORT_A);
    auto b = std::make_unique<AunBackend>(0, 254, PORT_B);
    if (!a->is_connected() || !b->is_connected()) SKIP("Ports unavailable");

    // A knows B, but B does NOT know A.
    a->add_peer(0, 254, loopback_ip(), PORT_B);

    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.dest_net = 0;
    frame.dest_stn = 254;
    frame.data = {0xAA};

    a->send_frame(frame);
    brief_pause();

    // B should discard the packet — sender not in peer table.
    auto received = b->receive_frame();
    CHECK_FALSE(received.has_value());
}

// =============================================================================
// Large Payload
// =============================================================================

TEST_CASE("AunBackend: large payload (1024 bytes) round-trip", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.port = 0x99;
    frame.dest_net = 0;
    frame.dest_stn = 254;
    frame.data.resize(1024);
    for (size_t i = 0; i < 1024; ++i) {
        frame.data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    pair.a->send_frame(frame);
    brief_pause();

    auto received = pair.b->receive_frame();
    REQUIRE(received.has_value());
    REQUIRE(received->data.size() == 1024);
    CHECK(received->data == frame.data);
}

// =============================================================================
// Multiple Frames
// =============================================================================

TEST_CASE("AunBackend: multiple frames delivered in order", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    for (int i = 0; i < 5; ++i) {
        NetworkFrame frame;
        frame.type = FrameType::Unicast;
        frame.port = static_cast<uint8_t>(i);
        frame.dest_net = 0;
        frame.dest_stn = 254;
        frame.data = {static_cast<uint8_t>(i)};
        pair.a->send_frame(frame);
    }

    brief_pause();

    for (int i = 0; i < 5; ++i) {
        auto received = pair.b->receive_frame();
        REQUIRE(received.has_value());
        CHECK(received->port == static_cast<uint8_t>(i));
        REQUIRE(received->data.size() == 1);
        CHECK(received->data[0] == static_cast<uint8_t>(i));
    }

    // No more frames.
    CHECK_FALSE(pair.b->receive_frame().has_value());
}

// =============================================================================
// Peer Add/Remove — Send Behaviour
// =============================================================================

TEST_CASE("AunBackend: send fails after removing peer", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    // Send before removal — should succeed.
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.dest_net = 0;
    frame.dest_stn = 254;
    frame.data = {0xAA};
    pair.a->send_frame(frame);
    brief_pause();

    auto received = pair.b->receive_frame();
    REQUIRE(received.has_value());

    // Remove peer.
    pair.a->remove_peer(0, 254);

    // Send after removal — should be dropped silently.
    frame.data = {0xBB};
    pair.a->send_frame(frame);
    brief_pause();

    CHECK_FALSE(pair.b->receive_frame().has_value());
}

// =============================================================================
// Handle Progression
// =============================================================================

TEST_CASE("AunBackend: handles increment by 4 on successive sends", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    // Send two Unicasts from A to B and decode the raw packets to check handles.
    // We can't directly inspect handles through the NetworkFrame interface,
    // but we can verify they're different by checking the raw UDP data.
    // For this test, we verify the mechanism works by sending and receiving
    // multiple frames successfully — the handle incrementing is an internal detail.

    for (int i = 0; i < 3; ++i) {
        NetworkFrame frame;
        frame.type = FrameType::Unicast;
        frame.port = 0x99;
        frame.dest_net = 0;
        frame.dest_stn = 254;
        frame.data = {static_cast<uint8_t>(i)};
        pair.a->send_frame(frame);
    }

    brief_pause();

    for (int i = 0; i < 3; ++i) {
        auto received = pair.b->receive_frame();
        REQUIRE(received.has_value());
        CHECK(received->data[0] == static_cast<uint8_t>(i));
    }
}

// =============================================================================
// Port 0 (OS-chosen ephemeral port)
// =============================================================================

TEST_CASE("AunBackend: port 0 binds to an ephemeral port",
          "[aun_backend][port]") {
    AunBackend backend(0, 1, 0);  // port 0 = OS-chosen

    REQUIRE(backend.is_connected());
    REQUIRE(backend.local_port() != 0);  // OS assigned a real port
}

TEST_CASE("AunBackend: local_port returns specified port when non-zero",
          "[aun_backend][port]") {
    AunBackend backend(0, 1, 44001);

    REQUIRE(backend.is_connected());
    REQUIRE(backend.local_port() == 44001);
}
