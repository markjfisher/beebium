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

#include <beebium/disc/formats/AdfsFormatHandler.hpp>
#include <beebium/disc/formats/SsdFormatHandler.hpp>
#include <beebium/disc/DiscFormatRegistry.hpp>
#include <beebium/disc/TrackDecoder.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// ADFS constants
static constexpr uint8_t k_adfs_spt = 16;
static constexpr uint16_t k_adfs_ss = 256;
static constexpr size_t k_adfs_bpt = k_adfs_spt * k_adfs_ss;  // 4096

// Helper: create an in-memory ADL image (80 tracks, 2 sides).
static std::vector<uint8_t> make_adl_image() {
    std::vector<uint8_t> data(80 * 2 * k_adfs_bpt, 0);
    // Fill each sector with a unique pattern
    for (uint8_t t = 0; t < 80; ++t) {
        for (uint8_t s = 0; s < 2; ++s) {
            for (uint8_t sec = 0; sec < k_adfs_spt; ++sec) {
                size_t offset = (static_cast<size_t>(t) * 2 + s) * k_adfs_bpt + sec * k_adfs_ss;
                uint8_t fill = static_cast<uint8_t>((t + s * 80 + sec) & 0xFF);
                std::fill_n(data.data() + offset, k_adfs_ss, fill);
            }
        }
    }
    return data;
}

// Helper: create an in-memory ADM image (80 tracks, 1 side).
static std::vector<uint8_t> make_adm_image() {
    std::vector<uint8_t> data(80 * 1 * k_adfs_bpt, 0);
    for (uint8_t t = 0; t < 80; ++t) {
        for (uint8_t sec = 0; sec < k_adfs_spt; ++sec) {
            size_t offset = static_cast<size_t>(t) * k_adfs_bpt + sec * k_adfs_ss;
            uint8_t fill = static_cast<uint8_t>((t * 16 + sec) & 0xFF);
            std::fill_n(data.data() + offset, k_adfs_ss, fill);
        }
    }
    return data;
}

// =============================================================================
// Detection Tests
// =============================================================================

TEST_CASE("AdfsFormatHandler detects ADL", "[disc][adfs][detect]") {
    AdfsFormatHandler handler;
    auto data = make_adl_image();
    auto result = handler.detect(data, ".adl");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
    CHECK(result.format_name == "ADL");
}

TEST_CASE("AdfsFormatHandler detects ADM", "[disc][adfs][detect]") {
    AdfsFormatHandler handler;
    auto data = make_adm_image();
    auto result = handler.detect(data, ".adm");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
}

TEST_CASE("AdfsFormatHandler detects ADS", "[disc][adfs][detect]") {
    AdfsFormatHandler handler;
    std::vector<uint8_t> data(40 * 1 * k_adfs_bpt, 0);
    auto result = handler.detect(data, ".ads");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
}

TEST_CASE("AdfsFormatHandler auto-detects ADF from size", "[disc][adfs][detect]") {
    AdfsFormatHandler handler;

    // Small -> ADFS-S
    {
        std::vector<uint8_t> data(40 * 1 * k_adfs_bpt, 0);
        auto result = handler.detect(data, ".adf");
        CHECK(result.detected);
    }

    // Medium -> ADFS-M
    {
        auto data = make_adm_image();
        auto result = handler.detect(data, ".adf");
        CHECK(result.detected);
    }

    // Large -> ADFS-L
    {
        auto data = make_adl_image();
        auto result = handler.detect(data, ".adf");
        CHECK(result.detected);
    }
}

TEST_CASE("AdfsFormatHandler rejects wrong extension", "[disc][adfs][detect]") {
    AdfsFormatHandler handler;
    auto data = make_adl_image();
    auto result = handler.detect(data, ".ssd");
    CHECK_FALSE(result.detected);
}

TEST_CASE("AdfsFormatHandler rejects empty file", "[disc][adfs][detect]") {
    AdfsFormatHandler handler;
    std::vector<uint8_t> data;
    auto result = handler.detect(data, ".adl");
    CHECK_FALSE(result.detected);
}

// =============================================================================
// Loading Tests
// =============================================================================

TEST_CASE("AdfsFormatHandler loads ADL", "[disc][adfs][load]") {
    AdfsFormatHandler handler;
    auto data = make_adl_image();
    auto result = handler.load(data, "/tmp/test.adl");

    REQUIRE(result.success());
    CHECK(result.disc->name() == "test.adl");
    CHECK(result.disc->is_double_sided());
    CHECK(result.disc->format_name() == "ADL");
}

TEST_CASE("AdfsFormatHandler loads ADM as single-sided", "[disc][adfs][load]") {
    AdfsFormatHandler handler;
    auto data = make_adm_image();
    auto result = handler.load(data, "/tmp/test.adm");

    REQUIRE(result.success());
    CHECK_FALSE(result.disc->is_double_sided());
}

// =============================================================================
// MFM Sector Round-Trip Tests
// =============================================================================

TEST_CASE("Loaded ADL has correct MFM sectors via TrackDecoder", "[disc][adfs][load][roundtrip]") {
    AdfsFormatHandler handler;
    auto data = make_adl_image();
    auto result = handler.load(data, "/tmp/test.adl");
    REQUIRE(result.success());

    // Verify sectors on track 0 side 0
    {
        TrackDecoder decoder(result.disc->track(false, 0));
        auto sectors = decoder.find_mfm_sectors();
        REQUIRE(sectors.size() == 16);

        for (uint8_t s = 0; s < 16; ++s) {
            CHECK(sectors[s].track == 0);
            CHECK(sectors[s].sector == s);
            CHECK(sectors[s].size_code == 1);
            CHECK_FALSE(sectors[s].has_header_crc_error);
            CHECK_FALSE(sectors[s].has_data_crc_error);
            CHECK_FALSE(sectors[s].is_fm);

            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((0 + 0 * 80 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }

    // Verify track 0 side 1
    {
        TrackDecoder decoder(result.disc->track(true, 0));
        auto sectors = decoder.find_mfm_sectors();
        REQUIRE(sectors.size() == 16);

        for (uint8_t s = 0; s < 16; ++s) {
            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((0 + 1 * 80 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }

    // Verify last track
    {
        TrackDecoder decoder(result.disc->track(false, 79));
        auto sectors = decoder.find_mfm_sectors();
        REQUIRE(sectors.size() == 16);

        for (uint8_t s = 0; s < 16; ++s) {
            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((79 + 0 * 80 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }
}

TEST_CASE("Loaded ADM has correct MFM sectors", "[disc][adfs][load][roundtrip]") {
    AdfsFormatHandler handler;
    auto data = make_adm_image();
    auto result = handler.load(data, "/tmp/test.adm");
    REQUIRE(result.success());

    TrackDecoder decoder(result.disc->track(false, 0));
    auto sectors = decoder.find_mfm_sectors();
    REQUIRE(sectors.size() == 16);

    for (uint8_t s = 0; s < 16; ++s) {
        std::array<uint8_t, 256> buffer{};
        decoder.read_sector_data(sectors[s], buffer);

        uint8_t expected = static_cast<uint8_t>((0 * 16 + s) & 0xFF);
        for (int i = 0; i < 256; ++i) {
            CHECK(buffer[i] == expected);
        }
    }
}

// =============================================================================
// Auto-Detection finds ADFS alongside SSD
// =============================================================================

TEST_CASE("Registry distinguishes ADL from SSD by extension", "[disc][adfs][registry]") {
    DiscFormatRegistry registry;
    registry.register_handler(std::make_unique<SsdFormatHandler>());
    registry.register_handler(std::make_unique<AdfsFormatHandler>());

    auto data = make_adl_image();
    auto result = registry.load_from_data(data, ".adl", "/tmp/test.adl");

    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "ADL");
}

// =============================================================================
// find_sectors() auto-detection (FM first, falls back to MFM)
// =============================================================================

TEST_CASE("TrackDecoder find_sectors auto-detects MFM when no FM found", "[disc][adfs][decoder]") {
    AdfsFormatHandler handler;
    auto data = make_adl_image();
    auto result = handler.load(data, "/tmp/test.adl");
    REQUIRE(result.success());

    TrackDecoder decoder(result.disc->track(false, 0));
    auto sectors = decoder.find_sectors();  // Should auto-detect MFM
    CHECK(sectors.size() == 16);
    CHECK_FALSE(sectors[0].is_fm);
}
