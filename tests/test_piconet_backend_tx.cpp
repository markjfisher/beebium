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
#include "beebium/econet/piconet/Base64.hpp"

#include "piconet/MockPiconetSerial.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace beebium;
using namespace beebium::piconet;
using beebium::piconet::test::MockPiconetSerial;

namespace {

// Convenience: build a PiconetBackend wired up to a mock and return both.
struct Wired {
    MockPiconetSerial* mock;
    std::unique_ptr<PiconetBackend> backend;
};

Wired make_backend() {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    auto backend = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null"}, std::move(mock_owner));
    return {mock, std::move(backend)};
}

NetworkFrame make_unicast(std::uint8_t dest_stn, std::uint8_t dest_net,
                          std::uint8_t ctrl, std::uint8_t port,
                          std::vector<std::uint8_t> data) {
    NetworkFrame f;
    f.type = FrameType::Unicast;
    f.dest_stn = dest_stn;
    f.dest_net = dest_net;
    f.src_stn = 0;  // ignored by Piconet -- it stamps its own SET_STATION value
    f.src_net = 0;
    f.control_byte = ctrl;
    f.port = port;
    f.data = std::move(data);
    return f;
}

}  // namespace

TEST_CASE("PiconetBackend: send_frame(Unicast, FILES ctrl) issues TX without scout-extra",
          "[piconet][backend][tx]") {
    auto w = make_backend();
    // Standard fileserver call: ctrl=0x80 (FILES), port=0x99, no scout-extra
    // bytes. FourWayHandshake's nf.data is just the data-frame payload.
    const std::vector<std::uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD};
    w.backend->send_frame(make_unicast(0x32, 0, 0x80, 0x99, payload));

    REQUIRE(w.mock->write_count() == 1);
    const std::string expected =
        "TX 50 0 128 153 " + encode_base64(payload) + "\r";
    CHECK(w.mock->write_as_string(0) == expected);
}

TEST_CASE("PiconetBackend: send_frame(Unicast, USERPROC ctrl) splits scout-extra from data",
          "[piconet][backend][tx]") {
    // ctrl=0x84 (USERPROC) carries 4 scout-extra bytes per FourWayHandshake.
    // FourWayHandshake packs nf.data as [4 scout-extra bytes][data payload].
    auto w = make_backend();
    const std::vector<std::uint8_t> nf_data{
        0x11, 0x22, 0x33, 0x44,  // scout extra (4 bytes for USERPROC)
        0xDE, 0xAD, 0xBE, 0xEF   // data payload
    };
    w.backend->send_frame(make_unicast(254, 0, 0x84, 0, nf_data));

    REQUIRE(w.mock->write_count() == 1);
    const std::vector<std::uint8_t> expected_scout{0x11, 0x22, 0x33, 0x44};
    const std::vector<std::uint8_t> expected_data{0xDE, 0xAD, 0xBE, 0xEF};
    const std::string expected =
        "TX 254 0 132 0 " + encode_base64(expected_data)
                          + " " + encode_base64(expected_scout) + "\r";
    CHECK(w.mock->write_as_string(0) == expected);
}

TEST_CASE("PiconetBackend: send_frame(Unicast, POKE ctrl) splits 8 scout-extra bytes",
          "[piconet][backend][tx]") {
    auto w = make_backend();
    // ctrl=0x82 (POKE) carries 8 scout-extra bytes.
    const std::vector<std::uint8_t> nf_data{
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,  // scout extra
        0xFF, 0xFE  // data payload
    };
    w.backend->send_frame(make_unicast(1, 0, 0x82, 0, nf_data));

    REQUIRE(w.mock->write_count() == 1);
    const std::vector<std::uint8_t> expected_scout{
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const std::vector<std::uint8_t> expected_data{0xFF, 0xFE};
    const std::string expected =
        "TX 1 0 130 0 " + encode_base64(expected_data)
                        + " " + encode_base64(expected_scout) + "\r";
    CHECK(w.mock->write_as_string(0) == expected);
}

TEST_CASE("PiconetBackend: send_frame(Broadcast) prepends ctrl and port to data",
          "[piconet][backend][tx]") {
    // FourWayHandshake puts the broadcast payload (after ctrl+port) in
    // nf.data, with ctrl/port in the named fields. Piconet's BCAST
    // command takes the bytes that go on the wire AFTER the 4-byte
    // address header, which means [ctrl, port, payload...]. The backend
    // must reconstruct that.
    auto w = make_backend();
    NetworkFrame f;
    f.type = FrameType::Broadcast;
    f.dest_stn = 0xFF;
    f.dest_net = 0xFF;
    f.control_byte = 0x82;
    f.port = 0x9C;
    f.data = {0xCA, 0xFE, 0xBA, 0xBE};

    w.backend->send_frame(f);

    REQUIRE(w.mock->write_count() == 1);
    const std::vector<std::uint8_t> expected_wire{
        0x82, 0x9C, 0xCA, 0xFE, 0xBA, 0xBE};
    CHECK(w.mock->write_as_string(0) == "BCAST " + encode_base64(expected_wire) + "\r");
}

TEST_CASE("PiconetBackend: send_frame(Immediate) issues TX with port 0 and no scout-extra",
          "[piconet][backend][tx]") {
    auto w = make_backend();
    // Outbound immediate, e.g. ctrl=0x88 (MachinePeek). nf.data is the
    // immediate operation's payload (typically empty for MachinePeek).
    NetworkFrame f;
    f.type = FrameType::Immediate;
    f.dest_stn = 5;
    f.dest_net = 0;
    f.control_byte = 0x88;
    f.port = 0;  // Immediates always port 0
    f.data = {};

    w.backend->send_frame(f);

    REQUIRE(w.mock->write_count() == 1);
    CHECK(w.mock->write_as_string(0) == "TX 5 0 136 0 \r");
}

TEST_CASE("PiconetBackend: send_frame(Ack) is dropped silently",
          "[piconet][backend][tx]") {
    // FourWayHandshake's synthesised Ack frames have nowhere to go --
    // Piconet's wire-level handshake completed before the host saw
    // TX_RESULT.
    auto w = make_backend();
    NetworkFrame f;
    f.type = FrameType::Ack;
    f.dest_stn = 32;
    f.dest_net = 0;
    f.control_byte = 0x80;
    f.port = 0x99;

    w.backend->send_frame(f);
    CHECK(w.mock->write_count() == 0);
}

TEST_CASE("PiconetBackend: send_frame(ImmReply) is dropped silently",
          "[piconet][backend][tx]") {
    // Piconet's REPLY path is unsupported (the firmware feature was
    // abandoned upstream); we cannot deliver host-generated replies to
    // inbound immediate operations. Drop them; FourWayHandshake's
    // watchdog handles cleanup.
    auto w = make_backend();
    NetworkFrame f;
    f.type = FrameType::ImmReply;
    f.dest_stn = 32;
    f.dest_net = 0;
    f.data = {0x55, 0x4A, 0x00, 0x05};  // Pretend MachinePeek-style reply

    w.backend->send_frame(f);
    CHECK(w.mock->write_count() == 0);
}

TEST_CASE("PiconetBackend: send_frame(RawFrame) is dropped silently",
          "[piconet][backend][tx]") {
    // PiconetBackend always runs in aun_mode (FourWayHandshake decorator
    // active), so RawFrame should never reach it. Defensive drop.
    auto w = make_backend();
    NetworkFrame f;
    f.type = FrameType::RawFrame;
    f.data = {0x01, 0x02, 0x03};

    w.backend->send_frame(f);
    CHECK(w.mock->write_count() == 0);
}

TEST_CASE("PiconetBackend: send_frame after close drops silently",
          "[piconet][backend][tx]") {
    auto w = make_backend();
    w.mock->close();
    CHECK_FALSE(w.backend->is_connected());

    w.backend->send_frame(make_unicast(1, 0, 0x80, 0x99, {0xAA}));
    CHECK(w.mock->write_count() == 0);
}

TEST_CASE("PiconetBackend: receive_frame is nullopt in phase 3",
          "[piconet][backend][tx]") {
    // Phase 4 will replace this with a queue draining from the reader
    // thread. For now: never returns a frame.
    auto w = make_backend();
    CHECK_FALSE(w.backend->receive_frame().has_value());
}

TEST_CASE("PiconetBackend: is_connected reflects the SerialPort state",
          "[piconet][backend][tx]") {
    auto w = make_backend();
    CHECK(w.backend->is_connected());
    w.mock->close();
    CHECK_FALSE(w.backend->is_connected());
    w.mock->set_open(true);
    CHECK(w.backend->is_connected());
}
