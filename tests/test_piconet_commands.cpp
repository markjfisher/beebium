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

#include "beebium/econet/piconet/Commands.hpp"

#include <cstdint>
#include <vector>

using namespace beebium::piconet;

TEST_CASE("Commands: STATUS and RESTART are simple terminators", "[piconet][protocol][commands]") {
    CHECK(format_status()  == "STATUS\r");
    CHECK(format_restart() == "RESTART\r");
}

TEST_CASE("Commands: SET_MODE emits the firmware's named mode token", "[piconet][protocol][commands]") {
    // The firmware parser at piconet/board/src/piconet.c lines 451-459 accepts
    // the literal strings STOP / LISTEN / MONITOR.
    CHECK(format_set_mode(Mode::Stop)    == "SET_MODE STOP\r");
    CHECK(format_set_mode(Mode::Listen)  == "SET_MODE LISTEN\r");
    CHECK(format_set_mode(Mode::Monitor) == "SET_MODE MONITOR\r");
}

TEST_CASE("Commands: SET_STATION emits decimal", "[piconet][protocol][commands]") {
    // Firmware parses base 10 (piconet.c line 466: strtol(..., NULL, 10)).
    CHECK(format_set_station(0)    == "SET_STATION 0\r");
    CHECK(format_set_station(2)    == "SET_STATION 2\r");
    CHECK(format_set_station(0x42) == "SET_STATION 66\r");
    CHECK(format_set_station(254)  == "SET_STATION 254\r");
    CHECK(format_set_station(255)  == "SET_STATION 255\r");
}

TEST_CASE("Commands: TX field order matches the firmware parser", "[piconet][protocol][commands]") {
    // Firmware reads (piconet.c lines 473-480):
    //   TX <dest_stn:dec> <dest_net:dec> <ctrl:dec> <port:dec> <data:b64> <scout_extra:b64>
    // Numbers are decimal, two base64 fields, data FIRST then scout_extra.
    const std::vector<std::uint8_t> data{0xAA, 0xBB, 0xCC};
    const std::string expected =
        "TX 50 0 128 153 " + encode_base64(data) + "\r";
    CHECK(format_tx(0x32, 0, 0x80, 0x99, data, {}) == expected);
}

TEST_CASE("Commands: TX with scout_extra appends second base64 field",
          "[piconet][protocol][commands]") {
    const std::vector<std::uint8_t> data{0x11, 0x22};
    const std::vector<std::uint8_t> scout_extra{0x33, 0x44, 0x55};
    const std::string expected =
        "TX 254 0 130 99 " + encode_base64(data) + " " + encode_base64(scout_extra) + "\r";
    CHECK(format_tx(254, 0, 130, 99, data, scout_extra) == expected);
}

TEST_CASE("Commands: TX with empty scout_extra omits the trailing field",
          "[piconet][protocol][commands]") {
    // The firmware treats a missing second token as scout_extra_len = 0
    // (piconet.c line 480; _decode_base64 returns 0 on NULL input).
    // The cleanest output is to omit the empty field rather than emit
    // a trailing space.
    const std::vector<std::uint8_t> data{0x01};
    const std::string out = format_tx(1, 0, 0, 0, data, {});
    CHECK(out == "TX 1 0 0 0 " + encode_base64(data) + "\r");
    // Sanity: no trailing space before the CR.
    CHECK(out[out.size() - 2] != ' ');
}

TEST_CASE("Commands: BCAST carries only a payload", "[piconet][protocol][commands]") {
    // Firmware at piconet.c lines 481-483: the source is stamped from
    // SET_STATION and the destination is hardcoded 0xFF; the command
    // provides only the data payload.
    const std::vector<std::uint8_t> data{0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(format_bcast(data) == "BCAST " + encode_base64(data) + "\r");
}
