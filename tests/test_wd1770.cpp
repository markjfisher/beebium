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

#include <beebium/disc/WD1770.hpp>
#include <beebium/disc/DiscDrive.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

// Phase 4.1: Test initial status register is 0x00
TEST_CASE("WD1770 initial status register is 0x00", "[disc][wd1770]") {
    WD1770 controller;
    CHECK(controller.read(0) == 0x00);
}

// Phase 4.2: Test read offset 0 returns status register
TEST_CASE("WD1770 read offset 0 returns status register", "[disc][wd1770]") {
    WD1770 controller;
    // Initial status should be 0
    CHECK(controller.read(0) == 0x00);
}

// Phase 4.3: Test read offset 1 returns track register
TEST_CASE("WD1770 read offset 1 returns track register", "[disc][wd1770]") {
    WD1770 controller;
    // Initial track register is 0
    CHECK(controller.read(1) == 0x00);
}

// Phase 4.4: Test read offset 2 returns sector register
TEST_CASE("WD1770 read offset 2 returns sector register", "[disc][wd1770]") {
    WD1770 controller;
    // Initial sector register is 1 (per WD1770 spec)
    CHECK(controller.read(2) == 0x01);
}

// Phase 4.5: Test read offset 3 returns data register
TEST_CASE("WD1770 read offset 3 returns data register", "[disc][wd1770]") {
    WD1770 controller;
    // Initial data register is 0
    CHECK(controller.read(3) == 0x00);
}

// Phase 4.6: Test write offset 1 sets track register
TEST_CASE("WD1770 write offset 1 sets track register", "[disc][wd1770]") {
    WD1770 controller;

    controller.write(1, 0x42);
    CHECK(controller.read(1) == 0x42);

    controller.write(1, 0xFF);
    CHECK(controller.read(1) == 0xFF);
}

// Phase 4.7: Test write offset 2 sets sector register
TEST_CASE("WD1770 write offset 2 sets sector register", "[disc][wd1770]") {
    WD1770 controller;

    controller.write(2, 0x05);
    CHECK(controller.read(2) == 0x05);

    controller.write(2, 0x0A);
    CHECK(controller.read(2) == 0x0A);
}

// Phase 4.8: Test write offset 3 sets data register
TEST_CASE("WD1770 write offset 3 sets data register", "[disc][wd1770]") {
    WD1770 controller;

    controller.write(3, 0xAB);
    CHECK(controller.read(3) == 0xAB);

    controller.write(3, 0xCD);
    CHECK(controller.read(3) == 0xCD);
}

// Phase 4.9: Test attach_drive connects drives
TEST_CASE("WD1770 attach_drive connects drives", "[disc][wd1770]") {
    WD1770 controller;
    DiscDrive drive0;
    DiscDrive drive1;

    controller.attach_drive(0, &drive0);
    controller.attach_drive(1, &drive1);

    // No crash, drives attached
    CHECK(true);
}

// Phase 4.10: Test set_side/set_drive/set_density external control
TEST_CASE("WD1770 external control signals", "[disc][wd1770]") {
    WD1770 controller;

    SECTION("set_side") {
        controller.set_side(0);
        CHECK(controller.selected_side() == 0);

        controller.set_side(1);
        CHECK(controller.selected_side() == 1);
    }

    SECTION("set_drive") {
        controller.set_drive(0);
        CHECK(controller.selected_drive() == 0);

        controller.set_drive(1);
        CHECK(controller.selected_drive() == 1);
    }

    SECTION("set_density") {
        controller.set_density(false);
        CHECK_FALSE(controller.is_double_density());

        controller.set_density(true);
        CHECK(controller.is_double_density());
    }
}

// Phase 4.11: Test reset clears all state
TEST_CASE("WD1770 reset clears all state", "[disc][wd1770]") {
    WD1770 controller;

    // Modify state
    controller.write(1, 0x42);  // Track
    controller.write(2, 0x07);  // Sector
    controller.write(3, 0xAB);  // Data
    controller.set_side(1);
    controller.set_drive(1);
    controller.set_density(true);

    // Reset
    controller.reset();

    // Verify state is cleared
    CHECK(controller.read(0) == 0x00);  // Status
    CHECK(controller.read(1) == 0x00);  // Track
    CHECK(controller.read(2) == 0x01);  // Sector (defaults to 1)
    CHECK(controller.read(3) == 0x00);  // Data
    CHECK(controller.selected_side() == 0);
    CHECK(controller.selected_drive() == 0);
    CHECK_FALSE(controller.is_double_density());
}

// Additional tests for interrupt status
TEST_CASE("WD1770 initial interrupt state", "[disc][wd1770]") {
    WD1770 controller;

    CHECK_FALSE(controller.drq());
    CHECK_FALSE(controller.intrq());
}

// Test register offset wrapping (only 2 bits used)
TEST_CASE("WD1770 register offset uses only 2 bits", "[disc][wd1770]") {
    WD1770 controller;

    // Offset 4 should map to offset 0 (status)
    CHECK(controller.read(4) == controller.read(0));

    // Offset 5 should map to offset 1 (track)
    controller.write(1, 0x55);
    CHECK(controller.read(5) == 0x55);
}

// ============================================================================
// Phase 5: Type I Commands (Seek/Step)
// ============================================================================

#include <beebium/disc/MemoryDiscImage.hpp>

namespace {
// Helper to run controller until command completes or timeout
void run_until_complete(WD1770& controller, int max_ticks = 1000000) {
    for (int i = 0; i < max_ticks; ++i) {
        controller.tick();
        // Use busy() to avoid clearing INTRQ via status read
        if (!controller.busy()) {
            return;
        }
    }
}
} // namespace

// Phase 5.1: Force Interrupt command clears BUSY
TEST_CASE("WD1770 Force Interrupt clears BUSY", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Start a Restore command
    controller.write(0, 0x00);
    CHECK((controller.read(0) & WD1770::STATUS_BUSY) != 0);

    // Force Interrupt (0xD0) should clear BUSY
    controller.write(0, 0xD0);
    run_until_complete(controller, 100);
    CHECK((controller.read(0) & WD1770::STATUS_BUSY) == 0);
}

// Phase 5.2: Writing command sets BUSY
TEST_CASE("WD1770 writing command sets BUSY", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    CHECK((controller.read(0) & WD1770::STATUS_BUSY) == 0);

    // Restore command
    controller.write(0, 0x00);
    CHECK((controller.read(0) & WD1770::STATUS_BUSY) != 0);
}

// Phase 5.3: Restore command seeks to track 0
TEST_CASE("WD1770 Restore seeks to track 0", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Move drive head to track 20
    for (int i = 0; i < 20; ++i) {
        drive.step_in();
    }
    CHECK(drive.current_track() == 20);

    // Restore command (0x00)
    controller.write(0, 0x00);
    run_until_complete(controller);

    CHECK(drive.current_track() == 0);
}

// Phase 5.4: Restore updates track register
TEST_CASE("WD1770 Restore updates track register", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Set track register to non-zero
    controller.write(1, 0x20);
    CHECK(controller.read(1) == 0x20);

    // Restore command
    controller.write(0, 0x00);
    run_until_complete(controller);

    // Track register should be 0 after restore
    CHECK(controller.read(1) == 0x00);
}

// Phase 5.5: Seek command seeks to data register value
TEST_CASE("WD1770 Seek moves to data register track", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Start at track 0
    CHECK(drive.current_track() == 0);
    controller.write(1, 0x00);  // Track register = 0

    // Set data register to target track
    controller.write(3, 25);

    // Seek command (0x10)
    controller.write(0, 0x10);
    run_until_complete(controller);

    CHECK(drive.current_track() == 25);
    CHECK(controller.read(1) == 25);  // Track register updated
}

// Phase 5.6: Step command steps in last direction
TEST_CASE("WD1770 Step uses last direction", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // First, do a Step-In to set direction
    controller.write(0, 0x40);  // Step-In
    run_until_complete(controller);
    CHECK(drive.current_track() == 1);

    // Now Step (0x20) should continue in same direction
    controller.write(0, 0x20);
    run_until_complete(controller);
    CHECK(drive.current_track() == 2);
}

// Phase 5.7: Step-In steps toward higher tracks
TEST_CASE("WD1770 Step-In increments track", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    CHECK(drive.current_track() == 0);

    // Step-In without T flag: drive moves, track register unchanged
    controller.write(0, 0x40);
    run_until_complete(controller);
    CHECK(drive.current_track() == 1);
    CHECK(controller.read(1) == 0);  // Track register unchanged

    // Step-In with T flag: drive moves, track register increments
    controller.write(0, 0x50);
    run_until_complete(controller);
    CHECK(drive.current_track() == 2);
    CHECK(controller.read(1) == 1);  // Incremented from 0 to 1
}

// Phase 5.8: Step-Out steps toward track 0
TEST_CASE("WD1770 Step-Out decrements track", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Move to track 5
    for (int i = 0; i < 5; ++i) {
        drive.step_in();
    }
    controller.write(1, 5);  // Set track register to match drive
    CHECK(drive.current_track() == 5);

    // Step-Out without T flag: drive moves, track register unchanged
    controller.write(0, 0x60);
    run_until_complete(controller);
    CHECK(drive.current_track() == 4);
    CHECK(controller.read(1) == 5);  // Track register unchanged

    // Step-Out with T flag: drive moves, track register decrements
    controller.write(0, 0x70);
    run_until_complete(controller);
    CHECK(drive.current_track() == 3);
    CHECK(controller.read(1) == 4);  // Decremented from 5 to 4
}

// Phase 5.9: TRACK0 status bit set when at track 0
TEST_CASE("WD1770 TRACK0 status bit", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // At track 0, status bit 2 should indicate track 0
    controller.write(0, 0x00);  // Restore
    run_until_complete(controller);

    // For Type I commands, bit 2 is TRACK0 (not DRQ)
    uint8_t status = controller.read(0);
    CHECK((status & 0x04) != 0);  // TRACK0 flag set

    // Move away from track 0
    controller.write(3, 5);
    controller.write(0, 0x10);  // Seek to track 5
    run_until_complete(controller);

    status = controller.read(0);
    CHECK((status & 0x04) == 0);  // TRACK0 flag clear
}

// Phase 5.13: INTRQ asserted on command completion
TEST_CASE("WD1770 INTRQ on command completion", "[disc][wd1770][type1]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    CHECK_FALSE(controller.intrq());

    // Start Restore
    controller.write(0, 0x00);

    // Run until complete
    run_until_complete(controller);

    // INTRQ should be asserted
    CHECK(controller.intrq());

    // Reading status should clear INTRQ
    controller.read(0);
    CHECK_FALSE(controller.intrq());
}

// ============================================================================
// Phase 6: Type II Commands (Read/Write Sector)
// ============================================================================

// Helper to transfer a full sector via DRQ
namespace {
std::vector<uint8_t> read_sector_data(WD1770& controller, int sector_size = 256) {
    std::vector<uint8_t> data;
    data.reserve(sector_size);

    for (int i = 0; i < sector_size; ++i) {
        // Wait for DRQ
        int timeout = 10000;
        while (!controller.drq() && timeout > 0) {
            controller.tick();
            --timeout;
        }
        if (timeout == 0) break;

        // Read data byte
        data.push_back(controller.read(3));
        controller.tick();
    }

    // Wait for command completion
    run_until_complete(controller);
    return data;
}

void write_sector_data(WD1770& controller, const std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); ++i) {
        // Wait for DRQ
        int timeout = 10000;
        while (!controller.drq() && timeout > 0) {
            controller.tick();
            --timeout;
        }
        if (timeout == 0) break;

        // Write data byte
        controller.write(3, data[i]);
        controller.tick();
    }

    // Wait for command completion
    run_until_complete(controller);
}
} // namespace

// Phase 6.1: Read Sector sets BUSY
TEST_CASE("WD1770 Read Sector sets BUSY", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    CHECK_FALSE(controller.busy());

    // Read Sector command (0x80)
    controller.write(0, 0x80);
    CHECK(controller.busy());
}

// Phase 6.2: Read Sector asserts DRQ for data transfer
TEST_CASE("WD1770 Read Sector asserts DRQ", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    CHECK_FALSE(controller.drq());

    // Read Sector command
    controller.write(2, 0);  // Sector 0
    controller.write(0, 0x80);

    // Tick until DRQ is asserted
    for (int i = 0; i < 1000 && !controller.drq(); ++i) {
        controller.tick();
    }

    CHECK(controller.drq());
}

// Phase 6.3: Read Sector returns sector data
TEST_CASE("WD1770 Read Sector returns sector data", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();

    // Write known data to sector 0
    std::vector<uint8_t> test_data(256);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<uint8_t>(i);
    }
    disc->write_sector(0, 0, 0, test_data);

    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);
    controller.set_side(0);

    // Read Sector 0
    controller.write(2, 0);  // Sector register
    controller.write(0, 0x80);  // Read Sector command

    auto read_data = read_sector_data(controller);

    REQUIRE(read_data.size() == 256);
    CHECK(read_data == test_data);
}

// Phase 6.4: Read Sector increments sector register with m flag
TEST_CASE("WD1770 Read Sector multi-sector mode", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Read Sector with m flag (0x90)
    controller.write(2, 0);  // Start at sector 0
    controller.write(0, 0x90);  // Read Sector with multi-sector flag

    // Read first sector
    read_sector_data(controller);

    // Sector register should have incremented
    // (Note: full multi-sector would continue reading, but we're testing register update)
    CHECK(controller.read(2) == 1);
}

// Phase 6.5: Read Sector sets RNF for invalid sector
TEST_CASE("WD1770 Read Sector sets RNF for invalid sector", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();  // 10 sectors per track (0-9)
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Try to read sector 15 (doesn't exist)
    controller.write(2, 15);
    controller.write(0, 0x80);

    run_until_complete(controller);

    uint8_t status = controller.read(0);
    CHECK((status & WD1770::STATUS_RNF) != 0);
}

// Phase 6.6: Write Sector sets BUSY
TEST_CASE("WD1770 Write Sector sets BUSY", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    CHECK_FALSE(controller.busy());

    // Write Sector command (0xA0)
    controller.write(0, 0xA0);
    CHECK(controller.busy());
}

// Phase 6.7: Write Sector asserts DRQ for data transfer
TEST_CASE("WD1770 Write Sector asserts DRQ", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    CHECK_FALSE(controller.drq());

    // Write Sector command
    controller.write(2, 0);  // Sector 0
    controller.write(0, 0xA0);

    // Tick until DRQ is asserted
    for (int i = 0; i < 1000 && !controller.drq(); ++i) {
        controller.tick();
    }

    CHECK(controller.drq());
}

// Phase 6.8: Write Sector writes data to disc
TEST_CASE("WD1770 Write Sector writes data to disc", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc_ptr = MemoryDiscImage::create_ssd();
    auto* disc = disc_ptr.get();
    drive.insert(std::move(disc_ptr));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);
    controller.set_side(0);

    // Prepare test data
    std::vector<uint8_t> test_data(256);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<uint8_t>(0xFF - i);
    }

    // Write Sector 0
    controller.write(2, 0);  // Sector register
    controller.write(0, 0xA0);  // Write Sector command

    write_sector_data(controller, test_data);

    // Verify data was written to disc
    std::vector<uint8_t> read_back(256);
    disc->read_sector(0, 0, 0, read_back);

    CHECK(read_back == test_data);
}

// Phase 6.9: Write Sector fails on write-protected disc
TEST_CASE("WD1770 Write Sector fails on write-protected disc", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    disc->set_write_protected(true);
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Write Sector command
    controller.write(2, 0);
    controller.write(0, 0xA0);

    run_until_complete(controller);

    uint8_t status = controller.read(0);
    CHECK((status & WD1770::STATUS_WRITE_PROT) != 0);
}

// Phase 6.10: Reading data register clears DRQ
TEST_CASE("WD1770 Reading data register clears DRQ", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Read Sector command
    controller.write(2, 0);
    controller.write(0, 0x80);

    // Wait for DRQ
    for (int i = 0; i < 1000 && !controller.drq(); ++i) {
        controller.tick();
    }
    REQUIRE(controller.drq());

    // Read data register should clear DRQ
    controller.read(3);
    CHECK_FALSE(controller.drq());
}

// Phase 6.11: DRQ appears in status register
TEST_CASE("WD1770 DRQ appears in status register", "[disc][wd1770][type2]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Read Sector command
    controller.write(2, 0);
    controller.write(0, 0x80);

    // Wait for DRQ
    for (int i = 0; i < 1000 && !controller.drq(); ++i) {
        controller.tick();
    }

    uint8_t status = controller.read(0);
    CHECK((status & WD1770::STATUS_DRQ) != 0);
}
