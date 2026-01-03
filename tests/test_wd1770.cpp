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
