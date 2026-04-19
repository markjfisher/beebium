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

#include "beebium/econet/piconet/Base64.hpp"

#include <array>

namespace beebium::piconet {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

// Reverse lookup table: char -> 6-bit value, or 0xFF for invalid.
constexpr std::array<std::uint8_t, 256> make_reverse_table() {
    std::array<std::uint8_t, 256> table{};
    for (auto& b : table) b = 0xFF;
    for (std::uint8_t i = 0; i < 64; ++i) {
        table[static_cast<std::uint8_t>(kAlphabet[i])] = i;
    }
    return table;
}

constexpr auto kReverse = make_reverse_table();

}  // namespace

std::string encode_base64(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return {};

    const std::size_t output_len = ((bytes.size() + 2) / 3) * 4;
    std::string out;
    out.reserve(output_len);

    std::size_t i = 0;
    while (i + 3 <= bytes.size()) {
        std::uint32_t triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                               (static_cast<std::uint32_t>(bytes[i + 1]) << 8) |
                                static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back(kAlphabet[triple & 0x3F]);
        i += 3;
    }

    const std::size_t remainder = bytes.size() - i;
    if (remainder == 1) {
        std::uint32_t triple = static_cast<std::uint32_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remainder == 2) {
        std::uint32_t triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                               (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

std::optional<std::vector<std::uint8_t>> decode_base64(std::string_view text) {
    if (text.empty()) return std::vector<std::uint8_t>{};

    if (text.size() % 4 != 0) return std::nullopt;

    // Count trailing '=' (0, 1, or 2). They must be only at the very end.
    std::size_t pad = 0;
    if (text[text.size() - 1] == '=') ++pad;
    if (text.size() >= 2 && text[text.size() - 2] == '=') ++pad;
    if (pad > 2) return std::nullopt;

    const std::size_t body_len = text.size() - pad;

    // Validate every body character is in the alphabet; '=' anywhere in the
    // body is a malformed input.
    for (std::size_t i = 0; i < body_len; ++i) {
        if (kReverse[static_cast<std::uint8_t>(text[i])] == 0xFF) {
            return std::nullopt;
        }
    }

    std::vector<std::uint8_t> out;
    out.reserve((text.size() / 4) * 3 - pad);

    std::size_t i = 0;
    while (i + 4 <= body_len) {
        std::uint32_t quad =
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i])]) << 18) |
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i + 1])]) << 12) |
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i + 2])]) << 6) |
             static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i + 3])]);
        out.push_back(static_cast<std::uint8_t>((quad >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((quad >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(quad & 0xFF));
        i += 4;
    }

    // Handle the final padded group, if any.
    if (pad == 1) {
        // 3 body chars + 1 '=' -> decodes to 2 bytes.
        std::uint32_t quad =
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i])]) << 18) |
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i + 1])]) << 12) |
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i + 2])]) << 6);
        out.push_back(static_cast<std::uint8_t>((quad >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((quad >> 8) & 0xFF));
    } else if (pad == 2) {
        // 2 body chars + 2 '=' -> decodes to 1 byte.
        std::uint32_t quad =
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i])]) << 18) |
            (static_cast<std::uint32_t>(kReverse[static_cast<std::uint8_t>(text[i + 1])]) << 12);
        out.push_back(static_cast<std::uint8_t>((quad >> 16) & 0xFF));
    }

    return out;
}

}  // namespace beebium::piconet
