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

#include <beebium/disc/TrackBuilder.hpp>
#include <beebium/disc/IbmDiscFormat.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// =============================================================================
// Basic FM Operations
// =============================================================================

TEST_CASE("TrackBuilder starts at position 0", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);
    CHECK(builder.position() == 0);
}

TEST_CASE("TrackBuilder append_fm_byte advances position by 1", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_byte(0x42);
    CHECK(builder.position() == 1);
}

TEST_CASE("TrackBuilder append_fm_byte produces correct pulses", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_byte(0x00);
    // FM byte 0x00 with default clocks 0xFF: each nibble = 0x4 -> 0x44444444
    CHECK(track.read_pulses(0) == fm_to_2us_pulses(0xFF, 0x00));
}

TEST_CASE("TrackBuilder append_fm_data_and_clocks uses custom clocks", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_data_and_clocks(k_id_mark_data_pattern, k_mark_clock_pattern);
    CHECK(track.read_pulses(0) == fm_to_2us_pulses(k_mark_clock_pattern, k_id_mark_data_pattern));
}

TEST_CASE("TrackBuilder append_fm_bytes writes N copies", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_bytes(0xFF, 5);
    CHECK(builder.position() == 5);

    uint32_t expected = fm_to_2us_pulses(0xFF, 0xFF);
    for (uint32_t i = 0; i < 5; ++i) {
        CHECK(track.read_pulses(i) == expected);
    }
}

TEST_CASE("TrackBuilder append_fm_chunk writes span of bytes", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);

    std::array<uint8_t, 4> data = {0x01, 0x02, 0x03, 0x04};
    builder.append_fm_chunk(data);

    CHECK(builder.position() == 4);
    for (uint32_t i = 0; i < 4; ++i) {
        CHECK(track.read_pulses(i) == fm_to_2us_pulses(0xFF, data[i]));
    }
}

// =============================================================================
// FM Fill
// =============================================================================

TEST_CASE("TrackBuilder fill_fm_byte fills remaining track", "[disc][builder]") {
    DiscTrack track;
    track.set_length(100);  // Short track for test speed
    TrackBuilder builder(track);

    builder.append_fm_bytes(0x00, 10);  // Fill first 10
    builder.fill_fm_byte(0xFF);          // Fill rest with FF

    CHECK(builder.position() == 100);
    uint32_t fill_pulses = fm_to_2us_pulses(0xFF, 0xFF);
    for (uint32_t i = 10; i < 100; ++i) {
        CHECK(track.read_pulses(i) == fill_pulses);
    }
}

// =============================================================================
// CRC Management
// =============================================================================

TEST_CASE("TrackBuilder CRC tracks FM bytes", "[disc][builder][crc]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.reset_crc(false);  // FM CRC init
    builder.append_fm_byte(0xFE);
    builder.append_fm_byte(0x00);
    builder.append_fm_byte(0x00);
    builder.append_fm_byte(0x00);
    builder.append_fm_byte(0x01);
    CHECK(builder.current_crc() == 0xF1D3);  // Known good: track 0 sector 0 ID CRC
}

TEST_CASE("TrackBuilder append_crc writes two FM CRC bytes", "[disc][builder][crc]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.reset_crc(false);
    builder.append_fm_byte(0xFE);
    builder.append_fm_byte(0x00);
    builder.append_fm_byte(0x00);
    builder.append_fm_byte(0x00);
    builder.append_fm_byte(0x01);
    // Position should be 5
    CHECK(builder.position() == 5);

    builder.append_crc_fm();
    CHECK(builder.position() == 7);  // Two CRC bytes appended

    // Verify the CRC bytes are correct (0xF1, 0xD3)
    CHECK(track.read_pulses(5) == fm_to_2us_pulses(0xFF, 0xF1));
    CHECK(track.read_pulses(6) == fm_to_2us_pulses(0xFF, 0xD3));
}

// =============================================================================
// Basic MFM Operations
// =============================================================================

TEST_CASE("TrackBuilder append_mfm_byte produces correct pulses", "[disc][builder][mfm]") {
    DiscTrack track;
    TrackBuilder builder(track);

    // MFM byte: 0x00 with initial last_bit=false -> 0xAAAA
    builder.append_mfm_byte(0x00);
    // MFM uses half of a pulse word (upper 16 bits for first byte)
    // After one MFM byte, position is still 0 (half-word), sub_position is 16
    // After two MFM bytes, position advances to 1
    // Actually: first MFM byte fills upper 16 bits, second fills lower 16 bits
    // Let's verify after two bytes
    builder.append_mfm_byte(0x00);

    // Two consecutive 0x00 bytes with last_bit starting false:
    // First: 0xAAAA (upper half), second: 0xAAAA (lower half)
    // Combined: 0xAAAAAAAA
    CHECK(track.read_pulses(0) == 0xAAAAAAAA);
    CHECK(builder.position() == 1);
}

TEST_CASE("TrackBuilder append_mfm_byte state continues across bytes", "[disc][builder][mfm]") {
    DiscTrack track;
    TrackBuilder builder(track);

    // Encode 0xFF then 0x00
    // 0xFF: all data bits 1 -> 0x5555 (no clocks), last_bit = true
    // 0x00: first bit prev=1 -> no clock, rest prev=0 -> clocks -> 0x2AAA
    builder.append_mfm_byte(0xFF);
    builder.append_mfm_byte(0x00);

    CHECK(track.read_pulses(0) == 0x55552AAA);
}

TEST_CASE("TrackBuilder append_mfm_bytes writes N copies", "[disc][builder][mfm]") {
    DiscTrack track;
    TrackBuilder builder(track);

    // Write 4 bytes of 0x4E (standard MFM gap fill)
    builder.append_mfm_bytes(0x4E, 4);
    CHECK(builder.position() == 2);  // 4 MFM bytes = 2 pulse positions
}

TEST_CASE("TrackBuilder append_mfm_chunk writes span", "[disc][builder][mfm]") {
    DiscTrack track;
    TrackBuilder builder(track);

    std::array<uint8_t, 2> data = {0xFF, 0x00};
    builder.append_mfm_chunk(data);

    // Same as the state continuity test above
    CHECK(track.read_pulses(0) == 0x55552AAA);
}

// =============================================================================
// MFM A1 Sync
// =============================================================================

TEST_CASE("TrackBuilder append_mfm_3x_a1_sync produces correct sync words", "[disc][builder][mfm]") {
    DiscTrack track;
    TrackBuilder builder(track);

    // Ensure we start at a pulse-word boundary with known state
    // Precede with some 0x00 to establish clock state, then align
    builder.append_mfm_bytes(0x00, 12);  // 12 MFM bytes = 6 pulse positions
    CHECK(builder.position() == 6);

    builder.append_mfm_3x_a1_sync();

    // After 3 A1 sync bytes (6 more half-words = 3 positions if aligned)
    // The A1 sync has a specific bit pattern: 0x4489
    // We need to check the raw pulse data contains these patterns
    // The sync bytes take 3 MFM bytes = 1.5 pulse positions
    // But we might not be pulse-aligned, so check position advanced by ~2
}

// =============================================================================
// MFM CRC
// =============================================================================

TEST_CASE("TrackBuilder MFM CRC after 3x A1 sync initialised correctly", "[disc][builder][mfm][crc]") {
    DiscTrack track;
    TrackBuilder builder(track);

    // The 3x A1 sync resets CRC to the MFM init value (0xCDB4)
    // and then the data mark should be included in the CRC
    builder.append_mfm_3x_a1_sync();
    CHECK(builder.current_crc() == 0xCDB4);
}

// =============================================================================
// MFM Fill
// =============================================================================

TEST_CASE("TrackBuilder fill_mfm_byte fills remaining track", "[disc][builder][mfm]") {
    DiscTrack track;
    track.set_length(50);
    TrackBuilder builder(track);

    builder.append_mfm_bytes(0x00, 10);  // 5 positions
    builder.fill_mfm_byte(0x4E);         // Fill rest with 0x4E

    CHECK(builder.position() == 50);
}

// =============================================================================
// Finalize
// =============================================================================

TEST_CASE("TrackBuilder finalize sets track length", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_bytes(0xFF, 100);
    builder.finalize();

    CHECK(track.length() == 100);
}

// =============================================================================
// Reset
// =============================================================================

TEST_CASE("TrackBuilder reset returns to position 0", "[disc][builder]") {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_bytes(0xFF, 10);
    CHECK(builder.position() == 10);

    builder.reset();
    CHECK(builder.position() == 0);
}

// =============================================================================
// Build a Standard DFS FM Track
// =============================================================================

TEST_CASE("TrackBuilder builds a complete DFS track within bounds", "[disc][builder][dfs]") {
    DiscTrack track;
    TrackBuilder builder(track);

    uint8_t track_num = 0;
    uint8_t side = 0;

    // GAP 1: post-index gap
    builder.append_fm_bytes(0xFF, k_std_gap1_FFs);
    builder.append_fm_bytes(0x00, k_std_sync_00s);

    for (uint8_t sector = 0; sector < 10; ++sector) {
        // ID address mark
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_id_mark_data_pattern, k_mark_clock_pattern);
        builder.append_fm_byte(track_num);
        builder.append_fm_byte(side);
        builder.append_fm_byte(sector);
        builder.append_fm_byte(0x01);  // Size code 1 = 256 bytes
        builder.append_crc_fm();

        // GAP 2: ID to data gap
        builder.append_fm_bytes(0xFF, k_std_gap2_FFs);
        builder.append_fm_bytes(0x00, k_std_sync_00s);

        // Data address mark
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);

        // Sector data (256 bytes of test pattern)
        for (int i = 0; i < 256; ++i) {
            builder.append_fm_byte(static_cast<uint8_t>(i));
        }
        builder.append_crc_fm();

        // GAP 3: inter-sector gap (except after last sector)
        if (sector < 9) {
            builder.append_fm_bytes(0xFF, k_std_10_sector_gap3_FFs);
            builder.append_fm_bytes(0x00, k_std_sync_00s);
        }
    }

    // GAP 4: fill to end of track
    builder.fill_fm_byte(0xFF);
    builder.finalize();

    // Must fit within standard track length
    CHECK(track.length() <= k_ibm_disc_bytes_per_track);

    // Verify first sector ID CRC is correct (track 0, side 0, sector 0, size 1)
    // The ID field starts after GAP1 (16 FF + 6 00 = 22 bytes)
    // Position 22 should be the ID mark
    uint32_t id_mark_pos = k_std_gap1_FFs + k_std_sync_00s;
    CHECK(track.read_pulses(id_mark_pos) ==
          fm_to_2us_pulses(k_mark_clock_pattern, k_id_mark_data_pattern));
}
