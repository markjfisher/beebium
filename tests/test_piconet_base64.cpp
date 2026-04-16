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
#include "beebium/econet/piconet/Constants.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

using namespace beebium::piconet;

namespace {

// RFC 4648 section 10 canonical test vectors.
struct Rfc4648Case {
    std::string_view plain;
    std::string_view encoded;
};
constexpr std::array<Rfc4648Case, 7> kRfc4648Cases = {{
    {"",       ""},
    {"f",      "Zg=="},
    {"fo",     "Zm8="},
    {"foo",    "Zm9v"},
    {"foob",   "Zm9vYg=="},
    {"fooba",  "Zm9vYmE="},
    {"foobar", "Zm9vYmFy"},
}};

std::vector<std::uint8_t> bytes_from(std::string_view s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

}  // namespace

TEST_CASE("Base64 encode matches RFC 4648 canonical test vectors", "[piconet][protocol][base64]") {
    for (const auto& c : kRfc4648Cases) {
        CHECK(encode_base64(bytes_from(c.plain)) == std::string(c.encoded));
    }
}

TEST_CASE("Base64 decode matches RFC 4648 canonical test vectors", "[piconet][protocol][base64]") {
    for (const auto& c : kRfc4648Cases) {
        auto decoded = decode_base64(c.encoded);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == bytes_from(c.plain));
    }
}

TEST_CASE("Base64 encodes the firmware's canned MachinePeek response correctly",
          "[piconet][protocol][base64]") {
    // The firmware replies to inbound MachinePeek (control byte 0x88) with
    // these 4 bytes appended to the wire-level ACK -- we never see this on
    // the serial protocol, but the bytes may appear in tests of the fake.
    // Source: piconet/board/src/econet.c lines 605-616.
    const std::span<const std::uint8_t> peek{MACHINE_PEEK_RESPONSE.data(),
                                             MACHINE_PEEK_RESPONSE.size()};
    CHECK(encode_base64(peek) == "VUoABQ==");

    // Round-trip.
    auto decoded = decode_base64("VUoABQ==");
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == MACHINE_PEEK_RESPONSE.size());
    for (size_t i = 0; i < MACHINE_PEEK_RESPONSE.size(); ++i) {
        CHECK((*decoded)[i] == MACHINE_PEEK_RESPONSE[i]);
    }
}

TEST_CASE("Base64 round-trips arbitrary byte sequences", "[piconet][protocol][base64]") {
    std::mt19937 rng(0xBEEF);  // Fixed seed for determinism.
    std::uniform_int_distribution<int> byte_dist(0, 255);

    for (size_t len : {0u, 1u, 2u, 3u, 4u, 7u, 16u, 100u, 256u, 1024u}) {
        std::vector<std::uint8_t> plain(len);
        for (auto& b : plain) {
            b = static_cast<std::uint8_t>(byte_dist(rng));
        }

        auto encoded = encode_base64(plain);

        // Encoded length is always a multiple of 4 (when non-empty).
        if (!plain.empty()) {
            CHECK(encoded.size() % 4 == 0);
        }

        auto decoded = decode_base64(encoded);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == plain);
    }
}

TEST_CASE("Base64 decode rejects malformed input", "[piconet][protocol][base64]") {
    // Length not a multiple of 4 -> invalid.
    CHECK_FALSE(decode_base64("Zg=").has_value());
    CHECK_FALSE(decode_base64("Z").has_value());
    CHECK_FALSE(decode_base64("ZmZmZmZ").has_value());

    // Invalid characters (the firmware never emits whitespace; we should not
    // accept it either -- pass clean tokens).
    CHECK_FALSE(decode_base64("Zm 9v").has_value());
    CHECK_FALSE(decode_base64("Zm9v\n").has_value());
    CHECK_FALSE(decode_base64("Zm9v\r").has_value());
    CHECK_FALSE(decode_base64("$$$$").has_value());

    // Padding in the middle of input -> invalid.
    CHECK_FALSE(decode_base64("Z=Zm").has_value());
}

TEST_CASE("Base64 empty input is well-defined", "[piconet][protocol][base64]") {
    CHECK(encode_base64(std::span<const std::uint8_t>{}).empty());
    auto decoded = decode_base64("");
    REQUIRE(decoded.has_value());
    CHECK(decoded->empty());
}
