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

#include <beebium/disc/FileDiscImage.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstring>
#include <atomic>
#include <sstream>
#include <unistd.h>

using namespace beebium;
namespace fs = std::filesystem;

namespace {
// Generate unique filename for parallel test safety
// Uses PID + counter to ensure uniqueness across parallel ctest invocations
std::string unique_temp_filename(const std::string& prefix, const std::string& extension) {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream oss;
    oss << prefix << "_" << getpid() << "_" << counter.fetch_add(1) << extension;
    return oss.str();
}
} // namespace

// Helper to create a minimal valid SSD image in a temporary location
class TempSsdImage {
public:
    explicit TempSsdImage(size_t tracks = 80) : tracks_(tracks) {
        filepath_ = fs::temp_directory_path() / unique_temp_filename("test_disc", ".ssd");
        data_.resize(tracks * 10 * 256, 0);  // Initialize with zeros

        // Write a recognizable pattern: each sector starts with its track/sector number
        for (size_t track = 0; track < tracks; ++track) {
            for (size_t sector = 0; sector < 10; ++sector) {
                size_t offset = (track * 10 + sector) * 256;
                data_[offset] = static_cast<uint8_t>(track);
                data_[offset + 1] = static_cast<uint8_t>(sector);
                // Fill rest with a pattern
                for (size_t i = 2; i < 256; ++i) {
                    data_[offset + i] = static_cast<uint8_t>((track + sector + i) & 0xFF);
                }
            }
        }

        // Write to file
        std::ofstream file(filepath_, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data_.data()), static_cast<std::streamsize>(data_.size()));
    }

    ~TempSsdImage() {
        if (fs::exists(filepath_)) {
            fs::remove(filepath_);
        }
    }

    const fs::path& path() const { return filepath_; }
    const std::vector<uint8_t>& data() const { return data_; }
    size_t tracks() const { return tracks_; }

    // Get expected data for a sector
    std::array<uint8_t, 256> expected_sector(uint8_t track, uint8_t sector) const {
        std::array<uint8_t, 256> result;
        size_t offset = (static_cast<size_t>(track) * 10 + sector) * 256;
        std::memcpy(result.data(), data_.data() + offset, 256);
        return result;
    }

private:
    fs::path filepath_;
    std::vector<uint8_t> data_;
    size_t tracks_;
};

// Phase 2.1: Test loading non-existent file throws exception
TEST_CASE("FileDiscImage load throws for non-existent file", "[disc][image]") {
    REQUIRE_THROWS_AS(
        FileDiscImage::load("/nonexistent/path/to/disc.ssd"),
        std::runtime_error
    );
}

// Phase 2.2: Test loading valid SSD file succeeds
TEST_CASE("FileDiscImage loads valid SSD file", "[disc][image]") {
    TempSsdImage temp_ssd;

    auto image = FileDiscImage::load(temp_ssd.path());

    CHECK(image->name() == temp_ssd.path().filename().string());
}

// Phase 2.3: Test read_sector() returns correct data
TEST_CASE("FileDiscImage read_sector returns correct data", "[disc][image]") {
    TempSsdImage temp_ssd;
    auto image = FileDiscImage::load(temp_ssd.path());

    SECTION("read track 0, sector 0") {
        std::array<uint8_t, 256> buffer{};
        bool success = image->read_sector(0, 0, 0, buffer);
        REQUIRE(success);

        auto expected = temp_ssd.expected_sector(0, 0);
        CHECK(buffer == expected);
    }

    SECTION("read track 0, sector 5") {
        std::array<uint8_t, 256> buffer{};
        bool success = image->read_sector(0, 0, 5, buffer);
        REQUIRE(success);

        auto expected = temp_ssd.expected_sector(0, 5);
        CHECK(buffer == expected);
    }

    SECTION("read track 79, sector 9") {
        std::array<uint8_t, 256> buffer{};
        bool success = image->read_sector(0, 79, 9, buffer);
        REQUIRE(success);

        auto expected = temp_ssd.expected_sector(79, 9);
        CHECK(buffer == expected);
    }
}

// Phase 2.4: Test read_sector() on invalid sector returns false
TEST_CASE("FileDiscImage read_sector returns false for invalid parameters", "[disc][image]") {
    TempSsdImage temp_ssd;
    auto image = FileDiscImage::load(temp_ssd.path());

    std::array<uint8_t, 256> buffer{};

    SECTION("invalid side") {
        bool success = image->read_sector(1, 0, 0, buffer);  // Side 1 doesn't exist for SSD
        CHECK_FALSE(success);
    }

    SECTION("invalid track") {
        bool success = image->read_sector(0, 80, 0, buffer);  // Track 80 doesn't exist
        CHECK_FALSE(success);
    }

    SECTION("invalid sector") {
        bool success = image->read_sector(0, 0, 10, buffer);  // Sector 10 doesn't exist
        CHECK_FALSE(success);
    }
}

// Phase 2.5: Test geometry metadata accessors
TEST_CASE("FileDiscImage provides correct geometry metadata", "[disc][image]") {
    TempSsdImage temp_ssd;
    auto image = FileDiscImage::load(temp_ssd.path());

    CHECK(image->sides() == 1);
    CHECK(image->tracks_per_side() == 80);
    CHECK(image->sectors_per_track() == 10);
    CHECK(image->sector_size() == 256);
}

// Phase 2.6: Test write-protected image rejects writes
TEST_CASE("FileDiscImage write_sector fails when write-protected", "[disc][image]") {
    TempSsdImage temp_ssd;
    auto image = FileDiscImage::load(temp_ssd.path());

    // Set write protection
    image->set_write_protected(true);
    CHECK(image->is_write_protected());

    std::array<uint8_t, 256> buffer{};
    std::fill(buffer.begin(), buffer.end(), 0xAA);

    bool success = image->write_sector(0, 0, 0, buffer);
    CHECK_FALSE(success);
}

// Phase 2.7: Test write_sector() modifies in-memory data
TEST_CASE("FileDiscImage write_sector modifies in-memory data", "[disc][image]") {
    TempSsdImage temp_ssd;
    auto image = FileDiscImage::load(temp_ssd.path());

    // Write new data to sector
    std::array<uint8_t, 256> write_buffer{};
    std::fill(write_buffer.begin(), write_buffer.end(), 0xBB);

    bool write_success = image->write_sector(0, 5, 3, write_buffer);
    REQUIRE(write_success);

    // Read back and verify
    std::array<uint8_t, 256> read_buffer{};
    bool read_success = image->read_sector(0, 5, 3, read_buffer);
    REQUIRE(read_success);
    CHECK(read_buffer == write_buffer);
}

// Phase 2.8: Test flush() persists to file
TEST_CASE("FileDiscImage flush persists changes to file", "[disc][image]") {
    TempSsdImage temp_ssd;
    fs::path filepath = temp_ssd.path();

    // Scope to ensure image is closed after flush
    {
        auto image = FileDiscImage::load(filepath);

        // Write new data
        std::array<uint8_t, 256> write_buffer{};
        std::fill(write_buffer.begin(), write_buffer.end(), 0xCC);

        bool write_success = image->write_sector(0, 10, 5, write_buffer);
        REQUIRE(write_success);

        // Flush to disk
        image->flush();
    }

    // Reload and verify data persisted
    auto image2 = FileDiscImage::load(filepath);

    std::array<uint8_t, 256> read_buffer{};
    bool read_success = image2->read_sector(0, 10, 5, read_buffer);
    REQUIRE(read_success);

    std::array<uint8_t, 256> expected{};
    std::fill(expected.begin(), expected.end(), 0xCC);
    CHECK(read_buffer == expected);
}

// Phase 2.9: Test write_sector persists immediately (write-through)
TEST_CASE("FileDiscImage write_sector persists immediately", "[disc][image]") {
    TempSsdImage temp_ssd;
    fs::path filepath = temp_ssd.path();

    // Write through first image
    {
        auto image = FileDiscImage::load(filepath);

        std::array<uint8_t, 256> write_buffer{};
        std::fill(write_buffer.begin(), write_buffer.end(), 0xDD);
        bool success = image->write_sector(0, 0, 0, write_buffer);
        REQUIRE(success);
        // No flush needed - writes go directly to file
    }

    // Reload and verify data was persisted
    auto image2 = FileDiscImage::load(filepath);

    std::array<uint8_t, 256> read_buffer{};
    image2->read_sector(0, 0, 0, read_buffer);

    // Should match written data
    std::array<uint8_t, 256> expected{};
    std::fill(expected.begin(), expected.end(), 0xDD);
    CHECK(read_buffer == expected);
}
