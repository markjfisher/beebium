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

#include <beebium/econet/AunPacket.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

// =============================================================================
// Encode — Frame Types
// =============================================================================

TEST_CASE("AunPacket encode: Unicast", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.port = 0x99;
    frame.control_byte = 0x00;
    frame.data = {0xAA, 0xBB};

    auto buf = aun_packet::encode(frame, 4);

    REQUIRE(buf.size() == AUN_HEADER_SIZE + 2);
    CHECK(buf[0] == 2);     // Unicast type
    CHECK(buf[1] == 0x99);  // Port
    CHECK(buf[2] == 0x00);  // CtrlByte
    CHECK(buf[3] == 0x00);  // Pad
    // Handle = 4, little-endian
    CHECK(buf[4] == 0x04);
    CHECK(buf[5] == 0x00);
    CHECK(buf[6] == 0x00);
    CHECK(buf[7] == 0x00);
    // Payload
    CHECK(buf[8] == 0xAA);
    CHECK(buf[9] == 0xBB);
}

TEST_CASE("AunPacket encode: Broadcast", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Broadcast;
    frame.port = 0x99;
    frame.control_byte = 0x00;
    frame.data = {0x42};

    auto buf = aun_packet::encode(frame, 8);

    CHECK(buf[0] == 1);  // Broadcast type
    CHECK(buf.size() == AUN_HEADER_SIZE + 1);
    CHECK(buf[8] == 0x42);
}

TEST_CASE("AunPacket encode: Ack with empty payload", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Ack;
    frame.port = 0x99;
    frame.control_byte = 0x00;

    auto buf = aun_packet::encode(frame, 12);

    CHECK(buf.size() == AUN_HEADER_SIZE);
    CHECK(buf[0] == 3);  // Ack type
}

TEST_CASE("AunPacket encode: Nack", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Nack;

    auto buf = aun_packet::encode(frame, 0);

    CHECK(buf[0] == 4);  // Nack type
    CHECK(buf.size() == AUN_HEADER_SIZE);
}

TEST_CASE("AunPacket encode: Immediate", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Immediate;
    frame.port = 0x00;
    frame.control_byte = 0x08;
    frame.data = {0xDD};

    auto buf = aun_packet::encode(frame, 16);

    CHECK(buf[0] == 5);     // Immediate type
    CHECK(buf[1] == 0x00);  // Port
    CHECK(buf[2] == 0x08);  // CtrlByte
    CHECK(buf.size() == AUN_HEADER_SIZE + 1);
    CHECK(buf[8] == 0xDD);
}

TEST_CASE("AunPacket encode: ImmReply", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::ImmReply;
    frame.data = {0xEE, 0xFF};

    auto buf = aun_packet::encode(frame, 20);

    CHECK(buf[0] == 6);  // ImmReply type
    CHECK(buf.size() == AUN_HEADER_SIZE + 2);
}

// =============================================================================
// Encode — Control Byte Bit 7 Clearing
// =============================================================================

TEST_CASE("AunPacket encode: control byte bit 7 is cleared", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.control_byte = 0x85;  // Bit 7 set

    auto buf = aun_packet::encode(frame, 0);

    CHECK(buf[2] == 0x05);  // Bit 7 cleared: 0x85 & 0x7F = 0x05
}

TEST_CASE("AunPacket encode: control byte already without bit 7 is unchanged", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.control_byte = 0x02;

    auto buf = aun_packet::encode(frame, 0);

    CHECK(buf[2] == 0x02);
}

// =============================================================================
// Encode — Handle Endianness
// =============================================================================

TEST_CASE("AunPacket encode: handle zero", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Ack;

    auto buf = aun_packet::encode(frame, 0);

    CHECK(buf[4] == 0x00);
    CHECK(buf[5] == 0x00);
    CHECK(buf[6] == 0x00);
    CHECK(buf[7] == 0x00);
}

TEST_CASE("AunPacket encode: handle 0xFFFFFFFF little-endian", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Ack;

    auto buf = aun_packet::encode(frame, 0xFFFFFFFF);

    CHECK(buf[4] == 0xFF);
    CHECK(buf[5] == 0xFF);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("AunPacket encode: handle 0x04030201 little-endian", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Ack;

    auto buf = aun_packet::encode(frame, 0x04030201);

    CHECK(buf[4] == 0x01);  // LSB first
    CHECK(buf[5] == 0x02);
    CHECK(buf[6] == 0x03);
    CHECK(buf[7] == 0x04);  // MSB last
}

// =============================================================================
// Encode — Empty and Large Payloads
// =============================================================================

TEST_CASE("AunPacket encode: empty payload produces header only", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Unicast;

    auto buf = aun_packet::encode(frame, 0);

    CHECK(buf.size() == AUN_HEADER_SIZE);
}

TEST_CASE("AunPacket encode: large payload (1024 bytes)", "[econet][aun][packet]") {
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.data.resize(1024, 0x5A);

    auto buf = aun_packet::encode(frame, 0);

    CHECK(buf.size() == AUN_HEADER_SIZE + 1024);
    CHECK(buf[8] == 0x5A);
    CHECK(buf[AUN_HEADER_SIZE + 1023] == 0x5A);
}

// =============================================================================
// Decode — Valid Packets
// =============================================================================

TEST_CASE("AunPacket decode: Unicast with payload", "[econet][aun][packet]") {
    std::vector<uint8_t> buf = {
        2,     // Type: Unicast
        0x99,  // Port
        0x00,  // CtrlByte
        0x00,  // Pad
        0x04, 0x00, 0x00, 0x00,  // Handle = 4
        0xAA, 0xBB  // Payload
    };

    auto result = aun_packet::decode(buf);

    CHECK(result.valid);
    CHECK(result.frame.type == FrameType::Unicast);
    CHECK(result.frame.port == 0x99);
    CHECK(result.frame.control_byte == 0x00);
    CHECK(result.handle == 4);
    REQUIRE(result.frame.data.size() == 2);
    CHECK(result.frame.data[0] == 0xAA);
    CHECK(result.frame.data[1] == 0xBB);
}

TEST_CASE("AunPacket decode: Ack with no payload", "[econet][aun][packet]") {
    std::vector<uint8_t> buf = {
        3,     // Type: Ack
        0x99,  // Port
        0x00,  // CtrlByte
        0x00,  // Pad
        0x08, 0x00, 0x00, 0x00  // Handle = 8
    };

    auto result = aun_packet::decode(buf);

    CHECK(result.valid);
    CHECK(result.frame.type == FrameType::Ack);
    CHECK(result.handle == 8);
    CHECK(result.frame.data.empty());
}

TEST_CASE("AunPacket decode: handle extraction little-endian", "[econet][aun][packet]") {
    std::vector<uint8_t> buf = {
        2, 0, 0, 0,
        0x78, 0x56, 0x34, 0x12  // Handle = 0x12345678
    };

    auto result = aun_packet::decode(buf);

    CHECK(result.valid);
    CHECK(result.handle == 0x12345678);
}

TEST_CASE("AunPacket decode: all valid type values", "[econet][aun][packet]") {
    for (uint8_t type = 1; type <= 6; ++type) {
        std::vector<uint8_t> buf = {type, 0, 0, 0, 0, 0, 0, 0};
        auto result = aun_packet::decode(buf);
        CHECK(result.valid);
        CHECK(result.frame.type == static_cast<FrameType>(type));
    }
}

// =============================================================================
// Decode — Invalid Packets
// =============================================================================

TEST_CASE("AunPacket decode: buffer too short (empty)", "[econet][aun][packet]") {
    std::vector<uint8_t> buf;
    auto result = aun_packet::decode(buf);
    CHECK_FALSE(result.valid);
}

TEST_CASE("AunPacket decode: buffer too short (7 bytes)", "[econet][aun][packet]") {
    std::vector<uint8_t> buf = {2, 0, 0, 0, 0, 0, 0};
    auto result = aun_packet::decode(buf);
    CHECK_FALSE(result.valid);
}

TEST_CASE("AunPacket decode: type 0 (RawFrame) is invalid for AUN", "[econet][aun][packet]") {
    std::vector<uint8_t> buf = {0, 0, 0, 0, 0, 0, 0, 0};
    auto result = aun_packet::decode(buf);
    CHECK_FALSE(result.valid);
}

TEST_CASE("AunPacket decode: type 7 is invalid", "[econet][aun][packet]") {
    std::vector<uint8_t> buf = {7, 0, 0, 0, 0, 0, 0, 0};
    auto result = aun_packet::decode(buf);
    CHECK_FALSE(result.valid);
}

TEST_CASE("AunPacket decode: type 255 is invalid", "[econet][aun][packet]") {
    std::vector<uint8_t> buf = {255, 0, 0, 0, 0, 0, 0, 0};
    auto result = aun_packet::decode(buf);
    CHECK_FALSE(result.valid);
}

// =============================================================================
// Round-Trip
// =============================================================================

TEST_CASE("AunPacket round-trip: encode then decode preserves fields", "[econet][aun][packet]") {
    NetworkFrame original;
    original.type = FrameType::Unicast;
    original.port = 0x99;
    original.control_byte = 0x02;
    original.data = {0x01, 0x02, 0x03, 0x04, 0x05};

    uint32_t handle = 100;
    auto encoded = aun_packet::encode(original, handle);
    auto result = aun_packet::decode(encoded);

    CHECK(result.valid);
    CHECK(result.frame.type == original.type);
    CHECK(result.frame.port == original.port);
    CHECK(result.frame.control_byte == original.control_byte);
    CHECK(result.handle == handle);
    CHECK(result.frame.data == original.data);
}

TEST_CASE("AunPacket round-trip: Broadcast with payload", "[econet][aun][packet]") {
    NetworkFrame original;
    original.type = FrameType::Broadcast;
    original.port = 0x99;
    original.control_byte = 0x00;
    original.data = {0x42, 0x43};

    auto encoded = aun_packet::encode(original, 44);
    auto result = aun_packet::decode(encoded);

    CHECK(result.valid);
    CHECK(result.frame.type == FrameType::Broadcast);
    CHECK(result.frame.port == 0x99);
    CHECK(result.handle == 44);
    CHECK(result.frame.data == original.data);
}

TEST_CASE("AunPacket round-trip: Immediate with empty payload", "[econet][aun][packet]") {
    NetworkFrame original;
    original.type = FrameType::Immediate;
    original.port = 0x00;
    original.control_byte = 0x08;

    auto encoded = aun_packet::encode(original, 0);
    auto result = aun_packet::decode(encoded);

    CHECK(result.valid);
    CHECK(result.frame.type == FrameType::Immediate);
    CHECK(result.frame.port == 0x00);
    CHECK(result.frame.control_byte == 0x08);
    CHECK(result.frame.data.empty());
}

TEST_CASE("AunPacket round-trip: large payload (1024 bytes)", "[econet][aun][packet]") {
    NetworkFrame original;
    original.type = FrameType::Unicast;
    original.data.resize(1024);
    for (size_t i = 0; i < 1024; ++i) {
        original.data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto encoded = aun_packet::encode(original, 0xDEADBEEF);
    auto result = aun_packet::decode(encoded);

    CHECK(result.valid);
    CHECK(result.handle == 0xDEADBEEF);
    CHECK(result.frame.data == original.data);
}

TEST_CASE("AunPacket round-trip: control byte bit 7 is cleared by encode", "[econet][aun][packet]") {
    NetworkFrame original;
    original.type = FrameType::Unicast;
    original.control_byte = 0x85;  // Bit 7 set

    auto encoded = aun_packet::encode(original, 0);
    auto result = aun_packet::decode(encoded);

    CHECK(result.valid);
    // Decode sees the cleared value, not the original
    CHECK(result.frame.control_byte == 0x05);
}
