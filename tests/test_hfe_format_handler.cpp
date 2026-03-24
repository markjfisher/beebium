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

#include <beebium/disc/formats/HfeFormatHandler.hpp>
#include <beebium/disc/formats/SsdFormatHandler.hpp>
#include <beebium/disc/DiscFormatRegistry.hpp>
#include <beebium/disc/TrackDecoder.hpp>
#include <beebium/disc/TrackBuilder.hpp>
#include <beebium/disc/IbmDiscFormat.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// Helper: reverse bits in a byte (matches HFE's storage convention).
static constexpr uint8_t byte_flip(uint8_t b) {
    b = static_cast<uint8_t>(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = static_cast<uint8_t>(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = static_cast<uint8_t>(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

// Write a 16-bit LE value.
static void write_le16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

// Helper: create a minimal valid HFE v1 file with 1 track, 1 side.
// The track data contains a simple bit pattern that can be verified.
static std::vector<uint8_t> make_minimal_hfe_v1(uint8_t num_tracks = 1,
                                                  uint8_t num_sides = 1) {
    // Layout: block 0 = header, block 1 = LUT, blocks 2+ = track data
    size_t bytes_per_side = 3125 * 4;  // 3125 pulse words * 4 bytes each
    // Each side's data padded to 256-byte chunks for interleaving
    size_t side_chunks = (bytes_per_side + 255) / 256;
    // Track data = side_chunks * 512 bytes (interleaved)
    size_t track_byte_length = side_chunks * 512;
    size_t blocks_per_track = (track_byte_length + 511) / 512;

    size_t total_size = (2 + static_cast<size_t>(num_tracks) * blocks_per_track) * 512;
    std::vector<uint8_t> file(total_size, 0);

    // Header (block 0)
    std::memcpy(file.data(), "HXCPICFE", 8);
    file[8] = 0;          // revision 0
    file[9] = num_tracks;
    file[10] = num_sides;
    file[11] = 2;         // encoding: IBM MFM
    write_le16(file.data() + 12, 250);  // bitrate
    write_le16(file.data() + 16, 7);    // interface mode: Shugart DD
    write_le16(file.data() + 18, 1);    // LUT at block 1

    // LUT (block 1): one 4-byte entry per track
    uint8_t* lut = file.data() + 512;
    for (uint8_t t = 0; t < num_tracks; ++t) {
        uint16_t block_offset = static_cast<uint16_t>(2 + t * blocks_per_track);
        write_le16(lut + t * 4, block_offset);
        write_le16(lut + t * 4 + 2, static_cast<uint16_t>(track_byte_length));
    }

    // Track data: fill side 0 with a known pattern (alternating 0xAA/0x55 bit-flipped)
    for (uint8_t t = 0; t < num_tracks; ++t) {
        size_t track_offset = (2 + static_cast<size_t>(t) * blocks_per_track) * 512;
        for (size_t i = 0; i < bytes_per_side && i / 256 * 512 + i % 256 < track_byte_length; ++i) {
            size_t block = i / 256;
            size_t offset = i % 256;
            size_t file_pos = track_offset + block * 512 + offset;  // side 0 = first 256 of each block
            file[file_pos] = byte_flip(static_cast<uint8_t>((i & 1) ? 0x55 : 0xAA));
        }
        if (num_sides == 2) {
            for (size_t i = 0; i < bytes_per_side && i / 256 * 512 + 256 + i % 256 < track_byte_length; ++i) {
                size_t block = i / 256;
                size_t offset = i % 256;
                size_t file_pos = track_offset + block * 512 + 256 + offset;  // side 1
                file[file_pos] = byte_flip(static_cast<uint8_t>((i & 1) ? 0x33 : 0xCC));
            }
        }
    }

    return file;
}

// Helper: create an HFE v3 file with RAND opcodes for weak bits on track 0.
static std::vector<uint8_t> make_hfe_v3_with_rand() {
    auto file = make_minimal_hfe_v1(1, 1);
    // Change signature to v3
    std::memcpy(file.data(), "HXCHFEV3", 8);

    // Replace some track data with RAND opcodes (byte_flip(0xF4))
    // The first few bytes of side 0, track 0
    size_t track_offset = 2 * 512;  // Block 2
    uint8_t rand_flipped = byte_flip(0xF4);
    for (size_t i = 0; i < 8; ++i) {
        file[track_offset + i] = rand_flipped;
    }

    return file;
}

// =============================================================================
// Detection Tests
// =============================================================================

TEST_CASE("HfeFormatHandler detects HFE v1 by magic bytes", "[disc][hfe][detect]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1();
    auto result = handler.detect(file, ".hfe");

    CHECK(result.detected);
    CHECK(result.confidence == 100);
    CHECK(result.format_name == "HFE v1");
}

TEST_CASE("HfeFormatHandler detects HFE v3 by magic bytes", "[disc][hfe][detect]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1();
    std::memcpy(file.data(), "HXCHFEV3", 8);
    auto result = handler.detect(file, ".hfe");

    CHECK(result.detected);
    CHECK(result.confidence == 100);
    CHECK(result.format_name == "HFE v3");
}

TEST_CASE("HfeFormatHandler detects HFE by magic even with wrong extension", "[disc][hfe][detect]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1();
    auto result = handler.detect(file, ".dsd");

    CHECK(result.detected);
    CHECK(result.confidence == 100);
}

TEST_CASE("HfeFormatHandler rejects non-HFE file", "[disc][hfe][detect]") {
    HfeFormatHandler handler;
    std::vector<uint8_t> data(1024, 0);
    auto result = handler.detect(data, ".hfe");

    // Low confidence (extension only)
    CHECK(result.confidence < 50);
}

TEST_CASE("HfeFormatHandler rejects too-small file", "[disc][hfe][detect]") {
    HfeFormatHandler handler;
    std::vector<uint8_t> data(100, 0);
    auto result = handler.detect(data, ".hfe");

    CHECK_FALSE(result.detected);
}

// =============================================================================
// Loading Tests
// =============================================================================

TEST_CASE("HfeFormatHandler loads HFE v1 file", "[disc][hfe][load]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1();
    auto result = handler.load(file, "/tmp/test.hfe");

    REQUIRE(result.success());
    CHECK(result.disc->name() == "test.hfe");
    CHECK(result.disc->format_name() == "HFE v1");
    CHECK_FALSE(result.disc->is_double_sided());
}

TEST_CASE("HfeFormatHandler loads HFE v1 double-sided", "[disc][hfe][load]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1(2, 2);
    auto result = handler.load(file, "/tmp/test.hfe");

    REQUIRE(result.success());
    CHECK(result.disc->is_double_sided());
}

TEST_CASE("HFE v1 track has non-zero pulse data", "[disc][hfe][load]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1();
    auto result = handler.load(file, "/tmp/test.hfe");
    REQUIRE(result.success());

    // Check that the track has some non-zero pulse data
    const auto& track = result.disc->track(false, 0);
    bool has_nonzero = false;
    for (uint32_t i = 0; i < track.length() && i < 100; ++i) {
        if (track.read_pulses(i) != 0) {
            has_nonzero = true;
            break;
        }
    }
    CHECK(has_nonzero);
}

TEST_CASE("HFE v3 with RAND opcodes loads with zero pulses for weak areas", "[disc][hfe][v3]") {
    HfeFormatHandler handler;
    auto file = make_hfe_v3_with_rand();
    auto result = handler.load(file, "/tmp/test_v3.hfe");
    REQUIRE(result.success());

    // The first portion of the track should have zeros from RAND opcodes
    const auto& track = result.disc->track(false, 0);
    // RAND opcodes produce 8 zero bits each; 8 RAND opcodes = 64 zero bits = 2 pulse words
    CHECK(track.read_pulses(0) == 0);
    CHECK(track.read_pulses(1) == 0);
}

// =============================================================================
// Format Metadata
// =============================================================================

TEST_CASE("HFE loader stores format metadata (LUT + version)", "[disc][hfe][metadata]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1();
    auto result = handler.load(file, "/tmp/test.hfe");
    REQUIRE(result.success());

    auto metadata = result.disc->format_metadata();
    CHECK(metadata.size() == 513);
    CHECK(metadata[512] == 1);  // Version 1
}

TEST_CASE("HFE v3 metadata has version 3", "[disc][hfe][metadata]") {
    HfeFormatHandler handler;
    auto file = make_minimal_hfe_v1();
    std::memcpy(file.data(), "HXCHFEV3", 8);
    auto result = handler.load(file, "/tmp/test.hfe");
    REQUIRE(result.success());

    CHECK(result.disc->format_metadata()[512] == 3);
}

// =============================================================================
// Registry Integration
// =============================================================================

TEST_CASE("Registry auto-detects HFE alongside SSD", "[disc][hfe][registry]") {
    DiscFormatRegistry registry;
    registry.register_handler(std::make_unique<SsdFormatHandler>());
    registry.register_handler(std::make_unique<HfeFormatHandler>());

    auto file = make_minimal_hfe_v1();

    // HFE magic bytes should win over SSD even with .hfe extension
    auto result = registry.load_from_data(file, ".hfe", "/tmp/test.hfe");
    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "HFE v1");
}

TEST_CASE("HFE beats SSD confidence for a valid HFE file", "[disc][hfe][registry]") {
    DiscFormatRegistry registry;
    registry.register_handler(std::make_unique<SsdFormatHandler>());
    registry.register_handler(std::make_unique<HfeFormatHandler>());

    auto file = make_minimal_hfe_v1();
    // Even with .ssd extension, HFE magic should win (confidence 100 vs 0)
    auto result = registry.load_from_data(file, ".ssd", "/tmp/test.ssd");
    // SSD handler shouldn't match because file size is wrong
    // HFE handler should still detect by magic
    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "HFE v1");
}
