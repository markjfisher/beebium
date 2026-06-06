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

// Golden-vector tests for the IP232 escape codec. The expected byte sequences
// are taken from the authoritative implementation (BeebEm Src/IP232.cpp +
// Src/Serial.cpp), NOT derived by round-tripping the codec against itself: a
// symmetric encode/decode bug would survive a round-trip but is still wire-wrong.

#include "Ip232Codec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

using namespace beebium::ip232;
using Bytes = std::vector<std::uint8_t>;

namespace {

Bytes encode_data(const Ip232Codec& codec, const Bytes& in) {
    Bytes out;
    for (std::uint8_t b : in) codec.encode_data(b, out);
    return out;
}

}  // namespace

// =============================================================================
// Outbound encoding (Beeb -> wire)
// =============================================================================

TEST_CASE("IP232 encodes ordinary data bytes verbatim", "[ip232][codec]") {
    Ip232Codec codec(/*raw=*/false);
    CHECK(encode_data(codec, {0x41, 0x00, 0x7F, 0xAB}) == Bytes{0x41, 0x00, 0x7F, 0xAB});
}

TEST_CASE("IP232 doubles a 0xFF data byte in ip232 mode", "[ip232][codec]") {
    Ip232Codec codec(/*raw=*/false);
    CHECK(encode_data(codec, {0xFF}) == Bytes{0xFF, 0xFF});
    CHECK(encode_data(codec, {0x41, 0xFF, 0x42}) == Bytes{0x41, 0xFF, 0xFF, 0x42});
}

TEST_CASE("IP232 raw mode never escapes 0xFF", "[ip232][codec]") {
    Ip232Codec codec(/*raw=*/true);
    CHECK(encode_data(codec, {0xFF, 0xFF}) == Bytes{0xFF, 0xFF});
}

TEST_CASE("IP232 encodes an RTS change as the 0xFF escape", "[ip232][codec]") {
    Bytes asserted;
    Ip232Codec::encode_rts(true, asserted);
    CHECK(asserted == Bytes{0xFF, 0x01});

    Bytes deasserted;
    Ip232Codec::encode_rts(false, deasserted);
    CHECK(deasserted == Bytes{0xFF, 0x00});
}

// =============================================================================
// Inbound decoding (wire -> Beeb)
// =============================================================================

TEST_CASE("IP232 decodes data, the doubled flag, and DTR events", "[ip232][codec]") {
    Ip232Codec codec(/*raw=*/false);
    Bytes data;
    std::vector<DtrEvent> events;
    // 0x41 | 0xFF 0xFF (-> literal 0xFF) | 0xFF 0x01 (-> DTR high) | 0x42
    const Bytes wire{0x41, 0xFF, 0xFF, 0xFF, 0x01, 0x42};
    codec.decode(std::span<const std::uint8_t>(wire.data(), wire.size()), data, events);

    CHECK(data == Bytes{0x41, 0xFF, 0x42});
    REQUIRE(events.size() == 1);
    CHECK(events[0] == DtrEvent::High);
}

TEST_CASE("IP232 decodes a DTR-low escape", "[ip232][codec]") {
    Ip232Codec codec(/*raw=*/false);
    Bytes data;
    std::vector<DtrEvent> events;
    const Bytes wire{0xFF, 0x00};
    codec.decode(std::span<const std::uint8_t>(wire.data(), wire.size()), data, events);

    CHECK(data.empty());
    REQUIRE(events.size() == 1);
    CHECK(events[0] == DtrEvent::Low);
}

TEST_CASE("IP232 raw mode passes all inbound bytes through", "[ip232][codec]") {
    Ip232Codec codec(/*raw=*/true);
    Bytes data;
    std::vector<DtrEvent> events;
    const Bytes wire{0xFF, 0x01, 0x00, 0xFF};
    codec.decode(std::span<const std::uint8_t>(wire.data(), wire.size()), data, events);

    CHECK(data == Bytes{0xFF, 0x01, 0x00, 0xFF});
    CHECK(events.empty());
}

TEST_CASE("IP232 carries the flag state across decode chunks", "[ip232][codec]") {
    Ip232Codec codec(/*raw=*/false);
    Bytes data;
    std::vector<DtrEvent> events;

    // A 0xFF arrives at the end of one chunk; its partner opens the next.
    const Bytes first{0x41, 0xFF};
    const Bytes second{0xFF, 0x42};
    codec.decode(std::span<const std::uint8_t>(first.data(), first.size()), data, events);
    CHECK(data == Bytes{0x41});  // flag pending, no literal yet

    codec.decode(std::span<const std::uint8_t>(second.data(), second.size()), data, events);
    CHECK(data == Bytes{0x41, 0xFF, 0x42});
    CHECK(events.empty());
}
