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

#include <beebium/disc/MemoryDiscImage.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>

using namespace beebium;

TEST_CASE("MemoryDiscImage create_ssd creates single-sided image", "[disc][memory]") {
    auto disc = MemoryDiscImage::create_ssd();

    CHECK(disc->name() == "memory.ssd");
    CHECK(disc->sides() == 1);
    CHECK(disc->tracks_per_side() == 80);
    CHECK(disc->sectors_per_track() == 10);
    CHECK(disc->sector_size() == 256);
    CHECK(disc->data().size() == 80 * 10 * 256);
}

TEST_CASE("MemoryDiscImage create_dsd creates double-sided image", "[disc][memory]") {
    auto disc = MemoryDiscImage::create_dsd();

    CHECK(disc->name() == "memory.dsd");
    CHECK(disc->sides() == 2);
    CHECK(disc->tracks_per_side() == 80);
    CHECK(disc->sectors_per_track() == 10);
    CHECK(disc->sector_size() == 256);
    CHECK(disc->data().size() == 2 * 80 * 10 * 256);
}

TEST_CASE("MemoryDiscImage read/write sectors", "[disc][memory]") {
    auto disc = MemoryDiscImage::create_ssd();

    std::array<uint8_t, 256> write_buffer{};
    std::fill(write_buffer.begin(), write_buffer.end(), 0xAA);

    REQUIRE(disc->write_sector(0, 5, 3, write_buffer));

    std::array<uint8_t, 256> read_buffer{};
    REQUIRE(disc->read_sector(0, 5, 3, read_buffer));

    CHECK(read_buffer == write_buffer);
}

TEST_CASE("MemoryDiscImage write protection", "[disc][memory]") {
    auto disc = MemoryDiscImage::create_ssd();

    CHECK_FALSE(disc->is_write_protected());

    disc->set_write_protected(true);
    CHECK(disc->is_write_protected());

    std::array<uint8_t, 256> buffer{};
    CHECK_FALSE(disc->write_sector(0, 0, 0, buffer));
}

TEST_CASE("MemoryDiscImage invalid sector access returns false", "[disc][memory]") {
    auto disc = MemoryDiscImage::create_ssd();
    std::array<uint8_t, 256> buffer{};

    SECTION("invalid side for SSD") {
        CHECK_FALSE(disc->read_sector(1, 0, 0, buffer));
    }

    SECTION("invalid track") {
        CHECK_FALSE(disc->read_sector(0, 80, 0, buffer));
    }

    SECTION("invalid sector") {
        CHECK_FALSE(disc->read_sector(0, 0, 10, buffer));
    }
}

TEST_CASE("MemoryDiscImage direct data access", "[disc][memory]") {
    auto disc = MemoryDiscImage::create_ssd();

    // Modify data directly
    disc->data()[0] = 0xFF;
    disc->data()[1] = 0xFE;

    // Read back via sector read
    std::array<uint8_t, 256> buffer{};
    REQUIRE(disc->read_sector(0, 0, 0, buffer));

    CHECK(buffer[0] == 0xFF);
    CHECK(buffer[1] == 0xFE);
}
