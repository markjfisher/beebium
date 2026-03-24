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

#include <beebium/disc/IbmDiscFormat.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// =============================================================================
// CRC Tests
// =============================================================================

TEST_CASE("CRC FM initialises to 0xFFFF", "[disc][ibm][crc]") {
    CHECK(crc_init(false) == 0xFFFF);
}

TEST_CASE("CRC MFM initialises to 0xCDB4", "[disc][ibm][crc]") {
    // MFM CRC starts after 3x 0xA1 sync bytes have been processed.
    // 0xCDB4 is the CRC of 0xFFFF after processing 0xA1, 0xA1, 0xA1.
    CHECK(crc_init(true) == 0xCDB4);
}

TEST_CASE("CRC MFM init value is correct for 3x A1 sync", "[disc][ibm][crc]") {
    // Verify that processing 3x A1 bytes from FM init gives the MFM init value.
    uint16_t crc = 0xFFFF;
    crc = crc_add_byte(crc, 0xA1);
    crc = crc_add_byte(crc, 0xA1);
    crc = crc_add_byte(crc, 0xA1);
    CHECK(crc == 0xCDB4);
}

TEST_CASE("CRC for track 0 sector 0 ID header is 0xF1D3", "[disc][ibm][crc]") {
    // From beebjit: "CRC for track 0 sector 0 ID header is 0xF1 0xD3"
    // ID field: FE (mark) + 00 (track) + 00 (side) + 00 (sector) + 01 (size code)
    uint16_t crc = crc_init(false);  // FM
    crc = crc_add_byte(crc, 0xFE);   // ID address mark
    crc = crc_add_byte(crc, 0x00);   // Track 0
    crc = crc_add_byte(crc, 0x00);   // Side 0
    crc = crc_add_byte(crc, 0x00);   // Sector 0
    crc = crc_add_byte(crc, 0x01);   // Size code 1 (256 bytes)
    CHECK(crc == 0xF1D3);
}

TEST_CASE("CRC for freshly formatted sector (0xE5 fill) is 0xA40C", "[disc][ibm][crc]") {
    // From beebjit: "CRC for freshly formatted sector (full of 0xE5) is 0xA4 0x0C"
    uint16_t crc = crc_init(false);  // FM
    crc = crc_add_byte(crc, 0xFB);   // Data address mark
    for (int i = 0; i < 256; ++i) {
        crc = crc_add_byte(crc, 0xE5);
    }
    CHECK(crc == 0xA40C);
}

TEST_CASE("CRC for zero-filled sector", "[disc][ibm][crc]") {
    // Compute CRC for a sector full of zeros (common in blank DFS discs)
    uint16_t crc = crc_init(false);
    crc = crc_add_byte(crc, 0xFB);  // Data address mark
    for (int i = 0; i < 256; ++i) {
        crc = crc_add_byte(crc, 0x00);
    }
    // Just verify it's deterministic and non-zero
    CHECK(crc != 0);
    // Run again to verify determinism
    uint16_t crc2 = crc_init(false);
    crc2 = crc_add_byte(crc2, 0xFB);
    for (int i = 0; i < 256; ++i) {
        crc2 = crc_add_byte(crc2, 0x00);
    }
    CHECK(crc == crc2);
}

// =============================================================================
// FM Encoding Tests
// =============================================================================

TEST_CASE("FM encode: all zeros with all clock bits", "[disc][ibm][fm]") {
    // Data 0x00, clocks 0xFF: clock bits present, no data bits
    // Each nibble: clock=1, data=0 -> 0x4 (bit 2 set)
    // 8 nibbles of 0x4 = 0x44444444
    uint32_t pulses = fm_to_2us_pulses(0xFF, 0x00);
    CHECK(pulses == 0x44444444);
}

TEST_CASE("FM encode: all ones with all clock bits", "[disc][ibm][fm]") {
    // Data 0xFF, clocks 0xFF: both clock and data set
    // Each nibble: clock=1, data=1 -> 0x5
    // 8 nibbles of 0x5 = 0x55555555
    uint32_t pulses = fm_to_2us_pulses(0xFF, 0xFF);
    CHECK(pulses == 0x55555555);
}

TEST_CASE("FM encode: data only no clocks", "[disc][ibm][fm]") {
    // Data 0xFF, clocks 0x00: data bits only
    // Each nibble: clock=0, data=1 -> 0x1
    // 8 nibbles of 0x1 = 0x11111111
    uint32_t pulses = fm_to_2us_pulses(0x00, 0xFF);
    CHECK(pulses == 0x11111111);
}

TEST_CASE("FM encode: standard data byte with normal clocks", "[disc][ibm][fm]") {
    // Standard FM encoding: clock bits all 1, data = 0xA5 (10100101)
    // Bit 7: clock=1, data=1 -> 0x5
    // Bit 6: clock=1, data=0 -> 0x4
    // Bit 5: clock=1, data=1 -> 0x5
    // Bit 4: clock=1, data=0 -> 0x4
    // Bit 3: clock=1, data=0 -> 0x4
    // Bit 2: clock=1, data=1 -> 0x5
    // Bit 1: clock=1, data=0 -> 0x4
    // Bit 0: clock=1, data=1 -> 0x5
    uint32_t pulses = fm_to_2us_pulses(0xFF, 0xA5);
    CHECK(pulses == 0x54544545);
}

TEST_CASE("FM encode: ID address mark with special clock", "[disc][ibm][fm]") {
    // ID mark: data 0xFE, clock 0xC7 (missing clock bit pattern)
    uint32_t pulses = fm_to_2us_pulses(k_mark_clock_pattern, k_id_mark_data_pattern);
    // Verify it's non-zero and specific
    CHECK(pulses != 0);
    // Decode it back and verify
    auto [clocks, data, iffy] = fm_from_2us_pulses(pulses);
    CHECK(clocks == 0xC7);
    CHECK(data == 0xFE);
    CHECK(iffy == false);
}

// =============================================================================
// FM Decode Tests
// =============================================================================

TEST_CASE("FM decode round-trip: standard data bytes", "[disc][ibm][fm]") {
    // Test every possible byte value with standard clocks (0xFF)
    for (uint32_t byte_val = 0; byte_val < 256; ++byte_val) {
        uint8_t data = static_cast<uint8_t>(byte_val);
        uint32_t pulses = fm_to_2us_pulses(0xFF, data);
        auto [clocks, decoded_data, iffy] = fm_from_2us_pulses(pulses);
        CHECK(decoded_data == data);
        CHECK(clocks == 0xFF);
        CHECK(iffy == false);
    }
}

TEST_CASE("FM decode round-trip: address marks with special clocks", "[disc][ibm][fm]") {
    // ID mark
    {
        uint32_t pulses = fm_to_2us_pulses(k_mark_clock_pattern, k_id_mark_data_pattern);
        auto [clocks, data, iffy] = fm_from_2us_pulses(pulses);
        CHECK(data == k_id_mark_data_pattern);
        CHECK(clocks == k_mark_clock_pattern);
    }
    // Data mark
    {
        uint32_t pulses = fm_to_2us_pulses(k_mark_clock_pattern, k_data_mark_data_pattern);
        auto [clocks, data, iffy] = fm_from_2us_pulses(pulses);
        CHECK(data == k_data_mark_data_pattern);
        CHECK(clocks == k_mark_clock_pattern);
    }
    // Deleted data mark
    {
        uint32_t pulses = fm_to_2us_pulses(k_mark_clock_pattern, k_deleted_data_mark_data_pattern);
        auto [clocks, data, iffy] = fm_from_2us_pulses(pulses);
        CHECK(data == k_deleted_data_mark_data_pattern);
        CHECK(clocks == k_mark_clock_pattern);
    }
}

TEST_CASE("FM decode detects iffy pulses", "[disc][ibm][fm]") {
    // An "iffy" pulse has bits in unexpected positions within a 4-bit group.
    // Specifically, bits at positions that are neither clock (bit 2) nor data (bit 0).
    // The mask 0x50000000 checks for bits at positions 28 and 30 (the "wrong" slots).
    // Manually construct a pulse word with a pulse in an odd position.
    uint32_t normal_pulses = fm_to_2us_pulses(0xFF, 0x00);  // 0x44444444
    // Add an extra pulse in the "iffy" position (bit 1 of first nibble = bit 29)
    uint32_t iffy_pulses = normal_pulses | 0x20000000;
    auto [clocks, data, iffy] = fm_from_2us_pulses(iffy_pulses);
    CHECK(iffy == true);
}

// =============================================================================
// MFM Encoding Tests
// =============================================================================

TEST_CASE("MFM encode: zero byte after zero", "[disc][ibm][mfm]") {
    // When previous bit is 0, encoding 0x00:
    // Each bit is 0, and the previous bit is 0, so clock bit is set.
    // Pattern: 10 10 10 10 10 10 10 10 = 0xAAAA
    bool last_bit = false;
    uint16_t pulses = mfm_to_2us_pulses(last_bit, 0x00);
    CHECK(pulses == 0xAAAA);
    CHECK(last_bit == false);
}

TEST_CASE("MFM encode: zero byte after one", "[disc][ibm][mfm]") {
    // When previous bit is 1, encoding 0x00:
    // First bit: data=0, prev=1, no clock. Then all subsequent: data=0, prev=0, clock.
    // Pattern: 00 10 10 10 10 10 10 10 = 0x2AAA
    bool last_bit = true;
    uint16_t pulses = mfm_to_2us_pulses(last_bit, 0x00);
    CHECK(pulses == 0x2AAA);
    CHECK(last_bit == false);
}

TEST_CASE("MFM encode: 0xFF after zero", "[disc][ibm][mfm]") {
    // All data bits are 1: no clock bits ever set.
    // Pattern: 01 01 01 01 01 01 01 01 = 0x5555
    bool last_bit = false;
    uint16_t pulses = mfm_to_2us_pulses(last_bit, 0xFF);
    CHECK(pulses == 0x5555);
    CHECK(last_bit == true);
}

TEST_CASE("MFM encode: 0xFF after one", "[disc][ibm][mfm]") {
    // Same as above -- data bits all 1, no clocks regardless of previous
    bool last_bit = true;
    uint16_t pulses = mfm_to_2us_pulses(last_bit, 0xFF);
    CHECK(pulses == 0x5555);
    CHECK(last_bit == true);
}

TEST_CASE("MFM encode: alternating pattern 0xAA", "[disc][ibm][mfm]") {
    // 0xAA = 10101010
    // Bit 7: data=1, -> 01
    // Bit 6: data=0, prev=1 -> 00
    // Bit 5: data=1, -> 01
    // Bit 4: data=0, prev=1 -> 00
    // Bit 3: data=1, -> 01
    // Bit 2: data=0, prev=1 -> 00
    // Bit 1: data=1, -> 01
    // Bit 0: data=0, prev=1 -> 00
    // Result: 01 00 01 00 01 00 01 00 = 0x4444
    bool last_bit = false;
    uint16_t pulses = mfm_to_2us_pulses(last_bit, 0xAA);
    CHECK(pulses == 0x4444);
    CHECK(last_bit == false);  // Last data bit is 0
}

// =============================================================================
// MFM Decode Tests
// =============================================================================

TEST_CASE("MFM decode round-trip: all byte values", "[disc][ibm][mfm]") {
    // Test round-trip for every possible byte with both initial states
    for (int init_bit = 0; init_bit <= 1; ++init_bit) {
        for (uint32_t byte_val = 0; byte_val < 256; ++byte_val) {
            bool last_bit = (init_bit != 0);
            uint8_t data = static_cast<uint8_t>(byte_val);
            uint16_t pulses = mfm_to_2us_pulses(last_bit, data);
            uint8_t decoded = mfm_from_2us_pulses(pulses);
            CHECK(decoded == data);
        }
    }
}

TEST_CASE("MFM A1 sync word is 0x4489", "[disc][ibm][mfm]") {
    CHECK(k_mfm_a1_sync == 0x4489);
}

TEST_CASE("MFM C2 sync word is 0x5224", "[disc][ibm][mfm]") {
    CHECK(k_mfm_c2_sync == 0x5224);
}

// =============================================================================
// MFM State Continuity Tests
// =============================================================================

TEST_CASE("MFM encoding maintains state across bytes", "[disc][ibm][mfm]") {
    // Encode two consecutive bytes and check that the state flows correctly
    bool last_bit = false;

    // First byte: 0x00 (all zeros, prev=0 -> all clocks)
    uint16_t p1 = mfm_to_2us_pulses(last_bit, 0x00);
    CHECK(last_bit == false);
    CHECK(p1 == 0xAAAA);

    // Second byte: 0x00 again (prev still 0 -> same result)
    uint16_t p2 = mfm_to_2us_pulses(last_bit, 0x00);
    CHECK(p2 == 0xAAAA);

    // Now encode 0x80 (10000000) after the zeros
    uint16_t p3 = mfm_to_2us_pulses(last_bit, 0x80);
    // Bit 7: data=1, prev=0 -> 01
    // Bits 6-0: data=0, prev from preceding: 00 10 10 10 10 10 10
    // Pattern: 01 00 10 10 10 10 10 10 = 0x4AAA... let's just decode it
    uint8_t decoded = mfm_from_2us_pulses(p3);
    CHECK(decoded == 0x80);
    CHECK(last_bit == false);
}

// =============================================================================
// Constants Tests
// =============================================================================

TEST_CASE("Standard constants are correct", "[disc][ibm][constants]") {
    CHECK(k_ibm_disc_bytes_per_track == 3125);
    CHECK(k_ibm_disc_tracks_per_disc == 84);
    CHECK(k_mark_clock_pattern == 0xC7);
    CHECK(k_id_mark_data_pattern == 0xFE);
    CHECK(k_data_mark_data_pattern == 0xFB);
    CHECK(k_deleted_data_mark_data_pattern == 0xF8);
    CHECK(k_std_sync_00s == 6);
    CHECK(k_std_gap1_FFs == 16);
    CHECK(k_std_gap2_FFs == 11);
    CHECK(k_std_10_sector_gap3_FFs == 21);
}
