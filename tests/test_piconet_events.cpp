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

#include "beebium/econet/piconet/Base64.hpp"
#include "beebium/econet/piconet/Events.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace beebium::piconet;

namespace {

// Convenience: extract a specific alternative from a ParsedEvent or
// REQUIRE-fail with a clear message.
template <typename T>
const T& as(const ParsedEvent& ev) {
    REQUIRE(std::holds_alternative<T>(ev));
    return std::get<T>(ev);
}

}  // namespace

TEST_CASE("Events: STATUS parses version, station, sr1, and mode", "[piconet][protocol][events]") {
    // Format: STATUS <maj.min.patch> <station_dec> <sr1_hex2> <mode_dec>
    // Source: piconet/board/src/piconet.c lines 189-196 (printf "STATUS %s %d %02x %d\n").
    auto ev = parse_event_line("STATUS 2.0.20 32 c3 1");
    const auto& s = as<StatusEvent>(ev);
    CHECK(s.version_major == 2);
    CHECK(s.version_minor == 0);
    CHECK(s.version_patch == 20);
    CHECK(s.station == 32);
    CHECK(s.status_register_1 == 0xC3);
    CHECK(s.mode == Mode::Listen);
}

TEST_CASE("Events: STATUS handles all three modes", "[piconet][protocol][events]") {
    CHECK(as<StatusEvent>(parse_event_line("STATUS 2.0.20 2 00 0")).mode == Mode::Stop);
    CHECK(as<StatusEvent>(parse_event_line("STATUS 2.0.20 2 00 1")).mode == Mode::Listen);
    CHECK(as<StatusEvent>(parse_event_line("STATUS 2.0.20 2 00 2")).mode == Mode::Monitor);
}

TEST_CASE("Events: TX_RESULT carries the parsed code", "[piconet][protocol][events]") {
    CHECK(as<TxResultEvent>(parse_event_line("TX_RESULT OK")).result == TxResult::Ok);
    CHECK(as<TxResultEvent>(parse_event_line("TX_RESULT NO_SCOUT_ACK")).result == TxResult::NoScoutAck);
    CHECK(as<TxResultEvent>(parse_event_line("TX_RESULT LINE_JAMMED")).result == TxResult::LineJammed);
    // Unknown code parses to TxResult::Unknown, but the event is still TxResultEvent.
    CHECK(as<TxResultEvent>(parse_event_line("TX_RESULT FUTURE_CODE")).result == TxResult::Unknown);
}

TEST_CASE("Events: REPLY_RESULT parses defensively (dead path)", "[piconet][protocol][events]") {
    // The firmware's REPLY pathway is dead but it still formats this line
    // (piconet.c lines 204-207). Parse it cleanly so we can log.
    auto ev = parse_event_line("REPLY_RESULT INVALID_RECEIVE_ID");
    CHECK(as<ReplyResultEvent>(ev).result == TxResult::InvalidReceiveId);
}

TEST_CASE("Events: RX_TRANSMIT decodes scout FIRST then data", "[piconet][protocol][events]") {
    // Source: piconet/board/src/piconet.c lines 246-256 prints scout then data.
    // This is the OPPOSITE order of the TX command's two base64 fields.
    const std::vector<std::uint8_t> scout{0x10, 0x20, 0x30};
    const std::vector<std::uint8_t> data{0xAA, 0xBB, 0xCC, 0xDD};
    const std::string line = "RX_TRANSMIT " + encode_base64(scout) + " " + encode_base64(data);
    auto ev = parse_event_line(line);
    const auto& rx = as<RxTransmitEvent>(ev);
    CHECK(rx.scout == scout);
    CHECK(rx.data  == data);
}

TEST_CASE("Events: RX_IMMEDIATE decodes scout then data", "[piconet][protocol][events]") {
    const std::vector<std::uint8_t> scout{0x82, 0x00, 0x01};  // Halt immediate
    const std::vector<std::uint8_t> data{0xFF};
    const std::string line = "RX_IMMEDIATE " + encode_base64(scout) + " " + encode_base64(data);
    auto ev = parse_event_line(line);
    const auto& im = as<RxImmediateEvent>(ev);
    CHECK(im.scout == scout);
    CHECK(im.data  == data);
}

TEST_CASE("Events: RX_BROADCAST is a single base64 field", "[piconet][protocol][events]") {
    const std::vector<std::uint8_t> data{0x01, 0x02, 0x03};
    auto ev = parse_event_line("RX_BROADCAST " + encode_base64(data));
    CHECK(as<RxBroadcastEvent>(ev).data == data);
}

TEST_CASE("Events: MONITOR is a single base64 field", "[piconet][protocol][events]") {
    const std::vector<std::uint8_t> data{0xCA, 0xFE, 0xBA, 0xBE};
    auto ev = parse_event_line("MONITOR " + encode_base64(data));
    CHECK(as<MonitorEvent>(ev).data == data);
}

TEST_CASE("Events: ERROR captures free-form remainder", "[piconet][protocol][events]") {
    // Firmware emits messages like "ERROR WHAT??" (piconet.c line 495) and
    // longer ASCII descriptions. We capture everything after "ERROR ".
    CHECK(as<ErrorEvent>(parse_event_line("ERROR WHAT??")).message == "WHAT??");
    CHECK(as<ErrorEvent>(parse_event_line("ERROR ECONET_RX_ERROR_CRC")).message == "ECONET_RX_ERROR_CRC");
    CHECK(as<ErrorEvent>(parse_event_line("ERROR multi word message")).message == "multi word message");
    // ERROR with no message yields empty message, not Unknown.
    CHECK(as<ErrorEvent>(parse_event_line("ERROR")).message.empty());
}

TEST_CASE("Events: unknown event tag yields UnknownEvent", "[piconet][protocol][events]") {
    auto ev = parse_event_line("FUTURE_EVENT some payload");
    const auto& u = as<UnknownEvent>(ev);
    CHECK(u.raw_line == "FUTURE_EVENT some payload");
}

TEST_CASE("Events: empty line yields UnknownEvent", "[piconet][protocol][events]") {
    auto ev = parse_event_line("");
    const auto& u = as<UnknownEvent>(ev);
    CHECK(u.raw_line.empty());
}

TEST_CASE("Events: malformed STATUS yields UnknownEvent", "[piconet][protocol][events]") {
    // Wrong arity (missing fields).
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS 2.0.20")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS 2.0.20 2 c3")));
    // Malformed version.
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS not.a.version 2 c3 1")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS 2.0 2 c3 1")));
    // Out-of-range station.
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS 2.0.20 999 c3 1")));
    // Out-of-range mode.
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS 2.0.20 2 c3 9")));
    // Bad SR1 hex.
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("STATUS 2.0.20 2 zz 1")));
}

TEST_CASE("Events: wrong arity for RX events yields UnknownEvent", "[piconet][protocol][events]") {
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_TRANSMIT")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_TRANSMIT abc")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_TRANSMIT abc def ghi")));

    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_IMMEDIATE abc")));

    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_BROADCAST")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_BROADCAST a b")));

    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("MONITOR")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("MONITOR a b")));
}

TEST_CASE("Events: bad base64 in RX field yields UnknownEvent", "[piconet][protocol][events]") {
    // Length not a multiple of 4 (Z is one char).
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_TRANSMIT Z aGVsbG8=")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("RX_BROADCAST $$$$")));
}

TEST_CASE("Events: TX_RESULT with no code yields UnknownEvent", "[piconet][protocol][events]") {
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("TX_RESULT")));
    CHECK(std::holds_alternative<UnknownEvent>(parse_event_line("TX_RESULT OK extra")));
}
