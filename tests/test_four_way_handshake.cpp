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

#include <beebium/econet/EconetSocket.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

namespace {

// Helper to create a RawFrame from byte data (as the ADLC would send).
NetworkFrame make_raw_frame(std::vector<uint8_t> data) {
    NetworkFrame frame;
    frame.type = FrameType::RawFrame;
    frame.data = std::move(data);
    return frame;
}

// Helper to tick the handshake N times.
void tick_n(FourWayHandshake& hs, int n) {
    for (int i = 0; i < n; ++i) {
        hs.tick();
    }
}

// Econet frame byte offsets
constexpr int DEST_STN = 0;
constexpr int DEST_NET = 1;
constexpr int SRC_STN  = 2;
constexpr int SRC_NET  = 3;
constexpr int CTRL     = 4;
constexpr int PORT     = 5;

}  // namespace

// =============================================================================
// Initial State
// =============================================================================

TEST_CASE("FourWayHandshake: initial state is Idle", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
}

TEST_CASE("FourWayHandshake: receive_frame returns nullopt when idle and no packets", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    CHECK_FALSE(hs.receive_frame().has_value());
}

TEST_CASE("FourWayHandshake: is_connected delegates to backend", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    CHECK(hs.is_connected());
    backend.set_connected(false);
    CHECK_FALSE(hs.is_connected());
}

TEST_CASE("FourWayHandshake: is_receiving_flags true when idle and connected (clock box)", "[econet][handshake]") {
    TestBackend backend;
    backend.set_connected(true);
    FourWayHandshake hs(backend);

    // Idle + connected = clock box flag fill active
    CHECK(hs.is_receiving_flags());
}

TEST_CASE("FourWayHandshake: is_receiving_flags false when idle and disconnected", "[econet][handshake]") {
    TestBackend backend;
    backend.set_connected(false);
    FourWayHandshake hs(backend);

    // Idle + disconnected = no clock box, no flag fill
    CHECK_FALSE(hs.is_receiving_flags());
}

// =============================================================================
// Scout Payload Sizes
// =============================================================================

TEST_CASE("FourWayHandshake: scout_payload_size for POKE (0x02) is 8", "[econet][handshake]") {
    CHECK(FourWayHandshake::scout_payload_size(0x02) == 8);
    CHECK(FourWayHandshake::scout_payload_size(0x82) == 8);  // With bit 7
}

TEST_CASE("FourWayHandshake: scout_payload_size for JSR/UserProc/OSProc is 4", "[econet][handshake]") {
    CHECK(FourWayHandshake::scout_payload_size(0x03) == 4);
    CHECK(FourWayHandshake::scout_payload_size(0x04) == 4);
    CHECK(FourWayHandshake::scout_payload_size(0x05) == 4);
    CHECK(FourWayHandshake::scout_payload_size(0x83) == 4);  // With bit 7
}

TEST_CASE("FourWayHandshake: scout_payload_size for other control bytes is 0", "[econet][handshake]") {
    CHECK(FourWayHandshake::scout_payload_size(0x00) == 0);
    CHECK(FourWayHandshake::scout_payload_size(0x01) == 0);
    CHECK(FourWayHandshake::scout_payload_size(0x80) == 0);  // 0x00 with bit 7
    CHECK(FourWayHandshake::scout_payload_size(0x06) == 0);
    CHECK(FourWayHandshake::scout_payload_size(0xFF) == 0);
}

// =============================================================================
// TX: Unicast — Full Four-Way Handshake (Scout → ScoutAck → Data → Ack)
// =============================================================================

TEST_CASE("FourWayHandshake: TX unicast scout is held, not sent to backend", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends a scout: dest=254.0, src=1.0, ctrl=0x80, port=0x99
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(backend.sent_frame_count() == 0);  // Scout NOT sent to network
    CHECK(hs.flag_fill_active());
}

TEST_CASE("FourWayHandshake: TX unicast fake scout ack delivered after timeout", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends scout
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // Tick until scout ack timeout fires
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    // ADLC should now be able to receive the fake scout ack
    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    // Scout ack is a 4-byte frame: src/dest swapped from scout
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Ack TO us (src of scout)
    CHECK(frame->data[DEST_NET] == 0);
    CHECK(frame->data[SRC_STN] == 254);   // Ack FROM them (dest of scout)
    CHECK(frame->data[SRC_NET] == 0);
}

TEST_CASE("FourWayHandshake: TX unicast data sent as AUN Unicast after scout ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // Consume fake scout ack

    // Beeb sends data frame (4 address bytes + payload)
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xAA, 0xBB, 0xCC}));

    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);
    REQUIRE(backend.sent_frame_count() == 1);

    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Unicast);
    CHECK(sent.dest_stn == 254);
    CHECK(sent.dest_net == 0);
    CHECK(sent.src_stn == 1);
    CHECK(sent.src_net == 0);
    CHECK(sent.port == 0x99);
    CHECK(sent.control_byte == 0x00);  // 0x80 & 0x7F = 0x00

    // Payload: data frame bytes after the 4 address bytes
    REQUIRE(sent.data.size() == 3);
    CHECK(sent.data[0] == 0xAA);
    CHECK(sent.data[1] == 0xBB);
    CHECK(sent.data[2] == 0xCC);
}

TEST_CASE("FourWayHandshake: TX unicast remote ack generates fake final ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout → timeout → data
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xAA}));

    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // Remote sends AUN Ack
    NetworkFrame ack;
    ack.type = FrameType::Ack;
    ack.dest_stn = 1;
    ack.dest_net = 0;
    ack.src_stn = 254;
    ack.src_net = 0;
    backend.inject_rx_network_frame(ack);

    // Handshake should deliver a fake final ack to the ADLC
    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Final ack TO us
    CHECK(frame->data[SRC_STN] == 254);   // Final ack FROM them

    // Should transition to WaitForIdle, then Idle once queue empty
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
}

// =============================================================================
// TX: Broadcast — Fire-and-Forget
// =============================================================================

TEST_CASE("FourWayHandshake: TX broadcast sent immediately to backend", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends broadcast: dest station 0xFF
    hs.send_frame(make_raw_frame({0xFF, 0, 1, 0, 0x80, 0x99, 0x42}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Broadcast);
    CHECK(sent.dest_stn == 0xFF);
    CHECK(sent.src_stn == 1);
    CHECK(sent.port == 0x99);
    CHECK(sent.control_byte == 0x00);  // 0x80 & 0x7F

    REQUIRE(sent.data.size() == 1);
    CHECK(sent.data[0] == 0x42);

    // Transitions through WaitForIdle
    CHECK(hs.flag_fill_active());
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// TX: Immediate Operations
// =============================================================================

TEST_CASE("FourWayHandshake: TX immediate sent to backend, waits for reply", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends immediate: port 0x00
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x88, 0x00, 0xDD}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ImmediateSent);
    REQUIRE(backend.sent_frame_count() == 1);

    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Immediate);
    CHECK(sent.port == 0x00);
    CHECK(sent.control_byte == 0x08);  // 0x88 & 0x7F
    REQUIRE(sent.data.size() == 1);
    CHECK(sent.data[0] == 0xDD);
}

TEST_CASE("FourWayHandshake: TX immediate reply received and delivered", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Send immediate
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x88, 0x00}));

    // Remote replies
    NetworkFrame reply;
    reply.type = FrameType::ImmReply;
    reply.dest_stn = 1;
    reply.dest_net = 0;
    reply.src_stn = 254;
    reply.src_net = 0;
    reply.data = {0xEE, 0xFF};
    backend.inject_rx_network_frame(reply);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    // 4 address bytes + reply payload
    REQUIRE(frame->data.size() == 6);
    CHECK(frame->data[DEST_STN] == 1);    // TO us
    CHECK(frame->data[SRC_STN] == 254);   // FROM them
    CHECK(frame->data[4] == 0xEE);
    CHECK(frame->data[5] == 0xFF);

    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// RX: Unicast — Incoming AUN Data → Four-Way Handshake for Beeb
// =============================================================================

TEST_CASE("FourWayHandshake: RX unicast delivers scout frame to Beeb", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Remote sends AUN Unicast to us
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;  // AUN ctrl (bit 7 clear)
    pkt.port = 0x99;
    pkt.data = {0xAA, 0xBB};  // Payload
    backend.inject_rx_network_frame(pkt);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutReceived);
    CHECK(hs.flag_fill_active());

    // Scout frame: dest, dest_net, src, src_net, ctrl|0x80, port
    REQUIRE(frame->data.size() >= 6);
    CHECK(frame->data[DEST_STN] == 1);
    CHECK(frame->data[DEST_NET] == 0);
    CHECK(frame->data[SRC_STN] == 254);
    CHECK(frame->data[SRC_NET] == 0);
    CHECK(frame->data[CTRL] == 0x80);  // 0x00 | 0x80
    CHECK(frame->data[PORT] == 0x99);
}

TEST_CASE("FourWayHandshake: RX unicast Beeb sends scout ack, data delivered after timeout", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Inject unicast
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0xAA, 0xBB};
    backend.inject_rx_network_frame(pkt);

    // Consume scout
    hs.receive_frame();
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutReceived);

    // Beeb sends scout ack (4 address bytes)
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckSent);

    // Nothing sent to network (scout ack is local)
    CHECK(backend.sent_frame_count() == 0);

    // Tick until timeout delivers cached data
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    CHECK(hs.stage() == FourWayHandshake::Stage::DataReceived);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    // Data frame: 4 address bytes + payload
    REQUIRE(frame->data.size() == 6);
    CHECK(frame->data[DEST_STN] == 1);    // dest = us
    CHECK(frame->data[SRC_STN] == 254);   // src = them
    CHECK(frame->data[4] == 0xAA);
    CHECK(frame->data[5] == 0xBB);
}

TEST_CASE("FourWayHandshake: RX unicast Beeb sends final ack, AUN Ack sent to network", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Full RX flow: unicast → scout → scout ack → data → final ack
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0xAA};
    backend.inject_rx_network_frame(pkt);

    hs.receive_frame();  // Scout
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));  // Scout ack
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // Data

    CHECK(hs.stage() == FourWayHandshake::Stage::DataReceived);

    // Beeb sends final ack
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));

    // AUN Ack should be sent to network
    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Ack);
    CHECK(sent.dest_stn == 254);  // Ack TO the original source
    CHECK(sent.src_stn == 1);     // Ack FROM us

    CHECK_FALSE(hs.flag_fill_active());
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// RX: Broadcast — Delivered Directly
// =============================================================================

TEST_CASE("FourWayHandshake: RX broadcast delivered directly, stays in Idle", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    NetworkFrame pkt;
    pkt.type = FrameType::Broadcast;
    pkt.dest_stn = 0xFF;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0x42};
    backend.inject_rx_network_frame(pkt);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);

    // Econet frame: dest, dest_net, src, src_net, ctrl|0x80, port, data
    REQUIRE(frame->data.size() == 7);
    CHECK(frame->data[DEST_STN] == 0xFF);
    CHECK(frame->data[SRC_STN] == 254);
    CHECK(frame->data[CTRL] == 0x80);
    CHECK(frame->data[PORT] == 0x99);
    CHECK(frame->data[6] == 0x42);

    // Stays in Idle — broadcasts are fire-and-forget
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// RX: Immediate Operations
// =============================================================================

TEST_CASE("FourWayHandshake: RX immediate delivered, Beeb reply sent as ImmReply", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Remote sends immediate request
    NetworkFrame pkt;
    pkt.type = FrameType::Immediate;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x08;
    pkt.port = 0x00;
    pkt.data = {};
    backend.inject_rx_network_frame(pkt);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::ImmediateReceived);

    // Econet frame delivered to Beeb
    REQUIRE(frame->data.size() >= 6);
    CHECK(frame->data[DEST_STN] == 1);
    CHECK(frame->data[SRC_STN] == 254);
    CHECK(frame->data[CTRL] == 0x88);  // 0x08 | 0x80
    CHECK(frame->data[PORT] == 0x00);

    // Beeb sends reply
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xEE, 0xFF}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::ImmReply);
    CHECK(sent.dest_stn == 254);  // Reply TO requester
    CHECK(sent.src_stn == 1);     // Reply FROM us
    REQUIRE(sent.data.size() == 2);
    CHECK(sent.data[0] == 0xEE);
    CHECK(sent.data[1] == 0xFF);

    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// TX: Port-0 Classification — Unicast (POKE/JSR/UserProc/OSProc) vs Immediate
// =============================================================================

TEST_CASE("FourWayHandshake: TX port-0 with ctrl 0x82 (POKE) is Unicast scout, not Immediate", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Port 0 + ctrl 0x82 = POKE = Unicast scout with 8 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x82, 0x00,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(backend.sent_frame_count() == 0);  // Scout NOT sent (held for four-way)
}

TEST_CASE("FourWayHandshake: TX port-0 with ctrl 0x83 (JSR) is Unicast scout, not Immediate", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Port 0 + ctrl 0x83 = JSR = Unicast scout with 4 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x83, 0x00,
        0x01, 0x02, 0x03, 0x04}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(backend.sent_frame_count() == 0);
}

TEST_CASE("FourWayHandshake: TX port-0 with ctrl 0x88 (Machine Peek) is Immediate", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Port 0 + ctrl 0x88 = Machine Peek = Immediate
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x88, 0x00}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ImmediateSent);
    REQUIRE(backend.sent_frame_count() == 1);
    CHECK(backend.sent_network_frames()[0].type == FrameType::Immediate);
}

// =============================================================================
// TX: Scout Payload Sizes — Extra Bytes in AUN Unicast
// =============================================================================

TEST_CASE("FourWayHandshake: TX unicast with POKE (ctrl 0x82) includes 8 scout extra bytes", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout with ctrl=0x82 and 8 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x82, 0x99,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));

    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();

    // Data frame (4 address bytes + data payload)
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD, 0xEE}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Unicast);
    CHECK(sent.control_byte == 0x02);  // 0x82 & 0x7F

    // AUN payload = 8 scout extra bytes + 2 data payload bytes
    REQUIRE(sent.data.size() == 10);
    CHECK(sent.data[0] == 0x01);
    CHECK(sent.data[7] == 0x08);
    CHECK(sent.data[8] == 0xDD);
    CHECK(sent.data[9] == 0xEE);
}

TEST_CASE("FourWayHandshake: TX unicast with JSR (ctrl 0x83) includes 4 scout extra bytes", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout with ctrl=0x83 and 4 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x83, 0x99,
        0x01, 0x02, 0x03, 0x04}));

    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();

    // Data frame
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];

    // AUN payload = 4 scout extra bytes + 1 data payload byte
    REQUIRE(sent.data.size() == 5);
    CHECK(sent.data[0] == 0x01);
    CHECK(sent.data[3] == 0x04);
    CHECK(sent.data[4] == 0xDD);
}

TEST_CASE("FourWayHandshake: TX unicast with normal ctrl (0x80) has no scout extra bytes", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout with ctrl=0x80, no extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));

    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();

    // Data frame
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD, 0xEE}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];

    // AUN payload = only data payload bytes (no scout extras)
    REQUIRE(sent.data.size() == 2);
    CHECK(sent.data[0] == 0xDD);
    CHECK(sent.data[1] == 0xEE);
}

// =============================================================================
// RX: Scout Payload Sizes — Extra Bytes in Incoming Scout
// =============================================================================

TEST_CASE("FourWayHandshake: RX unicast with POKE splits scout extra from data", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // AUN Unicast with ctrl=0x02 (POKE, 8 extra bytes) and total 10 data bytes
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x02;
    pkt.port = 0x99;
    pkt.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xAA, 0xBB};
    backend.inject_rx_network_frame(pkt);

    // Scout should include the 8 extra bytes
    auto scout = hs.receive_frame();
    REQUIRE(scout.has_value());
    // Scout: 4 addr + ctrl + port + 8 extra = 14 bytes
    REQUIRE(scout->data.size() == 14);
    CHECK(scout->data[CTRL] == 0x82);  // 0x02 | 0x80
    CHECK(scout->data[6] == 0x01);
    CHECK(scout->data[13] == 0x08);

    // Scout ack
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);

    // Data should contain the remaining 2 bytes
    auto data = hs.receive_frame();
    REQUIRE(data.has_value());
    // Data: 4 addr + 2 data bytes = 6 bytes
    REQUIRE(data->data.size() == 6);
    CHECK(data->data[4] == 0xAA);
    CHECK(data->data[5] == 0xBB);
}

// =============================================================================
// Timeouts
// =============================================================================

TEST_CASE("FourWayHandshake: scout ack timeout fires at exactly SCOUT_ACK_TIMEOUT ticks", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // One tick before timeout
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT - 1);
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // Exactly at timeout
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);
}

TEST_CASE("FourWayHandshake: watchdog timeout resets to Idle", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Start a TX unicast but never complete it
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    // Now in ScoutAckReceived — don't send data, let watchdog fire

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    // Watchdog was armed when scout was sent. Remaining time = WATCHDOG - SCOUT_ACK.
    int remaining = FourWayHandshake::WATCHDOG_TIMEOUT - FourWayHandshake::SCOUT_ACK_TIMEOUT;
    tick_n(hs, remaining);

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
}

TEST_CASE("FourWayHandshake: fake final ack delivered after FINAL_ACK_TIMEOUT", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Full TX path: scout → scout ack timeout → data → DataSent
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // Consume fake scout ack
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xAA}));

    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // One tick before the final ack timer fires
    tick_n(hs, FourWayHandshake::FINAL_ACK_TIMEOUT - 1);
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // Exactly at FINAL_ACK_TIMEOUT
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::WaitForIdle);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Final ack TO us (we were src)
    CHECK(frame->data[DEST_NET] == 0);
    CHECK(frame->data[SRC_STN] == 254);   // Final ack FROM them (they were dest)
    CHECK(frame->data[SRC_NET] == 0);
}

TEST_CASE("FourWayHandshake: real AUN Ack before timer cancels fake final ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // When a real server responds with AUN Ack before the FINAL_ACK_TIMEOUT,
    // handle_incoming() delivers the fake final ack and transitions to
    // WaitForIdle. The handshake timer becomes irrelevant.
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD}));
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // Remote server sends AUN Ack (well before FINAL_ACK_TIMEOUT)
    tick_n(hs, 100);
    NetworkFrame ack;
    ack.type = FrameType::Ack;
    ack.dest_stn = 1;
    ack.dest_net = 0;
    ack.src_stn = 254;
    ack.src_net = 0;
    backend.inject_rx_network_frame(ack);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Final ack TO us
    CHECK(frame->data[SRC_STN] == 254);   // Final ack FROM them

    CHECK(hs.stage() == FourWayHandshake::Stage::WaitForIdle);

    // Drain the queue and tick to reach Idle
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

TEST_CASE("FourWayHandshake: watchdog during non-DataSent does not generate fake ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Get into ScoutAckReceived (not DataSent) and let watchdog fire
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    int remaining = FourWayHandshake::WATCHDOG_TIMEOUT - FourWayHandshake::SCOUT_ACK_TIMEOUT;
    tick_n(hs, remaining);

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);

    // No fake ack should be enqueued for non-DataSent stages
    auto frame = hs.receive_frame();
    CHECK_FALSE(frame.has_value());
}

TEST_CASE("FourWayHandshake: flag fill times out after FLAG_FILL_TIMEOUT", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Start a broadcast to activate flag fill
    hs.send_frame(make_raw_frame({0xFF, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.flag_fill_active());

    // Tick one less than timeout
    tick_n(hs, FourWayHandshake::FLAG_FILL_TIMEOUT - 1);
    CHECK(hs.flag_fill_active());

    // At timeout
    hs.tick();
    CHECK_FALSE(hs.flag_fill_active());
}

// =============================================================================
// Reset
// =============================================================================

TEST_CASE("FourWayHandshake: reset clears all state", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Get into a non-idle state
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(hs.flag_fill_active());

    hs.reset();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
    CHECK_FALSE(hs.receive_frame().has_value());
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("FourWayHandshake: malformed frame (too short) is ignored", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Frame with < 4 bytes
    hs.send_frame(make_raw_frame({254, 0, 1}));

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK(backend.sent_frame_count() == 0);
}

TEST_CASE("FourWayHandshake: unexpected TX in wrong state resets handshake", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Get into ScoutSent (TX path waiting for timeout)
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // Send another frame — unexpected in ScoutSent stage
    hs.send_frame(make_raw_frame({253, 0, 1, 0, 0x80, 0x88}));

    // Should reset to Idle
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

TEST_CASE("FourWayHandshake: unexpected AUN packet type in DataSent is ignored", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Full TX path to DataSent
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD}));
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // Inject a Unicast instead of Ack — should be ignored
    NetworkFrame wrong;
    wrong.type = FrameType::Unicast;
    wrong.data = {0x42};
    backend.inject_rx_network_frame(wrong);

    auto frame = hs.receive_frame();
    CHECK_FALSE(frame.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);  // Unchanged
}

TEST_CASE("FourWayHandshake: WaitForIdle transitions to Idle when queue drains", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Broadcast puts us in WaitForIdle with flag fill
    hs.send_frame(make_raw_frame({0xFF, 0, 1, 0, 0x80, 0x99}));

    // Before tick, stage is WaitForIdle
    // After tick, queue is empty → transitions to Idle
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// EconetSocket AUN Mode Integration
// =============================================================================

TEST_CASE("EconetSocket: enable with aun_mode creates FourWayHandshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend), true);

    CHECK(socket.enabled());
    CHECK(socket.adlc() != nullptr);
    CHECK(socket.handshake() != nullptr);
    CHECK(socket.handshake()->stage() == FourWayHandshake::Stage::Idle);
}

TEST_CASE("EconetSocket: enable without aun_mode has no handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend), false);

    CHECK(socket.enabled());
    CHECK(socket.adlc() != nullptr);
    CHECK(socket.handshake() == nullptr);
}

TEST_CASE("EconetSocket: enable with default aun_mode has no handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    CHECK(socket.handshake() == nullptr);
}

TEST_CASE("EconetSocket: disable clears handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend), true);
    CHECK(socket.handshake() != nullptr);

    socket.disable();
    CHECK(socket.handshake() == nullptr);
}

TEST_CASE("EconetSocket: reset resets handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    auto* raw_backend = backend.get();
    socket.enable(1, std::move(backend), true);

    // Put handshake into non-idle state by injecting a unicast
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0xAA};
    raw_backend->inject_rx_network_frame(pkt);

    // Tick to let handshake process the packet
    socket.tick_rising();

    socket.reset();
    CHECK(socket.handshake()->stage() == FourWayHandshake::Stage::Idle);
}

