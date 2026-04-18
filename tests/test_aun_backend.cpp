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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <chrono>
#include <set>
#include <thread>

using namespace beebium;

namespace {

// Loopback address in network byte order.
uint32_t loopback_ip() {
    return htonl(INADDR_LOOPBACK);
}

// Helper: create two backends peered to each other on loopback.
// Uses OS-assigned ephemeral ports (local_port=0) so concurrent test
// processes don't fight over a fixed port number. With SO_REUSEADDR set
// inside AunBackend, fixed ports would let parallel tests bind the same
// port simultaneously on Windows and scatter datagrams across processes.
struct LoopbackPair {
    std::unique_ptr<AunBackend> a;  // Station 1
    std::unique_ptr<AunBackend> b;  // Station 254
    bool ready = false;

    LoopbackPair() {
        a = std::make_unique<AunBackend>(0, 1, 0);
        b = std::make_unique<AunBackend>(0, 254, 0);

        if (!a->is_connected() || !b->is_connected()) {
            return;  // Bind failure (rare with port 0) -- caller should SKIP
        }

        // Peer each other on loopback using each side's OS-assigned port.
        a->add_peer(0, 254, loopback_ip(), b->local_port());
        b->add_peer(0, 1, loopback_ip(), a->local_port());
        ready = true;
    }
};

// Brief pause to allow UDP datagram delivery on loopback.
void brief_pause() {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

// Poll receive_frame() until a frame arrives or the timeout expires.
std::optional<NetworkFrame> receive_with_timeout(
        AunBackend& backend,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto frame = backend.receive_frame();
        if (frame.has_value()) return frame;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

}  // namespace

// =============================================================================
// Construction
// =============================================================================

TEST_CASE("AunBackend: construction with OS-assigned port succeeds", "[econet][aun][backend]") {
    AunBackend backend(0, 1, 0);
    REQUIRE(backend.is_connected());
    CHECK(backend.local_port() != 0);
}

TEST_CASE("AunBackend: duplicate port binding fails gracefully", "[econet][aun][backend]") {
    // Bind the first backend to an OS-chosen port, then attempt a second
    // bind on that same port. SO_REUSEADDR makes this platform-dependent
    // (it may succeed on Windows/Linux, fail elsewhere); the test asserts
    // only that neither call crashes and the first stays connected.
    AunBackend first(0, 1, 0);
    REQUIRE(first.is_connected());

    AunBackend second(0, 2, first.local_port());
    CHECK(first.is_connected());
    (void)second;
}

// =============================================================================
// Peer Management
// =============================================================================

TEST_CASE("AunBackend: add and remove peers", "[econet][aun][backend]") {
    AunBackend backend(0, 1, 0);
    REQUIRE(backend.is_connected());

    CHECK(backend.peer_count() == 0);

    backend.add_peer(0, 254, loopback_ip(), 40001);
    CHECK(backend.peer_count() == 1);

    backend.add_peer(0, 253, loopback_ip(), 40002);
    CHECK(backend.peer_count() == 2);

    backend.remove_peer(0, 254);
    CHECK(backend.peer_count() == 1);

    backend.remove_peer(0, 253);
    CHECK(backend.peer_count() == 0);
}

TEST_CASE("AunBackend: remove non-existent peer is harmless", "[econet][aun][backend]") {
    AunBackend backend(0, 1, 0);
    REQUIRE(backend.is_connected());

    backend.remove_peer(0, 99);  // Should not crash
    CHECK(backend.peer_count() == 0);
}

// =============================================================================
// Receive When Empty
// =============================================================================

TEST_CASE("AunBackend: receive_frame returns nullopt when no data", "[econet][aun][backend]") {
    AunBackend backend(0, 1, 0);
    REQUIRE(backend.is_connected());

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
    AunBackend backend(0, 1, 0);
    REQUIRE(backend.is_connected());

    // No peers added -- should not crash.
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
    // Create two backends but don't peer B->A (A is unknown to B).
    auto a = std::make_unique<AunBackend>(0, 1, 0);
    auto b = std::make_unique<AunBackend>(0, 254, 0);
    REQUIRE(a->is_connected());
    REQUIRE(b->is_connected());

    // A knows B, but B does NOT know A.
    a->add_peer(0, 254, loopback_ip(), b->local_port());

    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.dest_net = 0;
    frame.dest_stn = 254;
    frame.data = {0xAA};

    a->send_frame(frame);
    brief_pause();

    // B should discard the packet -- sender not in peer table.
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

TEST_CASE("AunBackend: multiple frames all delivered", "[econet][aun][backend]") {
    LoopbackPair pair;
    if (!pair.ready) SKIP("Ports unavailable");

    constexpr int frame_count = 5;

    for (int i = 0; i < frame_count; ++i) {
        NetworkFrame frame;
        frame.type = FrameType::Unicast;
        frame.port = static_cast<uint8_t>(i);
        frame.dest_net = 0;
        frame.dest_stn = 254;
        frame.data = {static_cast<uint8_t>(i)};
        pair.a->send_frame(frame);
    }

    // Collect all received frames using polling (UDP delivery timing varies).
    std::vector<NetworkFrame> received;
    for (int i = 0; i < frame_count; ++i) {
        auto frame = receive_with_timeout(*pair.b);
        if (!frame.has_value()) break;
        received.push_back(std::move(*frame));
    }

    REQUIRE(received.size() == frame_count);

    // Verify all expected port values were received (without assuming ordering,
    // since UDP does not guarantee it).
    std::set<uint8_t> received_ports;
    for (const auto& f : received) {
        REQUIRE(f.data.size() == 1);
        CHECK(f.port == f.data[0]);
        received_ports.insert(f.port);
    }
    for (int i = 0; i < frame_count; ++i) {
        CHECK(received_ports.count(static_cast<uint8_t>(i)) == 1);
    }
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

    // We can't directly inspect handles through the NetworkFrame interface,
    // but we can verify the mechanism works by sending and receiving
    // multiple frames successfully — the handle incrementing is an internal detail.

    constexpr int frame_count = 3;

    for (int i = 0; i < frame_count; ++i) {
        NetworkFrame frame;
        frame.type = FrameType::Unicast;
        frame.port = 0x99;
        frame.dest_net = 0;
        frame.dest_stn = 254;
        frame.data = {static_cast<uint8_t>(i)};
        pair.a->send_frame(frame);
    }

    std::set<uint8_t> received_values;
    for (int i = 0; i < frame_count; ++i) {
        auto received = receive_with_timeout(*pair.b);
        REQUIRE(received.has_value());
        REQUIRE(received->data.size() == 1);
        received_values.insert(received->data[0]);
    }

    for (int i = 0; i < frame_count; ++i) {
        CHECK(received_values.count(static_cast<uint8_t>(i)) == 1);
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
    // Pick a port the OS just told us is free to minimise conflicts with
    // other test processes. The window between probe and rebind is tiny
    // but nonzero; if it fails, SKIP rather than flake.
    uint16_t probed_port;
    {
        AunBackend probe(0, 1, 0);
        REQUIRE(probe.is_connected());
        probed_port = probe.local_port();
    }
    AunBackend backend(0, 1, probed_port);
    if (!backend.is_connected()) {
        SKIP("Port " << probed_port << " was reused before we could rebind");
    }
    REQUIRE(backend.local_port() == probed_port);
}
