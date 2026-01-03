// Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/disc/DiscGeometry.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>

using namespace beebium;

// Phase 1.1: Test SSD-40 detection (102,400 bytes, .ssd)
TEST_CASE("DiscGeometry detects SSD-40 format", "[disc][geometry]") {
    constexpr size_t SSD_40_SIZE = 40 * 10 * 256;  // 40 tracks, 10 sectors, 256 bytes = 102,400

    auto geometry = DiscGeometry::detect_from_size(SSD_40_SIZE, ".ssd");

    REQUIRE(geometry.has_value());
    CHECK(geometry->format == DiscFormat::SSD);
    CHECK(geometry->sides == 1);
    CHECK(geometry->tracks_per_side == 40);
    CHECK(geometry->sectors_per_track == 10);
    CHECK(geometry->sector_size == 256);
    CHECK(geometry->total_size() == SSD_40_SIZE);
}

// Phase 1.2: Test SSD-80 detection (204,800 bytes, .ssd)
TEST_CASE("DiscGeometry detects SSD-80 format", "[disc][geometry]") {
    constexpr size_t SSD_80_SIZE = 80 * 10 * 256;  // 80 tracks, 10 sectors, 256 bytes = 204,800

    auto geometry = DiscGeometry::detect_from_size(SSD_80_SIZE, ".ssd");

    REQUIRE(geometry.has_value());
    CHECK(geometry->format == DiscFormat::SSD);
    CHECK(geometry->sides == 1);
    CHECK(geometry->tracks_per_side == 80);
    CHECK(geometry->sectors_per_track == 10);
    CHECK(geometry->sector_size == 256);
    CHECK(geometry->total_size() == SSD_80_SIZE);
}

// Phase 1.3: Test DSD-80 detection (409,600 bytes, .dsd)
TEST_CASE("DiscGeometry detects DSD-80 format", "[disc][geometry]") {
    constexpr size_t DSD_80_SIZE = 2 * 80 * 10 * 256;  // 2 sides, 80 tracks, 10 sectors, 256 bytes = 409,600

    auto geometry = DiscGeometry::detect_from_size(DSD_80_SIZE, ".dsd");

    REQUIRE(geometry.has_value());
    CHECK(geometry->format == DiscFormat::DSD);
    CHECK(geometry->sides == 2);
    CHECK(geometry->tracks_per_side == 80);
    CHECK(geometry->sectors_per_track == 10);
    CHECK(geometry->sector_size == 256);
    CHECK(geometry->total_size() == DSD_80_SIZE);
}

// Phase 1.4: Test extension case-insensitivity
TEST_CASE("DiscGeometry handles case-insensitive extensions", "[disc][geometry]") {
    constexpr size_t SSD_80_SIZE = 80 * 10 * 256;

    SECTION("uppercase .SSD") {
        auto geometry = DiscGeometry::detect_from_size(SSD_80_SIZE, ".SSD");
        REQUIRE(geometry.has_value());
        CHECK(geometry->format == DiscFormat::SSD);
    }

    SECTION("mixed case .Ssd") {
        auto geometry = DiscGeometry::detect_from_size(SSD_80_SIZE, ".Ssd");
        REQUIRE(geometry.has_value());
        CHECK(geometry->format == DiscFormat::SSD);
    }

    SECTION("uppercase .DSD") {
        constexpr size_t DSD_80_SIZE = 2 * 80 * 10 * 256;
        auto geometry = DiscGeometry::detect_from_size(DSD_80_SIZE, ".DSD");
        REQUIRE(geometry.has_value());
        CHECK(geometry->format == DiscFormat::DSD);
    }
}

// Phase 1.5: Test invalid size/extension combinations
TEST_CASE("DiscGeometry returns nullopt for invalid combinations", "[disc][geometry]") {
    SECTION("wrong file size for .ssd") {
        auto geometry = DiscGeometry::detect_from_size(12345, ".ssd");
        CHECK_FALSE(geometry.has_value());
    }

    SECTION("wrong file size for .dsd") {
        auto geometry = DiscGeometry::detect_from_size(12345, ".dsd");
        CHECK_FALSE(geometry.has_value());
    }

    SECTION("unknown extension") {
        auto geometry = DiscGeometry::detect_from_size(204800, ".xyz");
        CHECK_FALSE(geometry.has_value());
    }

    SECTION("empty extension") {
        auto geometry = DiscGeometry::detect_from_size(204800, "");
        CHECK_FALSE(geometry.has_value());
    }
}

// Phase 1.6: Test sector_offset() for SSD (linear layout)
TEST_CASE("DiscGeometry sector_offset for SSD (linear)", "[disc][geometry]") {
    constexpr size_t SSD_80_SIZE = 80 * 10 * 256;
    auto geometry = DiscGeometry::detect_from_size(SSD_80_SIZE, ".ssd");
    REQUIRE(geometry.has_value());

    SECTION("track 0, sector 0") {
        auto offset = geometry->sector_offset(0, 0, 0);
        REQUIRE(offset.has_value());
        CHECK(*offset == 0);
    }

    SECTION("track 0, sector 1") {
        auto offset = geometry->sector_offset(0, 0, 1);
        REQUIRE(offset.has_value());
        CHECK(*offset == 256);  // Second sector at 256 bytes
    }

    SECTION("track 1, sector 0") {
        auto offset = geometry->sector_offset(0, 1, 0);
        REQUIRE(offset.has_value());
        CHECK(*offset == 10 * 256);  // 10 sectors per track = 2560 bytes
    }

    SECTION("track 79, sector 9 (last sector)") {
        auto offset = geometry->sector_offset(0, 79, 9);
        REQUIRE(offset.has_value());
        CHECK(*offset == (79 * 10 + 9) * 256);  // Last sector
    }

    SECTION("side 1 returns nullopt for single-sided disc") {
        auto offset = geometry->sector_offset(1, 0, 0);
        CHECK_FALSE(offset.has_value());
    }

    SECTION("track out of range returns nullopt") {
        auto offset = geometry->sector_offset(0, 80, 0);
        CHECK_FALSE(offset.has_value());
    }

    SECTION("sector out of range returns nullopt") {
        auto offset = geometry->sector_offset(0, 0, 10);
        CHECK_FALSE(offset.has_value());
    }
}

// Phase 1.7: Test sector_offset() for DSD (interleaved layout)
TEST_CASE("DiscGeometry sector_offset for DSD (interleaved)", "[disc][geometry]") {
    constexpr size_t DSD_80_SIZE = 2 * 80 * 10 * 256;
    auto geometry = DiscGeometry::detect_from_size(DSD_80_SIZE, ".dsd");
    REQUIRE(geometry.has_value());

    // DSD layout is interleaved: track 0 side 0, track 0 side 1, track 1 side 0, track 1 side 1, ...

    SECTION("side 0, track 0, sector 0") {
        auto offset = geometry->sector_offset(0, 0, 0);
        REQUIRE(offset.has_value());
        CHECK(*offset == 0);
    }

    SECTION("side 0, track 0, sector 1") {
        auto offset = geometry->sector_offset(0, 0, 1);
        REQUIRE(offset.has_value());
        CHECK(*offset == 256);
    }

    SECTION("side 1, track 0, sector 0") {
        // Side 1 of track 0 comes after side 0 of track 0
        auto offset = geometry->sector_offset(1, 0, 0);
        REQUIRE(offset.has_value());
        CHECK(*offset == 10 * 256);  // After 10 sectors of side 0
    }

    SECTION("side 0, track 1, sector 0") {
        // Track 1 side 0 comes after both sides of track 0
        auto offset = geometry->sector_offset(0, 1, 0);
        REQUIRE(offset.has_value());
        CHECK(*offset == 2 * 10 * 256);  // After 20 sectors (track 0 both sides)
    }

    SECTION("side 1, track 1, sector 0") {
        auto offset = geometry->sector_offset(1, 1, 0);
        REQUIRE(offset.has_value());
        CHECK(*offset == 3 * 10 * 256);  // After 30 sectors
    }

    SECTION("side 1, track 79, sector 9 (last sector)") {
        auto offset = geometry->sector_offset(1, 79, 9);
        REQUIRE(offset.has_value());
        // Track 79 starts at 79 * 2 * 10 * 256 = 79 * 5120
        // Side 1 adds 10 * 256 = 2560
        // Sector 9 adds 9 * 256 = 2304
        size_t expected = (79 * 2 * 10 + 10 + 9) * 256;
        CHECK(*offset == expected);
    }

    SECTION("side 2 returns nullopt") {
        auto offset = geometry->sector_offset(2, 0, 0);
        CHECK_FALSE(offset.has_value());
    }

    SECTION("track out of range returns nullopt") {
        auto offset = geometry->sector_offset(0, 80, 0);
        CHECK_FALSE(offset.has_value());
    }

    SECTION("sector out of range returns nullopt") {
        auto offset = geometry->sector_offset(0, 0, 10);
        CHECK_FALSE(offset.has_value());
    }
}

// Phase 1.8: Test edge cases
TEST_CASE("DiscGeometry edge cases", "[disc][geometry]") {
    SECTION("SSD-40 last sector") {
        constexpr size_t SSD_40_SIZE = 40 * 10 * 256;
        auto geometry = DiscGeometry::detect_from_size(SSD_40_SIZE, ".ssd");
        REQUIRE(geometry.has_value());

        auto offset = geometry->sector_offset(0, 39, 9);  // Last sector of 40-track disc
        REQUIRE(offset.has_value());
        CHECK(*offset == (39 * 10 + 9) * 256);
        CHECK(*offset + 256 == SSD_40_SIZE);  // Verify it's the last sector
    }

    SECTION("DSD-40 format detection") {
        constexpr size_t DSD_40_SIZE = 2 * 40 * 10 * 256;  // 204,800 bytes
        auto geometry = DiscGeometry::detect_from_size(DSD_40_SIZE, ".dsd");
        REQUIRE(geometry.has_value());
        CHECK(geometry->format == DiscFormat::DSD);
        CHECK(geometry->sides == 2);
        CHECK(geometry->tracks_per_side == 40);
    }

    SECTION("total_size() matches expected") {
        constexpr size_t SSD_80_SIZE = 80 * 10 * 256;
        auto geometry = DiscGeometry::detect_from_size(SSD_80_SIZE, ".ssd");
        REQUIRE(geometry.has_value());
        CHECK(geometry->total_size() == SSD_80_SIZE);
    }
}
