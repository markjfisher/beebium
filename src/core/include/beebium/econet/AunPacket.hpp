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

#include "NetworkBackend.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace beebium {

// AUN wire format constants.
constexpr size_t AUN_HEADER_SIZE = 8;
constexpr uint16_t AUN_DEFAULT_PORT = 32768;

// Result of decoding an AUN wire-format packet.
struct AunDecodeResult {
    NetworkFrame frame;
    uint32_t handle = 0;
    bool valid = false;
};

namespace aun_packet {

// Encode a NetworkFrame into an AUN wire-format buffer.
//
// AUN header layout (8 bytes):
//   Byte 0:   Type (uint8_t, values 1-6)
//   Byte 1:   Port (uint8_t)
//   Byte 2:   CtrlByte (uint8_t, bit 7 cleared for AUN)
//   Byte 3:   Pad (always 0)
//   Byte 4-7: Handle (uint32_t, little-endian)
//   Byte 8+:  Payload
//
// The handle parameter is written into bytes 4-7. Addressing fields in the
// NetworkFrame are not encoded — AUN derives addressing from UDP source/dest.
inline std::vector<uint8_t> encode(const NetworkFrame& frame, uint32_t handle) {
    std::vector<uint8_t> buffer;
    buffer.reserve(AUN_HEADER_SIZE + frame.data.size());

    buffer.push_back(static_cast<uint8_t>(frame.type));
    buffer.push_back(frame.port);
    buffer.push_back(frame.control_byte & 0x7F);  // Bit 7 cleared for AUN
    buffer.push_back(0);  // Pad

    // Handle: little-endian uint32_t
    buffer.push_back(static_cast<uint8_t>(handle));
    buffer.push_back(static_cast<uint8_t>(handle >> 8));
    buffer.push_back(static_cast<uint8_t>(handle >> 16));
    buffer.push_back(static_cast<uint8_t>(handle >> 24));

    buffer.insert(buffer.end(), frame.data.begin(), frame.data.end());

    return buffer;
}

// Decode an AUN wire-format buffer into a NetworkFrame.
//
// The buffer must be at least AUN_HEADER_SIZE (8) bytes. Addressing fields
// (src/dest net/stn) are NOT populated — the caller fills those from the
// peer table using the UDP sender's IP and port.
inline AunDecodeResult decode(std::span<const uint8_t> buffer) {
    AunDecodeResult result;

    if (buffer.size() < AUN_HEADER_SIZE) {
        return result;  // valid = false
    }

    uint8_t type_byte = buffer[0];
    if (type_byte < 1 || type_byte > 6) {
        return result;  // valid = false: unknown type
    }

    result.frame.type = static_cast<FrameType>(type_byte);
    result.frame.port = buffer[1];
    result.frame.control_byte = buffer[2];
    // buffer[3] is pad, ignored

    // Handle: little-endian uint32_t
    result.handle = static_cast<uint32_t>(buffer[4])
                  | (static_cast<uint32_t>(buffer[5]) << 8)
                  | (static_cast<uint32_t>(buffer[6]) << 16)
                  | (static_cast<uint32_t>(buffer[7]) << 24);

    if (buffer.size() > AUN_HEADER_SIZE) {
        result.frame.data.assign(buffer.begin() + AUN_HEADER_SIZE, buffer.end());
    }

    result.valid = true;
    return result;
}

}  // namespace aun_packet
}  // namespace beebium
