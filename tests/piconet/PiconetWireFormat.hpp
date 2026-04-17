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

#pragma once

// Pure functions shared between the Piconet test doubles
// (FakePiconetDevice, AunBridgePiconetDevice). Used to construct the
// wire-format byte sequences that get base64-encoded into RX_TRANSMIT
// and RX_BROADCAST events.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace beebium::piconet::test {

// 2-character lowercase hex (matches the firmware's "%02x" format for
// SR1 in STATUS responses).
inline std::string hex2(std::uint8_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    char buf[3] = { digits[(value >> 4) & 0xF], digits[value & 0xF], 0 };
    return std::string(buf, 2);
}

// Build a wire-format scout frame: [dst, dst_net, src, src_net, ctrl|0x80, port, scout_extra...].
// The wire scout high bit (0x80) is OR'd onto the ctrl byte to match the
// firmware's on-wire representation; consumers (e.g. PiconetBackend's reader)
// strip it off again when constructing NetworkFrames.
inline std::vector<std::uint8_t> build_scout_wire(
    std::uint8_t dest_stn, std::uint8_t dest_net,
    std::uint8_t src_stn,  std::uint8_t src_net,
    std::uint8_t ctrl,     std::uint8_t port,
    const std::uint8_t* extra, std::size_t extra_len) {
    std::vector<std::uint8_t> out;
    out.reserve(6 + extra_len);
    out.push_back(dest_stn);
    out.push_back(dest_net);
    out.push_back(src_stn);
    out.push_back(src_net);
    out.push_back(static_cast<std::uint8_t>(ctrl | 0x80));
    out.push_back(port);
    out.insert(out.end(), extra, extra + extra_len);
    return out;
}

// Build a wire-format data frame: [dst, dst_net, src, src_net, payload...].
inline std::vector<std::uint8_t> build_data_wire(
    std::uint8_t dest_stn, std::uint8_t dest_net,
    std::uint8_t src_stn,  std::uint8_t src_net,
    const std::uint8_t* payload, std::size_t payload_len) {
    std::vector<std::uint8_t> out;
    out.reserve(4 + payload_len);
    out.push_back(dest_stn);
    out.push_back(dest_net);
    out.push_back(src_stn);
    out.push_back(src_net);
    out.insert(out.end(), payload, payload + payload_len);
    return out;
}

}  // namespace beebium::piconet::test
