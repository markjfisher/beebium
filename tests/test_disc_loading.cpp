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

// Tests for disc image loading via the DiscFormatRegistry, covering:
// - Successful loading of each supported format (SSD, DSD, ADFS variants, HFE)
// - Rejection of malformed, empty, and unrecognised disc images
// - Correct error messages and failure modes
//
// Test images live in tests/assets/discs/ (valid) and tests/assets/discs/malformed/ (invalid).

#include <beebium/disc/DiscLoader.hpp>
#include <beebium/disc/DiscFormatRegistry.hpp>
#include <beebium/disc/TrackDecoder.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace beebium;

static const std::filesystem::path assets_dirpath{BEEBIUM_TEST_ASSETS_DIR};
static const auto discs_dirpath = assets_dirpath / "discs";
static const auto malformed_dirpath = discs_dirpath / "malformed";

// =============================================================================
// Successful loading of real disc images
// =============================================================================

TEST_CASE("Load real SSD image", "[disc][loading][ssd]") {
    auto filepath = discs_dirpath / "Disc999-EliteSNG45.ssd";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());
    CHECK(result.disc->name() == "Disc999-EliteSNG45.ssd");
    CHECK(result.disc->format_name() == "SSD");
    CHECK_FALSE(result.disc->is_double_sided());
}

TEST_CASE("Load real SSD image and verify sectors via TrackDecoder", "[disc][loading][ssd]") {
    auto filepath = discs_dirpath / "Disc999-EliteSNG45.ssd";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());

    // Read raw bytes for comparison
    std::ifstream raw(filepath, std::ios::binary);
    std::vector<uint8_t> raw_bytes((std::istreambuf_iterator<char>(raw)),
                                    std::istreambuf_iterator<char>());

    // Verify sector 0 on track 0 matches raw file
    TrackDecoder decoder(result.disc->track(false, 0));
    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    std::array<uint8_t, 256> buffer{};
    decoder.read_sector_data(sectors[0], buffer);
    for (int i = 0; i < 256; ++i) {
        CHECK(buffer[i] == raw_bytes[i]);
    }
}

TEST_CASE("Load real HFE v3 image", "[disc][loading][hfe]") {
    auto filepath = discs_dirpath / "Citadel_FSD0174_1.hfe";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());
    CHECK(result.disc->name() == "Citadel_FSD0174_1.hfe");
    CHECK(result.disc->format_name() == "HFE v3");
}

TEST_CASE("Load real HFE image and find sectors on track 0", "[disc][loading][hfe]") {
    auto filepath = discs_dirpath / "Citadel_FSD0174_1.hfe";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());

    // HFE images of BBC discs should have FM-encoded sectors on track 0
    TrackDecoder decoder(result.disc->track(false, 0));
    auto sectors = decoder.find_sectors();
    // DFS disc: expect 10 FM sectors
    WARN("HFE track 0: " << sectors.size() << " sectors found");
    CHECK(sectors.size() >= 1);  // At least some sectors should be decodable
}

TEST_CASE("Load real ADL image", "[disc][loading][adfs]") {
    auto filepath = discs_dirpath / "adl.adl";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "ADL");
    CHECK(result.disc->is_double_sided());
}

TEST_CASE("Load real ADL image and verify MFM sectors", "[disc][loading][adfs]") {
    auto filepath = discs_dirpath / "adl.adl";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());

    // ADL disc: expect 16 MFM sectors on track 0
    TrackDecoder decoder(result.disc->track(false, 0));
    auto sectors = decoder.find_mfm_sectors();
    CHECK(sectors.size() == 16);

    // Verify data matches raw file
    std::ifstream raw(filepath, std::ios::binary);
    std::vector<uint8_t> raw_bytes((std::istreambuf_iterator<char>(raw)),
                                    std::istreambuf_iterator<char>());

    std::array<uint8_t, 256> buffer{};
    decoder.read_sector_data(sectors[0], buffer);
    for (int i = 0; i < 256; ++i) {
        CHECK(buffer[i] == raw_bytes[i]);
    }
}

TEST_CASE("Load real ADM image", "[disc][loading][adfs]") {
    auto filepath = discs_dirpath / "adm.adm";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "ADM");
    CHECK_FALSE(result.disc->is_double_sided());
}

TEST_CASE("Load real ADS image", "[disc][loading][adfs]") {
    auto filepath = discs_dirpath / "ads.ads";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "ADS");
    CHECK_FALSE(result.disc->is_double_sided());
}

TEST_CASE("Load second real ADL image (l3server)", "[disc][loading][adfs]") {
    auto filepath = discs_dirpath / "l3server.adl";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "ADL");
    CHECK(result.disc->is_double_sided());
}

// =============================================================================
// Rejection of malformed disc images
// =============================================================================

TEST_CASE("Reject empty SSD file", "[disc][loading][malformed]") {
    auto filepath = malformed_dirpath / "empty.ssd";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.error.empty());
    WARN("empty.ssd error: " << result.error);
}

TEST_CASE("Reject empty HFE file", "[disc][loading][malformed]") {
    auto filepath = malformed_dirpath / "empty.hfe";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.error.empty());
    WARN("empty.hfe error: " << result.error);
}

TEST_CASE("Reject SSD with wrong size (not sector-aligned)", "[disc][loading][malformed]") {
    auto filepath = malformed_dirpath / "wrong_size.ssd";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.error.empty());
    WARN("wrong_size.ssd error: " << result.error);
}

TEST_CASE("Reject truncated HFE (valid signature, truncated body)", "[disc][loading][malformed]") {
    auto filepath = malformed_dirpath / "truncated.hfe";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.error.empty());
    WARN("truncated.hfe error: " << result.error);
}

TEST_CASE("Reject non-HFE content with .hfe extension", "[disc][loading][malformed]") {
    auto filepath = malformed_dirpath / "not_hfe.hfe";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.error.empty());
    WARN("not_hfe.hfe error: " << result.error);
}

TEST_CASE("Reject file with unknown extension", "[disc][loading][malformed]") {
    auto filepath = malformed_dirpath / "mystery.xyz";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.error.empty());
    WARN("mystery.xyz error: " << result.error);
}

TEST_CASE("Report error for nonexistent file", "[disc][loading][malformed]") {
    auto filepath = discs_dirpath / "does_not_exist.ssd";

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.error.empty());
    WARN("nonexistent error: " << result.error);
}

TEST_CASE("Random content with valid SSD size still loads (sector-aligned)", "[disc][loading][malformed]") {
    // A 2560-byte file with .ssd extension IS a valid 1-track SSD
    // (10 sectors * 256 bytes). The content is random but the format
    // constraints are met. This SHOULD load -- it's a valid disc image
    // even if the data is nonsensical.
    auto filepath = malformed_dirpath / "random_content.ssd";
    REQUIRE(std::filesystem::exists(filepath));

    auto result = load_disc_from_url_or_filepath(filepath.string());
    CHECK(result.success());
    if (result.success()) {
        WARN("random_content.ssd loaded as: " << result.disc->format_name());
    }
}
