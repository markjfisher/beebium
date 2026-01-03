// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// Integration tests for Model B+ disc controller

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/disc/MemoryDiscImage.hpp>
#include <array>

using namespace beebium;

// WD1770 register addresses on Model B+
constexpr uint16_t DISC_CONTROL = 0xFE80;
constexpr uint16_t WD1770_STATUS = 0xFE84;
constexpr uint16_t WD1770_COMMAND = 0xFE84;
constexpr uint16_t WD1770_TRACK = 0xFE85;
constexpr uint16_t WD1770_SECTOR = 0xFE86;
constexpr uint16_t WD1770_DATA = 0xFE87;

// Disc control register bits
constexpr uint8_t CTRL_DRIVE_SELECT = 0x01;
constexpr uint8_t CTRL_SIDE_SELECT = 0x02;
constexpr uint8_t CTRL_DENSITY = 0x04;
constexpr uint8_t CTRL_MOTOR_ON = 0x10;
constexpr uint8_t CTRL_RESET = 0x20;
constexpr uint8_t CTRL_NMI_ENABLE = 0x40;

// WD1770 status bits
constexpr uint8_t STATUS_BUSY = 0x01;
constexpr uint8_t STATUS_DRQ = 0x02;

// WD1770 commands
constexpr uint8_t CMD_RESTORE = 0x00;
constexpr uint8_t CMD_READ_SECTOR = 0x80;
constexpr uint8_t CMD_FORCE_INT = 0xD0;

namespace {

// Wait for WD1770 to become not busy
void wait_not_busy(ModelBPlus& machine, int max_cycles = 1000000) {
    for (int i = 0; i < max_cycles; ++i) {
        machine.step();
        if ((machine.read(WD1770_STATUS) & STATUS_BUSY) == 0) {
            return;
        }
    }
}

// Read a complete sector from the disc controller
std::vector<uint8_t> read_sector(ModelBPlus& machine, int sector_size = 256) {
    std::vector<uint8_t> data;
    data.reserve(sector_size);

    for (int i = 0; i < sector_size; ++i) {
        // Wait for DRQ
        int timeout = 10000;
        while ((machine.read(WD1770_STATUS) & STATUS_DRQ) == 0 && timeout > 0) {
            machine.step();
            --timeout;
        }
        if (timeout == 0) break;

        // Read byte
        data.push_back(machine.read(WD1770_DATA));
        machine.step();
    }

    // Wait for command completion
    wait_not_busy(machine);

    return data;
}

} // namespace

TEST_CASE("Model B+ disc controller registers are accessible", "[disc][integration]") {
    ModelBPlus machine;

    SECTION("Disc control register at 0xFE80") {
        // Write to disc control
        machine.write(DISC_CONTROL, 0x00);
        CHECK(machine.read(DISC_CONTROL) == 0x00);

        machine.write(DISC_CONTROL, CTRL_MOTOR_ON | CTRL_NMI_ENABLE);
        CHECK(machine.read(DISC_CONTROL) == (CTRL_MOTOR_ON | CTRL_NMI_ENABLE));
    }

    SECTION("WD1770 track register at 0xFE85") {
        // WD1770 track register is read/write
        machine.write(WD1770_TRACK, 0x00);
        CHECK(machine.read(WD1770_TRACK) == 0x00);

        machine.write(WD1770_TRACK, 0x4F);  // Track 79
        CHECK(machine.read(WD1770_TRACK) == 0x4F);
    }

    SECTION("WD1770 sector register at 0xFE86") {
        // WD1770 sector register is read/write
        machine.write(WD1770_SECTOR, 0x00);
        CHECK(machine.read(WD1770_SECTOR) == 0x00);

        machine.write(WD1770_SECTOR, 0x09);  // Sector 9
        CHECK(machine.read(WD1770_SECTOR) == 0x09);
    }

    SECTION("WD1770 data register at 0xFE87") {
        machine.write(WD1770_DATA, 0xAA);
        CHECK(machine.read(WD1770_DATA) == 0xAA);
    }
}

TEST_CASE("Model B+ disc control register controls WD1770", "[disc][integration]") {
    ModelBPlus machine;

    SECTION("Reset bit resets WD1770") {
        // Set some state
        machine.write(WD1770_TRACK, 0x10);
        machine.write(WD1770_SECTOR, 0x05);

        // Issue reset via disc control
        machine.write(DISC_CONTROL, CTRL_RESET);
        machine.write(DISC_CONTROL, 0x00);  // Clear reset

        // WD1770 should be reset (sector defaults to 1)
        CHECK(machine.read(WD1770_SECTOR) == 0x01);
    }

    SECTION("Drive select is written correctly") {
        // Select drive 0
        machine.write(DISC_CONTROL, 0x00);
        CHECK(machine.memory().disc_controller.selected_drive() == 0);

        // Select drive 1
        machine.write(DISC_CONTROL, CTRL_DRIVE_SELECT);
        CHECK(machine.memory().disc_controller.selected_drive() == 1);
    }

    SECTION("Side select is written correctly") {
        // Select side 0
        machine.write(DISC_CONTROL, 0x00);
        CHECK(machine.memory().disc_controller.selected_side() == 0);

        // Select side 1
        machine.write(DISC_CONTROL, CTRL_SIDE_SELECT);
        CHECK(machine.memory().disc_controller.selected_side() == 1);
    }
}

TEST_CASE("Model B+ can read sector from inserted disc", "[disc][integration]") {
    ModelBPlus machine;

    // Create a disc with known content
    auto disc = MemoryDiscImage::create_ssd();

    // Write test pattern to sector 0 of track 0
    std::array<uint8_t, 256> test_pattern{};
    for (size_t i = 0; i < 256; ++i) {
        test_pattern[i] = static_cast<uint8_t>(i);
    }
    REQUIRE(disc->write_sector(0, 0, 0, test_pattern));

    // Insert disc into drive 0
    machine.memory().disc_drive_0.insert(std::move(disc));

    // Configure disc control: drive 0, side 0, motor on, NMI enabled
    machine.write(DISC_CONTROL, CTRL_MOTOR_ON | CTRL_NMI_ENABLE);

    // Seek to track 0
    machine.write(WD1770_COMMAND, CMD_RESTORE);
    wait_not_busy(machine);

    // Set sector to 0
    machine.write(WD1770_SECTOR, 0);

    // Issue read sector command
    machine.write(WD1770_COMMAND, CMD_READ_SECTOR);

    // Read sector data
    auto data = read_sector(machine);

    // Verify data matches test pattern
    REQUIRE(data.size() == 256);
    for (size_t i = 0; i < 256; ++i) {
        CHECK(data[i] == static_cast<uint8_t>(i));
    }
}

TEST_CASE("Model B+ NMI is gated by disc control register", "[disc][integration][nmi]") {
    ModelBPlus machine;

    // Insert a disc
    auto disc = MemoryDiscImage::create_ssd();
    machine.memory().disc_drive_0.insert(std::move(disc));

    SECTION("NMI disabled - poll_nmi returns 0 even when INTRQ set") {
        // Configure: motor on but NMI disabled
        machine.write(DISC_CONTROL, CTRL_MOTOR_ON);

        // Issue Force Interrupt to set INTRQ
        machine.write(WD1770_COMMAND, CMD_FORCE_INT);

        // WD1770 should have INTRQ set
        CHECK(machine.memory().disc_controller.intrq());

        // But poll_nmi should return 0 because NMI is disabled
        CHECK(machine.memory().poll_nmi() == 0);
    }

    SECTION("NMI enabled - poll_nmi returns 1 when INTRQ set") {
        // Configure: motor on and NMI enabled
        machine.write(DISC_CONTROL, CTRL_MOTOR_ON | CTRL_NMI_ENABLE);

        // Issue Force Interrupt to set INTRQ
        machine.write(WD1770_COMMAND, CMD_FORCE_INT);

        // WD1770 should have INTRQ set
        CHECK(machine.memory().disc_controller.intrq());

        // poll_nmi should return 1 because NMI is enabled
        CHECK(machine.memory().poll_nmi() == 0x01);
    }

    SECTION("NMI cleared when status read") {
        // Configure: motor on and NMI enabled
        machine.write(DISC_CONTROL, CTRL_MOTOR_ON | CTRL_NMI_ENABLE);

        // Issue Force Interrupt to set INTRQ
        machine.write(WD1770_COMMAND, CMD_FORCE_INT);
        CHECK(machine.memory().poll_nmi() == 0x01);

        // Reading status clears INTRQ
        machine.read(WD1770_STATUS);
        CHECK(machine.memory().poll_nmi() == 0x00);
    }
}

TEST_CASE("Model B+ disc controller survives reset", "[disc][integration]") {
    ModelBPlus machine;

    // Insert disc into drive
    auto disc = MemoryDiscImage::create_ssd();
    machine.memory().disc_drive_0.insert(std::move(disc));

    // Configure disc system
    machine.write(DISC_CONTROL, CTRL_MOTOR_ON | CTRL_DRIVE_SELECT);
    machine.write(WD1770_TRACK, 0x10);

    // Machine reset
    machine.reset();

    // Disc should still be in drive (hardware doesn't eject on reset)
    CHECK(machine.memory().disc_drive_0.has_disc());

    // Disc control register should be reset
    CHECK(machine.read(DISC_CONTROL) == 0x00);

    // WD1770 sector register should be reset to default (1)
    CHECK(machine.read(WD1770_SECTOR) == 0x01);
}
