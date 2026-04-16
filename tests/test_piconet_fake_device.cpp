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

// Tests for FakePiconetDevice in isolation. These verify that the fake
// answers protocol commands the same way the real firmware does -- which
// is what lets us trust integration tests built on top of it.

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/piconet/Base64.hpp"
#include "beebium/econet/piconet/Commands.hpp"
#include "beebium/econet/piconet/Constants.hpp"
#include "beebium/econet/piconet/Events.hpp"

#include "piconet/FakePiconetDevice.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <variant>

using namespace beebium::piconet;
using beebium::piconet::test::FakePiconetDevice;

namespace {

void send(FakePiconetDevice& fake, std::string_view line) {
    auto bytes = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
    auto wr = fake.write(bytes);
    REQUIRE_FALSE(wr.error);
    REQUIRE(wr.bytes == line.size());
}

// Drain the fake's outgoing buffer into a single string of complete
// event lines (without the trailing '\n'). Returns when no more data is
// immediately available (would_block).
std::vector<std::string> drain_lines(FakePiconetDevice& fake) {
    std::string buffer;
    std::array<std::uint8_t, 256> buf{};
    while (true) {
        auto rr = fake.read({buf.data(), buf.size()});
        if (rr.would_block || rr.error) break;
        buffer.append(reinterpret_cast<const char*>(buf.data()), rr.bytes);
    }
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        if (buffer[i] == EVENT_TERMINATOR) {
            lines.emplace_back(buffer.data() + start, i - start);
            start = i + 1;
        }
    }
    return lines;
}

}  // namespace

TEST_CASE("FakePiconetDevice starts in firmware power-on defaults",
          "[piconet][fake]") {
    // Source: piconet/board/src/econet.c line 97 (default station 0x02);
    // piconet.c default mode STOP.
    FakePiconetDevice fake;
    CHECK(fake.station() == DEFAULT_STATION);
    CHECK(fake.mode() == Mode::Stop);
    CHECK(fake.tx_command_count() == 0);
}

TEST_CASE("FakePiconetDevice STATUS reports current state", "[piconet][fake]") {
    FakePiconetDevice fake;
    send(fake, format_status());

    auto lines = drain_lines(fake);
    REQUIRE(lines.size() == 1);
    auto event = parse_event_line(lines[0]);
    auto* status = std::get_if<StatusEvent>(&event);
    REQUIRE(status != nullptr);
    CHECK(status->station == DEFAULT_STATION);
    CHECK(status->mode == Mode::Stop);
    CHECK(status->version_major == 2);  // Default firmware version "2.0.20"
    CHECK(status->version_minor == 0);
    CHECK(status->version_patch == 20);
}

TEST_CASE("FakePiconetDevice STATUS reflects custom firmware version",
          "[piconet][fake]") {
    // Lets PiconetBackend tests validate version-mismatch handling without
    // needing real hardware.
    FakePiconetDevice fake;
    fake.set_firmware_version("3.7.1");
    send(fake, format_status());
    auto lines = drain_lines(fake);
    auto event = parse_event_line(lines.at(0));
    auto* s = std::get_if<StatusEvent>(&event);
    REQUIRE(s);
    CHECK(s->version_major == 3);
    CHECK(s->version_minor == 7);
    CHECK(s->version_patch == 1);
}

TEST_CASE("FakePiconetDevice SET_STATION updates internal station and STATUS",
          "[piconet][fake]") {
    // Source: piconet/board/src/econet.c lines 313-315.
    FakePiconetDevice fake;
    send(fake, format_set_station(42));
    CHECK(fake.station() == 42);

    send(fake, format_status());
    auto lines = drain_lines(fake);
    auto event = parse_event_line(lines.at(0));
    auto* s = std::get_if<StatusEvent>(&event);
    REQUIRE(s);
    CHECK(s->station == 42);
}

TEST_CASE("FakePiconetDevice SET_MODE updates mode field",
          "[piconet][fake]") {
    // Source: piconet/board/src/piconet.c lines 446-459.
    FakePiconetDevice fake;
    send(fake, format_set_mode(Mode::Listen));
    CHECK(fake.mode() == Mode::Listen);
    send(fake, format_set_mode(Mode::Monitor));
    CHECK(fake.mode() == Mode::Monitor);
    send(fake, format_set_mode(Mode::Stop));
    CHECK(fake.mode() == Mode::Stop);
}

TEST_CASE("FakePiconetDevice TX records fields and emits TX_RESULT OK by default",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    const std::vector<std::uint8_t> data{0xAA, 0xBB, 0xCC};
    send(fake, format_tx(0x42, 0, 0x80, 0x99, data, {}));

    CHECK(fake.tx_command_count() == 1);
    CHECK(fake.last_tx_dest_stn() == 0x42);
    CHECK(fake.last_tx_dest_net() == 0);
    CHECK(fake.last_tx_ctrl() == 0x80);
    CHECK(fake.last_tx_port() == 0x99);
    CHECK(fake.last_tx_data() == data);
    CHECK(fake.last_tx_scout_extra().empty());

    auto lines = drain_lines(fake);
    REQUIRE(lines.size() == 1);
    auto event = parse_event_line(lines[0]);
    auto* tx = std::get_if<TxResultEvent>(&event);
    REQUIRE(tx);
    CHECK(tx->result == TxResult::Ok);
}

TEST_CASE("FakePiconetDevice TX with scout_extra splits the two base64 fields",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    const std::vector<std::uint8_t> data{0x11, 0x22};
    const std::vector<std::uint8_t> scout_extra{0x33, 0x44, 0x55, 0x66};
    send(fake, format_tx(254, 0, 0x84, 0, data, scout_extra));

    CHECK(fake.last_tx_data() == data);
    CHECK(fake.last_tx_scout_extra() == scout_extra);
}

TEST_CASE("FakePiconetDevice configurable TX failure sticks for one command",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    fake.set_next_tx_result(TxResult::NoScoutAck);  // Non-sticky.
    const std::vector<std::uint8_t> data1{0xAA};
    send(fake, format_tx(99, 0, 0x80, 0x99, data1, {}));
    auto lines = drain_lines(fake);
    auto first = std::get<TxResultEvent>(parse_event_line(lines.at(0))).result;
    CHECK(first == TxResult::NoScoutAck);

    // Next TX defaults back to Ok.
    const std::vector<std::uint8_t> data2{0xBB};
    send(fake, format_tx(99, 0, 0x80, 0x99, data2, {}));
    auto lines2 = drain_lines(fake);
    auto second = std::get<TxResultEvent>(parse_event_line(lines2.at(0))).result;
    CHECK(second == TxResult::Ok);
}

TEST_CASE("FakePiconetDevice sticky TX failure persists across commands",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    fake.set_next_tx_result(TxResult::LineJammed, /*sticky=*/true);
    for (int i = 0; i < 3; ++i) {
        const std::vector<std::uint8_t> d{static_cast<std::uint8_t>(i)};
        send(fake, format_tx(99, 0, 0x80, 0x99, d, {}));
    }
    auto lines = drain_lines(fake);
    REQUIRE(lines.size() == 3);
    for (const auto& l : lines) {
        auto ev = parse_event_line(l);
        CHECK(std::get<TxResultEvent>(ev).result == TxResult::LineJammed);
    }
}

TEST_CASE("FakePiconetDevice BCAST records data and emits TX_RESULT",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    const std::vector<std::uint8_t> wire{0x80, 0x99, 'H', 'I'};  // ctrl, port, payload
    send(fake, format_bcast(wire));
    CHECK(fake.bcast_command_count() == 1);
    CHECK(fake.last_bcast_data() == wire);
    auto lines = drain_lines(fake);
    REQUIRE(lines.size() == 1);
    auto ev = parse_event_line(lines[0]);
    CHECK(std::get<TxResultEvent>(ev).result == TxResult::Ok);
}

TEST_CASE("FakePiconetDevice STOP mode drops inbound RX_TRANSMIT injections",
          "[piconet][fake]") {
    // Source: piconet/board/src/piconet.c lines 357-359
    // (STOP mode skips RX checks).
    FakePiconetDevice fake;
    // mode is STOP by default
    fake.inject_inbound_unicast(/*src_stn=*/254, /*src_net=*/0,
                                /*ctrl=*/0x80, /*port=*/0x99,
                                /*scout_extra=*/{}, /*data=*/{0xAA});
    auto lines = drain_lines(fake);
    CHECK(lines.empty());
}

TEST_CASE("FakePiconetDevice LISTEN mode delivers inbound to our station",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    send(fake, format_set_mode(Mode::Listen));
    send(fake, format_set_station(32));

    fake.inject_inbound_unicast(/*src_stn=*/254, /*src_net=*/0,
                                /*ctrl=*/0x80, /*port=*/0x99,
                                /*scout_extra=*/{}, /*data=*/{'H', 'I'});

    auto lines = drain_lines(fake);
    // Expect: STATUS reply (none here), so just the RX_TRANSMIT event.
    REQUIRE(!lines.empty());
    // Find the RX_TRANSMIT line (skipping any earlier replies).
    bool found = false;
    for (const auto& l : lines) {
        auto ev = parse_event_line(l);
        if (auto* rx = std::get_if<RxTransmitEvent>(&ev)) {
            CHECK(rx->scout[0] == 32);  // dest = us
            CHECK(rx->scout[2] == 254); // src = them
            CHECK((rx->scout[4] & 0x7F) == 0x00);  // function code (with high bit set on wire = 0x80)
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("FakePiconetDevice LISTEN mode filters inbound for other stations",
          "[piconet][fake]") {
    // Source: piconet/board/src/econet.c line 287, 668 (filter against
    // _listen_addresses).
    FakePiconetDevice fake;
    send(fake, format_set_mode(Mode::Listen));
    send(fake, format_set_station(32));
    drain_lines(fake);  // discard any prior replies

    // Inject a frame addressed to a different station: should be dropped.
    fake.inject_inbound_unicast_to(/*dest_stn=*/99, /*dest_net=*/0,
                                   /*src_stn=*/254, /*src_net=*/0,
                                   /*ctrl=*/0x80, /*port=*/0x99,
                                   /*scout_extra=*/{}, /*data=*/{'X'});
    auto lines = drain_lines(fake);
    bool any_rx = false;
    for (const auto& l : lines) {
        auto ev = parse_event_line(l);
        if (std::holds_alternative<RxTransmitEvent>(ev)) any_rx = true;
    }
    CHECK_FALSE(any_rx);
}

TEST_CASE("FakePiconetDevice auto-replies to inbound MachinePeek without delivering an event",
          "[piconet][fake]") {
    // Source: piconet/board/src/econet.c lines 603-627. The firmware
    // handles MachinePeek inline; the host never sees an RX_IMMEDIATE
    // for it.
    FakePiconetDevice fake;
    send(fake, format_set_mode(Mode::Listen));
    drain_lines(fake);

    fake.inject_inbound_immediate(/*src_stn=*/254, /*src_net=*/0,
                                  MACHINE_PEEK_CTRL, /*data=*/{});
    auto lines = drain_lines(fake);
    CHECK(lines.empty());
    CHECK(fake.machine_peek_count() == 1);
}

TEST_CASE("FakePiconetDevice non-MachinePeek immediates produce RX_IMMEDIATE",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    send(fake, format_set_mode(Mode::Listen));
    drain_lines(fake);

    fake.inject_inbound_immediate(/*src_stn=*/254, /*src_net=*/0,
                                  /*ctrl=*/0x82,  // Halt
                                  /*data=*/{0xFF});
    auto lines = drain_lines(fake);
    REQUIRE(lines.size() == 1);
    auto ev = parse_event_line(lines[0]);
    auto* im = std::get_if<RxImmediateEvent>(&ev);
    REQUIRE(im);
    CHECK((im->scout[4] & 0x7F) == 0x02);  // function code masked from wire 0x82
}

TEST_CASE("FakePiconetDevice broadcast injection delivers RX_BROADCAST in LISTEN",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    send(fake, format_set_mode(Mode::Listen));
    drain_lines(fake);

    fake.inject_inbound_broadcast(/*src_stn=*/254, /*src_net=*/0,
                                  /*ctrl=*/0x9C, /*port=*/0x80,
                                  /*payload=*/{'B', 'C'});
    auto lines = drain_lines(fake);
    REQUIRE(lines.size() == 1);
    auto ev = parse_event_line(lines[0]);
    auto* bc = std::get_if<RxBroadcastEvent>(&ev);
    REQUIRE(bc);
    CHECK(bc->data[0] == 0xFF);  // wire dest
    CHECK(bc->data[2] == 254);   // wire src
}

TEST_CASE("FakePiconetDevice malformed command emits ERROR WHAT??",
          "[piconet][fake]") {
    // Source: piconet/board/src/piconet.c line 495.
    FakePiconetDevice fake;
    send(fake, "GIBBERISH\r");
    auto lines = drain_lines(fake);
    REQUIRE(lines.size() == 1);
    auto ev = parse_event_line(lines[0]);
    auto* err = std::get_if<ErrorEvent>(&ev);
    REQUIRE(err);
    CHECK(err->message == "WHAT??");
}

TEST_CASE("FakePiconetDevice RESTART resets to firmware power-on defaults",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    send(fake, format_set_station(99));
    send(fake, format_set_mode(Mode::Listen));
    drain_lines(fake);

    send(fake, format_restart());
    CHECK(fake.station() == DEFAULT_STATION);
    CHECK(fake.mode() == Mode::Stop);
}

TEST_CASE("FakePiconetDevice close() makes is_open false; subsequent ops fail",
          "[piconet][fake]") {
    FakePiconetDevice fake;
    fake.close();
    CHECK_FALSE(fake.is_open());

    std::array<std::uint8_t, 4> buf{};
    auto rr = fake.read({buf.data(), buf.size()});
    CHECK(rr.error);

    const std::uint8_t b = 'x';
    auto wr = fake.write({&b, 1});
    CHECK(wr.error);
}
