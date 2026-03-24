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

#include <beebium/disc/TrackDecoder.hpp>
#include <beebium/disc/TrackBuilder.hpp>
#include <beebium/disc/IbmDiscFormat.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// Helper: build a standard DFS FM track with 10 sectors.
// Sector data is filled with sector_number repeated.
static DiscTrack build_dfs_track(uint8_t track_num, uint8_t side) {
    DiscTrack track;
    TrackBuilder builder(track);

    // GAP 1
    builder.append_fm_bytes(0xFF, k_std_gap1_FFs);
    builder.append_fm_bytes(0x00, k_std_sync_00s);

    for (uint8_t sector = 0; sector < 10; ++sector) {
        // ID field
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_id_mark_data_pattern, k_mark_clock_pattern);
        builder.append_fm_byte(track_num);
        builder.append_fm_byte(side);
        builder.append_fm_byte(sector);
        builder.append_fm_byte(0x01);  // 256 bytes
        builder.append_crc_fm();

        // GAP 2
        builder.append_fm_bytes(0xFF, k_std_gap2_FFs);
        builder.append_fm_bytes(0x00, k_std_sync_00s);

        // Data field
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);
        for (int i = 0; i < 256; ++i) {
            builder.append_fm_byte(sector);  // Fill with sector number
        }
        builder.append_crc_fm();

        // GAP 3
        if (sector < 9) {
            builder.append_fm_bytes(0xFF, k_std_10_sector_gap3_FFs);
            builder.append_fm_bytes(0x00, k_std_sync_00s);
        }
    }

    builder.fill_fm_byte(0xFF);
    builder.finalize();
    return track;
}

// Helper: build a DFS FM track with specific sector data.
static DiscTrack build_dfs_track_with_data(uint8_t track_num, uint8_t side,
                                            const std::array<std::array<uint8_t, 256>, 10>& sector_data) {
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_bytes(0xFF, k_std_gap1_FFs);
    builder.append_fm_bytes(0x00, k_std_sync_00s);

    for (uint8_t sector = 0; sector < 10; ++sector) {
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_id_mark_data_pattern, k_mark_clock_pattern);
        builder.append_fm_byte(track_num);
        builder.append_fm_byte(side);
        builder.append_fm_byte(sector);
        builder.append_fm_byte(0x01);
        builder.append_crc_fm();

        builder.append_fm_bytes(0xFF, k_std_gap2_FFs);
        builder.append_fm_bytes(0x00, k_std_sync_00s);

        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);
        builder.append_fm_chunk(sector_data[sector]);
        builder.append_crc_fm();

        if (sector < 9) {
            builder.append_fm_bytes(0xFF, k_std_10_sector_gap3_FFs);
            builder.append_fm_bytes(0x00, k_std_sync_00s);
        }
    }

    builder.fill_fm_byte(0xFF);
    builder.finalize();
    return track;
}

// =============================================================================
// Finding Sectors
// =============================================================================

TEST_CASE("TrackDecoder finds 10 sectors on a standard DFS FM track", "[disc][decoder]") {
    auto track = build_dfs_track(0, 0);
    TrackDecoder decoder(track);

    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    for (uint8_t i = 0; i < 10; ++i) {
        CHECK(sectors[i].track == 0);
        CHECK(sectors[i].side == 0);
        CHECK(sectors[i].sector == i);
        CHECK(sectors[i].size_code == 1);  // 256 bytes
        CHECK_FALSE(sectors[i].has_header_crc_error);
        CHECK_FALSE(sectors[i].is_deleted);
        CHECK_FALSE(sectors[i].has_data_crc_error);
    }
}

TEST_CASE("TrackDecoder finds sectors on track 39 side 1", "[disc][decoder]") {
    auto track = build_dfs_track(39, 1);
    TrackDecoder decoder(track);

    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    for (uint8_t i = 0; i < 10; ++i) {
        CHECK(sectors[i].track == 39);
        CHECK(sectors[i].side == 1);
        CHECK(sectors[i].sector == i);
    }
}

// =============================================================================
// Reading Sector Data
// =============================================================================

TEST_CASE("TrackDecoder reads correct sector data", "[disc][decoder]") {
    auto track = build_dfs_track(0, 0);
    TrackDecoder decoder(track);

    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    for (uint8_t sec = 0; sec < 10; ++sec) {
        std::array<uint8_t, 256> buffer{};
        uint32_t bytes_read = decoder.read_sector_data(sectors[sec], buffer);
        REQUIRE(bytes_read == 256);

        // Each sector was filled with its sector number
        for (int i = 0; i < 256; ++i) {
            CHECK(buffer[i] == sec);
        }
    }
}

TEST_CASE("TrackDecoder reads varied sector data correctly", "[disc][decoder]") {
    // Create sectors with distinct data patterns
    std::array<std::array<uint8_t, 256>, 10> data{};
    for (uint8_t s = 0; s < 10; ++s) {
        for (int i = 0; i < 256; ++i) {
            data[s][i] = static_cast<uint8_t>((s * 37 + i) & 0xFF);
        }
    }

    auto track = build_dfs_track_with_data(5, 0, data);
    TrackDecoder decoder(track);

    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    for (uint8_t s = 0; s < 10; ++s) {
        std::array<uint8_t, 256> buffer{};
        uint32_t bytes_read = decoder.read_sector_data(sectors[s], buffer);
        REQUIRE(bytes_read == 256);

        for (int i = 0; i < 256; ++i) {
            CHECK(buffer[i] == data[s][i]);
        }
    }
}

// =============================================================================
// CRC Verification
// =============================================================================

TEST_CASE("TrackDecoder reports no CRC errors on valid track", "[disc][decoder][crc]") {
    auto track = build_dfs_track(0, 0);
    TrackDecoder decoder(track);

    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    for (const auto& sec : sectors) {
        CHECK_FALSE(sec.has_header_crc_error);
        CHECK_FALSE(sec.has_data_crc_error);
    }
}

// =============================================================================
// Deleted Data Marks
// =============================================================================

TEST_CASE("TrackDecoder detects deleted data marks", "[disc][decoder][deleted]") {
    // Build a track where sector 3 has a deleted data mark
    DiscTrack track;
    TrackBuilder builder(track);

    builder.append_fm_bytes(0xFF, k_std_gap1_FFs);
    builder.append_fm_bytes(0x00, k_std_sync_00s);

    for (uint8_t sector = 0; sector < 10; ++sector) {
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_id_mark_data_pattern, k_mark_clock_pattern);
        builder.append_fm_byte(0);
        builder.append_fm_byte(0);
        builder.append_fm_byte(sector);
        builder.append_fm_byte(0x01);
        builder.append_crc_fm();

        builder.append_fm_bytes(0xFF, k_std_gap2_FFs);
        builder.append_fm_bytes(0x00, k_std_sync_00s);

        builder.reset_crc(false);
        if (sector == 3) {
            builder.append_fm_data_and_clocks(k_deleted_data_mark_data_pattern, k_mark_clock_pattern);
        } else {
            builder.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);
        }
        for (int i = 0; i < 256; ++i) {
            builder.append_fm_byte(0x00);
        }
        builder.append_crc_fm();

        if (sector < 9) {
            builder.append_fm_bytes(0xFF, k_std_10_sector_gap3_FFs);
            builder.append_fm_bytes(0x00, k_std_sync_00s);
        }
    }

    builder.fill_fm_byte(0xFF);
    builder.finalize();

    TrackDecoder decoder(track);
    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    for (uint8_t i = 0; i < 10; ++i) {
        if (i == 3) {
            CHECK(sectors[i].is_deleted);
        } else {
            CHECK_FALSE(sectors[i].is_deleted);
        }
    }
}

// =============================================================================
// Empty Track
// =============================================================================

TEST_CASE("TrackDecoder finds no sectors on empty track", "[disc][decoder]") {
    DiscTrack track;  // All zeros
    TrackDecoder decoder(track);

    auto sectors = decoder.find_sectors();
    CHECK(sectors.empty());
}

TEST_CASE("TrackDecoder finds no sectors on gap-filled track", "[disc][decoder]") {
    DiscTrack track;
    TrackBuilder builder(track);
    builder.fill_fm_byte(0xFF);
    builder.finalize();

    TrackDecoder decoder(track);
    auto sectors = decoder.find_sectors();
    CHECK(sectors.empty());
}
