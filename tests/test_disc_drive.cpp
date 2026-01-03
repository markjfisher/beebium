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

#include <beebium/disc/DiscDrive.hpp>
#include <beebium/disc/FileDiscImage.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <array>

using namespace beebium;
namespace fs = std::filesystem;

// Helper to create a temporary SSD image
class TempSsdForDrive {
public:
    TempSsdForDrive() {
        filepath_ = fs::temp_directory_path() / ("drive_test_" + std::to_string(rand()) + ".ssd");
        std::vector<uint8_t> data(80 * 10 * 256, 0);
        // Write track/sector markers
        for (size_t track = 0; track < 80; ++track) {
            for (size_t sector = 0; sector < 10; ++sector) {
                size_t offset = (track * 10 + sector) * 256;
                data[offset] = static_cast<uint8_t>(track);
                data[offset + 1] = static_cast<uint8_t>(sector);
            }
        }
        std::ofstream file(filepath_, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    ~TempSsdForDrive() {
        if (fs::exists(filepath_)) {
            fs::remove(filepath_);
        }
    }

    std::unique_ptr<DiscImage> load() {
        auto result = FileDiscImage::load(filepath_);
        if (result) {
            return std::move(*result);
        }
        return nullptr;
    }

private:
    fs::path filepath_;
};

// Phase 3.1: Test empty drive has no disc
TEST_CASE("DiscDrive empty drive has no disc", "[disc][drive]") {
    DiscDrive drive;
    CHECK_FALSE(drive.has_disc());
}

// Phase 3.2: Test insert() makes disc available
TEST_CASE("DiscDrive insert makes disc available", "[disc][drive]") {
    TempSsdForDrive temp;
    DiscDrive drive;

    drive.insert(temp.load());

    CHECK(drive.has_disc());
}

// Phase 3.3: Test eject() returns disc and clears state
TEST_CASE("DiscDrive eject returns disc and clears state", "[disc][drive]") {
    TempSsdForDrive temp;
    DiscDrive drive;
    drive.insert(temp.load());

    auto ejected = drive.eject();

    CHECK(ejected != nullptr);
    CHECK_FALSE(drive.has_disc());
}

// Phase 3.4: Test initial track is 0
TEST_CASE("DiscDrive initial track is 0", "[disc][drive]") {
    DiscDrive drive;
    CHECK(drive.current_track() == 0);
}

// Phase 3.5: Test step_in() increments track
TEST_CASE("DiscDrive step_in increments track", "[disc][drive]") {
    DiscDrive drive;
    CHECK(drive.current_track() == 0);

    drive.step_in();
    CHECK(drive.current_track() == 1);

    drive.step_in();
    CHECK(drive.current_track() == 2);
}

// Phase 3.6: Test step_out() decrements track
TEST_CASE("DiscDrive step_out decrements track", "[disc][drive]") {
    DiscDrive drive;
    drive.step_in();
    drive.step_in();
    CHECK(drive.current_track() == 2);

    drive.step_out();
    CHECK(drive.current_track() == 1);
}

// Phase 3.7: Test step_out() at track 0 stays at 0
TEST_CASE("DiscDrive step_out at track 0 stays at 0", "[disc][drive]") {
    DiscDrive drive;
    CHECK(drive.current_track() == 0);

    drive.step_out();
    CHECK(drive.current_track() == 0);

    drive.step_out();
    CHECK(drive.current_track() == 0);
}

// Phase 3.8: Test step_in() at track 79 stays at 79
TEST_CASE("DiscDrive step_in at track 79 stays at 79", "[disc][drive]") {
    DiscDrive drive;

    // Step to track 79
    for (int i = 0; i < 79; ++i) {
        drive.step_in();
    }
    CHECK(drive.current_track() == 79);

    // Try to step beyond
    drive.step_in();
    CHECK(drive.current_track() == 79);
}

// Phase 3.9: Test at_track_0() flag
TEST_CASE("DiscDrive at_track_0 flag", "[disc][drive]") {
    DiscDrive drive;

    CHECK(drive.at_track_0());

    drive.step_in();
    CHECK_FALSE(drive.at_track_0());

    drive.step_out();
    CHECK(drive.at_track_0());
}

// Phase 3.10: Test seek() moves to specific track
TEST_CASE("DiscDrive seek moves to specific track", "[disc][drive]") {
    DiscDrive drive;

    drive.seek(50);
    CHECK(drive.current_track() == 50);

    drive.seek(10);
    CHECK(drive.current_track() == 10);

    drive.seek(0);
    CHECK(drive.current_track() == 0);
    CHECK(drive.at_track_0());
}

// Phase 3.11: Test motor on/off state
TEST_CASE("DiscDrive motor control", "[disc][drive]") {
    DiscDrive drive;

    CHECK_FALSE(drive.motor_on());

    drive.set_motor(true);
    CHECK(drive.motor_on());

    drive.set_motor(false);
    CHECK_FALSE(drive.motor_on());
}

// Phase 3.12: Test read_sector() delegates to DiscImage
TEST_CASE("DiscDrive read_sector delegates to DiscImage", "[disc][drive]") {
    TempSsdForDrive temp;
    DiscDrive drive;
    drive.insert(temp.load());

    std::array<uint8_t, 256> buffer{};
    bool success = drive.read_sector(0, 5, buffer);

    REQUIRE(success);
    CHECK(buffer[0] == 0);  // Track 0
    CHECK(buffer[1] == 5);  // Sector 5
}

// Phase 3.13: Test write_sector() delegates to DiscImage
TEST_CASE("DiscDrive write_sector delegates to DiscImage", "[disc][drive]") {
    TempSsdForDrive temp;
    DiscDrive drive;
    drive.insert(temp.load());

    std::array<uint8_t, 256> write_buffer{};
    std::fill(write_buffer.begin(), write_buffer.end(), 0xEE);

    bool write_success = drive.write_sector(0, 3, write_buffer);
    REQUIRE(write_success);

    // Verify by reading back
    std::array<uint8_t, 256> read_buffer{};
    bool read_success = drive.read_sector(0, 3, read_buffer);
    REQUIRE(read_success);
    CHECK(read_buffer == write_buffer);
}

// Phase 3.14: Test is_write_protected() delegates to DiscImage
TEST_CASE("DiscDrive is_write_protected delegates to DiscImage", "[disc][drive]") {
    TempSsdForDrive temp;
    DiscDrive drive;

    SECTION("empty drive is not write protected") {
        CHECK_FALSE(drive.is_write_protected());
    }

    SECTION("delegates to disc image") {
        auto disc = temp.load();
        disc->set_write_protected(true);
        drive.insert(std::move(disc));
        CHECK(drive.is_write_protected());
    }
}

// Additional: Test read/write without disc fails
TEST_CASE("DiscDrive read/write without disc fails", "[disc][drive]") {
    DiscDrive drive;
    std::array<uint8_t, 256> buffer{};

    CHECK_FALSE(drive.read_sector(0, 0, buffer));
    CHECK_FALSE(drive.write_sector(0, 0, buffer));
}
