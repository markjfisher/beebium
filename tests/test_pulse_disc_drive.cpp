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

#include <beebium/disc/PulseDiscDrive.hpp>
#include <beebium/disc/formats/SsdFormatHandler.hpp>
#include <beebium/disc/TrackBuilder.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// Helper: load a small SSD image into a Disc.
static std::unique_ptr<Disc> make_test_disc() {
    SsdFormatHandler handler;
    std::vector<uint8_t> data(80 * 10 * 256, 0);
    // Put known data in track 0 sector 0
    for (int i = 0; i < 256; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }
    auto result = handler.load(data, "/tmp/test.ssd");
    return std::move(result.disc);
}

// =============================================================================
// Construction and State
// =============================================================================

TEST_CASE("PulseDiscDrive default construction is empty", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    CHECK(drive.state() == DriveState::Empty);
    CHECK_FALSE(drive.has_disc());
    CHECK(drive.current_track() == 0);
    CHECK_FALSE(drive.motor_on());
}

// =============================================================================
// Disc Insertion / Ejection
// =============================================================================

TEST_CASE("PulseDiscDrive insert transitions to Loaded", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    drive.insert(make_test_disc());

    CHECK(drive.state() == DriveState::Loaded);
    CHECK(drive.has_disc());
    CHECK(drive.disc() != nullptr);
}

TEST_CASE("PulseDiscDrive eject_immediate returns disc", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    drive.insert(make_test_disc());

    auto disc = drive.eject_immediate();
    CHECK(disc != nullptr);
    CHECK(drive.state() == DriveState::Empty);
    CHECK_FALSE(drive.has_disc());
}

// =============================================================================
// Head Positioning
// =============================================================================

TEST_CASE("PulseDiscDrive step_in and step_out", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    CHECK(drive.current_track() == 0);
    CHECK(drive.at_track_0());

    drive.step_in();
    CHECK(drive.current_track() == 1);
    CHECK_FALSE(drive.at_track_0());

    drive.step_out();
    CHECK(drive.current_track() == 0);
    CHECK(drive.at_track_0());
}

TEST_CASE("PulseDiscDrive step_out at track 0 stays at 0", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    drive.step_out();
    CHECK(drive.current_track() == 0);
}

TEST_CASE("PulseDiscDrive step_in clamped at MAX_TRACK", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    for (int i = 0; i < 100; ++i) drive.step_in();
    CHECK(drive.current_track() == PulseDiscDrive::MAX_TRACK);
}

TEST_CASE("PulseDiscDrive seek", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    drive.seek(40);
    CHECK(drive.current_track() == 40);

    drive.seek(0);
    CHECK(drive.at_track_0());
}

// =============================================================================
// Motor Control
// =============================================================================

TEST_CASE("PulseDiscDrive motor control", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    CHECK_FALSE(drive.motor_on());

    drive.spin_up();
    CHECK(drive.motor_on());

    drive.spin_down();
    CHECK_FALSE(drive.motor_on());
}

// =============================================================================
// Pulse-Level Access
// =============================================================================

TEST_CASE("PulseDiscDrive read_pulses returns data from disc", "[disc][pulsedrive][pulses]") {
    PulseDiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    // Track 0 should have non-zero pulse data (FM-encoded DFS sectors)
    uint32_t pulses = drive.read_pulses();
    // After SSD load, first pulse word should be FM-encoded 0xFF (gap bytes)
    CHECK(pulses != 0);
}

TEST_CASE("PulseDiscDrive read_pulses returns quasi-random with no disc", "[disc][pulsedrive][pulses]") {
    PulseDiscDrive drive;
    drive.set_side(0);

    // With no disc, should still return something (quasi-random pulses)
    uint32_t p1 = drive.read_pulses();
    // Just verify it doesn't crash
    (void)p1;
}

TEST_CASE("PulseDiscDrive advance_head increments position", "[disc][pulsedrive][pulses]") {
    PulseDiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    CHECK(drive.head_position() == 0);

    bool index = drive.advance_head();
    CHECK(drive.head_position() == 1);
    CHECK_FALSE(index);  // Not at index yet
}

TEST_CASE("PulseDiscDrive advance_head wraps at track end", "[disc][pulsedrive][pulses]") {
    PulseDiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    // Advance to just before the end
    uint32_t track_len = drive.track_length();
    REQUIRE(track_len > 0);

    for (uint32_t i = 0; i < track_len - 1; ++i) {
        bool index = drive.advance_head();
        CHECK_FALSE(index);
    }

    // This advance should wrap and trigger index
    bool index = drive.advance_head();
    CHECK(index);
    CHECK(drive.head_position() == 0);
}

TEST_CASE("PulseDiscDrive advance_head_half increments by half-word", "[disc][pulsedrive][pulses]") {
    PulseDiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    CHECK(drive.head_position() == 0);
    CHECK(drive.pulse_sub_position() == 0);

    bool index = drive.advance_head_half();
    CHECK_FALSE(index);
    CHECK(drive.head_position() == 0);
    CHECK(drive.pulse_sub_position() == 16);

    index = drive.advance_head_half();
    CHECK_FALSE(index);
    CHECK(drive.head_position() == 1);
    CHECK(drive.pulse_sub_position() == 0);
}

// =============================================================================
// Write Pulses
// =============================================================================

TEST_CASE("PulseDiscDrive write_pulses modifies track", "[disc][pulsedrive][pulses]") {
    PulseDiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    drive.write_pulses(0xDEADBEEF);
    CHECK(drive.read_pulses() == 0xDEADBEEF);
}

TEST_CASE("PulseDiscDrive write marks disc dirty", "[disc][pulsedrive][pulses]") {
    PulseDiscDrive drive;
    auto disc = make_test_disc();
    auto* disc_ptr = disc.get();
    drive.insert(std::move(disc));
    drive.set_side(0);

    CHECK_FALSE(disc_ptr->has_dirty_tracks());
    drive.write_pulses(0x12345678);
    CHECK(disc_ptr->has_dirty_tracks());
}

// =============================================================================
// Side Selection
// =============================================================================

TEST_CASE("PulseDiscDrive side selection affects which surface is read", "[disc][pulsedrive][side]") {
    PulseDiscDrive drive;

    SsdFormatHandler handler;
    // Make a DSD (double-sided) image with distinct data on each side
    std::vector<uint8_t> data(80 * 2 * 10 * 256, 0);
    // Fill side 0 track 0 sector 0 with 0xAA, side 1 track 0 sector 0 with 0x55
    for (int i = 0; i < 256; ++i) {
        data[i] = 0xAA;              // Side 0, track 0, sector 0
        data[10 * 256 + i] = 0x55;   // Side 1, track 0, sector 0
    }
    auto result = handler.load(data, "/tmp/test.dsd");
    REQUIRE(result.success());

    drive.insert(std::move(result.disc));

    // Advance past GAP1 + sync (22 positions) + ID field (7) + GAP2 (17) + data mark (1) = 47
    // to reach sector 0 data where the sides differ
    auto advance_to = [&](uint32_t pos) {
        drive.seek(0);
        // Reset head position by ejecting and reinserting... simpler: just advance
        for (uint32_t i = 0; i < pos; ++i) {
            drive.advance_head();
        }
    };

    // The sector data starts at GAP1(16) + sync(6) + IDmark(1) + ID(4) + CRC(2) + GAP2_FF(11)
    // + sync(6) + datamark(1) = 47 positions from start
    uint32_t data_start = 16 + 6 + 1 + 4 + 2 + 11 + 6 + 1;

    drive.set_side(0);
    advance_to(data_start);
    uint32_t p0 = drive.read_pulses();

    // Reset position for side 1
    // Eject and reinsert to reset head position (simpler than tracking)
    auto disc = drive.eject_immediate();
    drive.insert(std::move(disc));

    drive.set_side(1);
    advance_to(data_start);
    uint32_t p1 = drive.read_pulses();

    // The sector data is different (0xAA vs 0x55), so pulses should differ
    CHECK(p0 != p1);
}

// =============================================================================
// Write Protection
// =============================================================================

TEST_CASE("PulseDiscDrive is_write_protected delegates to disc", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    auto disc = make_test_disc();
    disc->set_write_protected(true);
    drive.insert(std::move(disc));

    CHECK(drive.is_write_protected());
}

TEST_CASE("PulseDiscDrive is_write_protected false with no disc", "[disc][pulsedrive]") {
    PulseDiscDrive drive;
    CHECK_FALSE(drive.is_write_protected());
}
