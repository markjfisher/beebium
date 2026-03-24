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

#include <beebium/disc/formats/SsdFormatHandler.hpp>
#include <beebium/disc/DiscFormatRegistry.hpp>
#include <beebium/disc/TrackDecoder.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// Helper: create an in-memory SSD image with known sector data.
// Each sector is filled with (track * 10 + sector) & 0xFF.
static std::vector<uint8_t> make_ssd_image(uint8_t num_tracks) {
    std::vector<uint8_t> data(num_tracks * 10 * 256);
    for (uint8_t t = 0; t < num_tracks; ++t) {
        for (uint8_t s = 0; s < 10; ++s) {
            size_t offset = (t * 10 + s) * 256;
            uint8_t fill = static_cast<uint8_t>((t * 10 + s) & 0xFF);
            std::fill_n(data.data() + offset, 256, fill);
        }
    }
    return data;
}

// Helper: create an in-memory DSD image with known sector data.
static std::vector<uint8_t> make_dsd_image(uint8_t num_tracks) {
    std::vector<uint8_t> data(num_tracks * 2 * 10 * 256);
    for (uint8_t t = 0; t < num_tracks; ++t) {
        for (uint8_t side = 0; side < 2; ++side) {
            for (uint8_t s = 0; s < 10; ++s) {
                size_t offset = (static_cast<size_t>(t) * 2 + side) * 10 * 256 + s * 256;
                uint8_t fill = static_cast<uint8_t>((t * 20 + side * 10 + s) & 0xFF);
                std::fill_n(data.data() + offset, 256, fill);
            }
        }
    }
    return data;
}

// =============================================================================
// Detection Tests
// =============================================================================

TEST_CASE("SsdFormatHandler detects standard SSD 80-track", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.detect(data, ".ssd");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
    CHECK(result.format_name == "SSD 80-track");
}

TEST_CASE("SsdFormatHandler detects standard SSD 40-track", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(40);
    auto result = handler.detect(data, ".ssd");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
    CHECK(result.format_name == "SSD 40-track");
}

TEST_CASE("SsdFormatHandler detects standard DSD 80-track", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_dsd_image(80);
    auto result = handler.detect(data, ".dsd");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
}

TEST_CASE("SsdFormatHandler detects truncated SSD", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    // 3 tracks of data (7680 bytes) - truncated
    auto data = make_ssd_image(3);
    auto result = handler.detect(data, ".ssd");

    CHECK(result.detected);
    CHECK(result.confidence >= 70);
    CHECK(result.confidence < 90);  // Lower than standard
    CHECK(result.format_name == "SSD (truncated)");
}

TEST_CASE("SsdFormatHandler rejects wrong extension", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.detect(data, ".hfe");

    CHECK_FALSE(result.detected);
}

TEST_CASE("SsdFormatHandler rejects non-sector-aligned size", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    std::vector<uint8_t> data(1000);  // Not a multiple of 256
    auto result = handler.detect(data, ".ssd");

    CHECK_FALSE(result.detected);
}

TEST_CASE("SsdFormatHandler rejects empty file", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    std::vector<uint8_t> data;
    auto result = handler.detect(data, ".ssd");

    CHECK_FALSE(result.detected);
}

TEST_CASE("SsdFormatHandler rejects oversized SSD", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    std::vector<uint8_t> data(81 * 10 * 256);  // 81 tracks > max 80
    auto result = handler.detect(data, ".ssd");

    CHECK_FALSE(result.detected);
}

// =============================================================================
// Loading Tests
// =============================================================================

TEST_CASE("SsdFormatHandler loads standard SSD 80-track", "[disc][ssd][load]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.load(data, "/tmp/test.ssd");

    REQUIRE(result.success());
    CHECK(result.disc->name() == "test.ssd");
    CHECK_FALSE(result.disc->is_double_sided());
    CHECK(result.disc->format_name() == "SSD");
}

TEST_CASE("SsdFormatHandler loads DSD and marks as double-sided", "[disc][ssd][load]") {
    SsdFormatHandler handler;
    auto data = make_dsd_image(80);
    auto result = handler.load(data, "/tmp/test.dsd");

    REQUIRE(result.success());
    CHECK(result.disc->is_double_sided());
    CHECK(result.disc->format_name() == "DSD");
}

TEST_CASE("Loaded SSD has correct sector data via TrackDecoder", "[disc][ssd][load][roundtrip]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.load(data, "/tmp/test.ssd");
    REQUIRE(result.success());

    // Verify sectors on several tracks
    for (uint8_t t : {0, 1, 39, 79}) {
        TrackDecoder decoder(result.disc->track(false, t));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            CHECK(sectors[s].track == t);
            CHECK(sectors[s].side == 0);
            CHECK(sectors[s].sector == s);
            CHECK_FALSE(sectors[s].has_header_crc_error);
            CHECK_FALSE(sectors[s].has_data_crc_error);

            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((t * 10 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }
}

TEST_CASE("Loaded DSD has correct sector data on both sides", "[disc][ssd][load][roundtrip]") {
    SsdFormatHandler handler;
    auto data = make_dsd_image(40);
    auto result = handler.load(data, "/tmp/test.dsd");
    REQUIRE(result.success());

    for (uint8_t side = 0; side < 2; ++side) {
        TrackDecoder decoder(result.disc->track(side == 1, 0));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            CHECK(sectors[s].side == side);

            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((0 * 20 + side * 10 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }
}

// =============================================================================
// Truncated Image Tests
// =============================================================================

TEST_CASE("Truncated SSD loads successfully with zero-filled missing sectors", "[disc][ssd][truncated]") {
    SsdFormatHandler handler;
    // Only 3 tracks of data (beebasm-style truncated image)
    auto data = make_ssd_image(3);
    auto result = handler.load(data, "/tmp/truncated.ssd");
    REQUIRE(result.success());

    // Verify the 3 tracks that have data
    for (uint8_t t = 0; t < 3; ++t) {
        TrackDecoder decoder(result.disc->track(false, t));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((t * 10 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }

    // Verify track 3 onwards has zero-filled sectors (blank formatted)
    {
        TrackDecoder decoder(result.disc->track(false, 3));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            std::array<uint8_t, 256> buffer{};
            buffer.fill(0xFF);  // Pre-fill to detect zero-fill
            decoder.read_sector_data(sectors[s], buffer);

            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == 0x00);
            }
        }
    }
}

TEST_CASE("Truncated SSD with partial track loads correctly", "[disc][ssd][truncated]") {
    SsdFormatHandler handler;
    // 2 full tracks + 5 sectors (2 * 2560 + 5 * 256 = 6400 bytes)
    std::vector<uint8_t> data(6400);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto result = handler.load(data, "/tmp/partial.ssd");
    REQUIRE(result.success());

    // Track 2 should have 5 sectors with data and 5 zero-filled
    TrackDecoder decoder(result.disc->track(false, 2));
    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    // First 5 sectors have data from the file
    for (uint8_t s = 0; s < 5; ++s) {
        std::array<uint8_t, 256> buffer{};
        decoder.read_sector_data(sectors[s], buffer);
        // Verify first byte matches expected offset
        size_t expected_offset = (2 * 10 + s) * 256;
        CHECK(buffer[0] == static_cast<uint8_t>(expected_offset & 0xFF));
    }

    // Last 5 sectors should be zero-filled
    for (uint8_t s = 5; s < 10; ++s) {
        std::array<uint8_t, 256> buffer{};
        buffer.fill(0xFF);
        decoder.read_sector_data(sectors[s], buffer);
        for (int i = 0; i < 256; ++i) {
            CHECK(buffer[i] == 0x00);
        }
    }
}

// =============================================================================
// Registry Integration Tests
// =============================================================================

TEST_CASE("DiscFormatRegistry loads SSD via auto-detection", "[disc][registry]") {
    DiscFormatRegistry registry;
    registry.register_handler(std::make_unique<SsdFormatHandler>());

    auto data = make_ssd_image(80);
    auto result = registry.load_from_data(data, ".ssd", "/tmp/test.ssd");

    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "SSD");
}

TEST_CASE("DiscFormatRegistry rejects unknown format", "[disc][registry]") {
    DiscFormatRegistry registry;
    registry.register_handler(std::make_unique<SsdFormatHandler>());

    std::vector<uint8_t> data(1024);
    auto result = registry.load_from_data(data, ".xyz", "/tmp/test.xyz");

    CHECK_FALSE(result.success());
}

TEST_CASE("DiscFormatRegistry reports error for empty handler list", "[disc][registry]") {
    DiscFormatRegistry registry;  // No handlers registered

    std::vector<uint8_t> data(1024);
    auto result = registry.load_from_data(data, ".ssd", "/tmp/test.ssd");

    CHECK_FALSE(result.success());
}
