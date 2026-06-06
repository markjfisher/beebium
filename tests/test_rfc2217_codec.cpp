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

// Golden-vector tests for the RFC 2217 codec. Expected wire bytes are taken from
// RFC 2217 (cross-checked against pySerial's constants), not by round-tripping
// the codec against itself.

#include "Rfc2217Codec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

using namespace beebium::rfc2217;
using Bytes = std::vector<std::uint8_t>;

// =============================================================================
// Outbound encoding
// =============================================================================

TEST_CASE("RFC2217 escapes IAC in data", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Client);
    Bytes out;
    const Bytes data{0x41, 0xFF, 0x42};
    codec.encode_data(std::span<const std::uint8_t>(data.data(), data.size()), out);
    CHECK(out == Bytes{0x41, 0xFF, 0xFF, 0x42});  // 0xFF doubled
}

TEST_CASE("RFC2217 client encodes SET-BAUDRATE", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Client);
    Bytes out;
    codec.encode_set_baudrate(19200, out);
    // IAC SB COM-PORT SET-BAUDRATE <4-byte BE> IAC SE
    CHECK(out == Bytes{255, 250, 44, 1, 0x00, 0x00, 0x4B, 0x00, 255, 240});
}

TEST_CASE("RFC2217 server uses the +100 command form", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Server);
    Bytes out;
    codec.encode_notify_modemstate(comport::MODEMSTATE_CTS, out);
    // SERVER NOTIFY-MODEMSTATE = 7 + 100 = 107; CTS bit = 0x10
    CHECK(out == Bytes{255, 250, 44, 107, 0x10, 255, 240});
}

TEST_CASE("RFC2217 client encodes SET-CONTROL RTS", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Client);
    Bytes on;
    codec.encode_set_control(comport::CONTROL_RTS_ON, on);
    CHECK(on == Bytes{255, 250, 44, 5, 11, 255, 240});  // RTS_ON = 11
    Bytes off;
    codec.encode_set_control(comport::CONTROL_RTS_OFF, off);
    CHECK(off == Bytes{255, 250, 44, 5, 12, 255, 240});
}

TEST_CASE("RFC2217 escapes IAC inside a subnegotiation payload", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Client);
    Bytes out;
    // baud 0x000000FF -> the trailing 0xFF must be doubled inside the SB
    codec.encode_set_baudrate(0xFF, out);
    CHECK(out == Bytes{255, 250, 44, 1, 0x00, 0x00, 0x00, 0xFF, 0xFF, 255, 240});
}

// =============================================================================
// Inbound decoding
// =============================================================================

TEST_CASE("RFC2217 decodes data and unescapes IAC IAC", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Server);
    Bytes data, out;
    std::vector<ComPortCommand> cmds;
    const Bytes wire{0x41, 255, 255, 0x42};  // A, literal 0xFF, B
    codec.decode(std::span<const std::uint8_t>(wire.data(), wire.size()), data, cmds, out);
    CHECK(data == Bytes{0x41, 0xFF, 0x42});
    CHECK(cmds.empty());
}

TEST_CASE("RFC2217 server decodes a client SET-CONTROL subneg", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Server);
    Bytes data, out;
    std::vector<ComPortCommand> cmds;
    const Bytes wire{255, 250, 44, 5, 11, 255, 240};  // SET-CONTROL RTS_ON
    codec.decode(std::span<const std::uint8_t>(wire.data(), wire.size()), data, cmds, out);
    CHECK(data.empty());
    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].command == comport::SET_CONTROL);
    REQUIRE(cmds[0].value.size() == 1);
    CHECK(cmds[0].value[0] == comport::CONTROL_RTS_ON);
}

TEST_CASE("RFC2217 server answers a client WILL COM-PORT with DO", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Server);
    Bytes data, out;
    std::vector<ComPortCommand> cmds;
    const Bytes wire{255, 251, 44};  // IAC WILL COM-PORT
    codec.decode(std::span<const std::uint8_t>(wire.data(), wire.size()), data, cmds, out);
    CHECK(out == Bytes{255, 253, 44});  // IAC DO COM-PORT
    CHECK(codec.option_negotiated());
}

TEST_CASE("RFC2217 refuses an unsupported option", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Server);
    Bytes data, out;
    std::vector<ComPortCommand> cmds;
    const Bytes wire{255, 253, 24};  // IAC DO TERMINAL-TYPE (24) -> refuse
    codec.decode(std::span<const std::uint8_t>(wire.data(), wire.size()), data, cmds, out);
    CHECK(out == Bytes{255, 252, 24});  // IAC WONT TERMINAL-TYPE
}

TEST_CASE("RFC2217 carries IAC parser state across decode chunks", "[rfc2217][codec]") {
    Rfc2217Codec codec(Rfc2217Codec::Role::Server);
    Bytes data, out;
    std::vector<ComPortCommand> cmds;
    // A subnegotiation split mid-payload across two decode() calls.
    const Bytes first{255, 250, 44, 1, 0x00, 0x00};
    const Bytes second{0x4B, 0x00, 255, 240};
    codec.decode(std::span<const std::uint8_t>(first.data(), first.size()), data, cmds, out);
    CHECK(cmds.empty());  // not complete yet
    codec.decode(std::span<const std::uint8_t>(second.data(), second.size()), data, cmds, out);
    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].command == comport::SET_BAUDRATE);
    CHECK(cmds[0].value == Bytes{0x00, 0x00, 0x4B, 0x00});
}
