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

#include <catch2/catch_test_macros.hpp>
#include <beebium/devices/ConfigurableSlot.hpp>
#include <array>

using namespace beebium;

TEST_CASE("ConfigurableSlot default construction", "[configurable_slot][init]") {
    ConfigurableSlot slot;

    SECTION("Default type is Empty") {
        REQUIRE(slot.type() == SlotType::Empty);
        REQUIRE(slot.is_empty());
        REQUIRE_FALSE(slot.is_rom());
        REQUIRE_FALSE(slot.is_ram());
    }

    SECTION("Empty slot is not populated") {
        REQUIRE_FALSE(slot.is_populated());
    }

    SECTION("Empty slot is not writable") {
        REQUIRE_FALSE(slot.is_writable());
    }
}

TEST_CASE("ConfigurableSlot empty behavior", "[configurable_slot][empty]") {
    ConfigurableSlot slot(SlotType::Empty);

    SECTION("Read returns 0xFF") {
        REQUIRE(slot.read(0x0000) == 0xFF);
        REQUIRE(slot.read(0x2000) == 0xFF);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }

    SECTION("Write is ignored") {
        slot.write(0x0000, 0x42);
        REQUIRE(slot.read(0x0000) == 0xFF);
    }

    SECTION("Not populated") {
        REQUIRE_FALSE(slot.is_populated());
    }
}

TEST_CASE("ConfigurableSlot ROM behavior", "[configurable_slot][rom]") {
    ConfigurableSlot slot(SlotType::Rom);

    SECTION("Read returns data from slot") {
        // Load test data
        std::array<uint8_t, 16384> rom_data;
        rom_data.fill(0x42);
        slot.load(rom_data.data(), rom_data.size());

        REQUIRE(slot.read(0x0000) == 0x42);
        REQUIRE(slot.read(0x3FFF) == 0x42);
    }

    SECTION("Write is ignored") {
        std::array<uint8_t, 16384> rom_data;
        rom_data.fill(0x42);
        slot.load(rom_data.data(), rom_data.size());

        slot.write(0x0000, 0x00);
        REQUIRE(slot.read(0x0000) == 0x42);
    }

    SECTION("Populated after loading data") {
        std::array<uint8_t, 16384> rom_data;
        rom_data.fill(0x42);
        slot.load(rom_data.data(), rom_data.size());

        REQUIRE(slot.is_populated());
    }

    SECTION("Not populated if all 0xFF") {
        // Slot defaults to 0xFF, so if not loaded it's "not populated"
        REQUIRE_FALSE(slot.is_populated());
    }

    SECTION("ROM is not writable") {
        REQUIRE_FALSE(slot.is_writable());
    }
}

TEST_CASE("ConfigurableSlot RAM behavior", "[configurable_slot][ram]") {
    ConfigurableSlot slot(SlotType::Ram);

    SECTION("Read returns data from slot") {
        slot.write(0x0000, 0x42);
        REQUIRE(slot.read(0x0000) == 0x42);
    }

    SECTION("Write updates data") {
        slot.write(0x1234, 0xAB);
        slot.write(0x5678, 0xCD);

        REQUIRE(slot.read(0x1234) == 0xAB);
        REQUIRE(slot.read(0x5678) == 0xCD);
    }

    SECTION("RAM is writable") {
        REQUIRE(slot.is_writable());
    }

    SECTION("Populated after writing non-0xFF data") {
        slot.write(0x0000, 0x42);
        REQUIRE(slot.is_populated());
    }

    SECTION("Can load pre-initialised data") {
        std::array<uint8_t, 16384> ram_data;
        ram_data.fill(0x55);
        slot.load(ram_data.data(), ram_data.size());

        REQUIRE(slot.read(0x0000) == 0x55);
        REQUIRE(slot.read(0x3FFF) == 0x55);
    }

    SECTION("Can write after loading") {
        std::array<uint8_t, 16384> ram_data;
        ram_data.fill(0x55);
        slot.load(ram_data.data(), ram_data.size());

        slot.write(0x0000, 0xAA);
        REQUIRE(slot.read(0x0000) == 0xAA);
        REQUIRE(slot.read(0x0001) == 0x55);
    }
}

TEST_CASE("ConfigurableSlot type switching", "[configurable_slot][type]") {
    ConfigurableSlot slot;

    SECTION("Can switch from Empty to ROM") {
        slot.set_type(SlotType::Rom);
        REQUIRE(slot.type() == SlotType::Rom);
        REQUIRE(slot.is_rom());
    }

    SECTION("Can switch from Empty to RAM") {
        slot.set_type(SlotType::Ram);
        REQUIRE(slot.type() == SlotType::Ram);
        REQUIRE(slot.is_ram());
    }

    SECTION("Can switch from ROM to RAM") {
        slot.set_type(SlotType::Rom);
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        slot.load(data.data(), data.size());

        slot.set_type(SlotType::Ram);

        // Data should be preserved
        REQUIRE(slot.read(0x0000) == 0x42);

        // But now writable
        slot.write(0x0000, 0xAA);
        REQUIRE(slot.read(0x0000) == 0xAA);
    }

    SECTION("Can switch from RAM to ROM") {
        slot.set_type(SlotType::Ram);
        slot.write(0x0000, 0x42);

        slot.set_type(SlotType::Rom);

        // Data should be preserved
        REQUIRE(slot.read(0x0000) == 0x42);

        // But now read-only
        slot.write(0x0000, 0xAA);
        REQUIRE(slot.read(0x0000) == 0x42);
    }

    SECTION("Switching to Empty still allows reading internal data") {
        slot.set_type(SlotType::Ram);
        slot.write(0x0000, 0x42);

        slot.set_type(SlotType::Empty);

        // Empty always returns 0xFF regardless of internal data
        REQUIRE(slot.read(0x0000) == 0xFF);
    }
}

TEST_CASE("ConfigurableSlot clear", "[configurable_slot][clear]") {
    SECTION("Clearing RAM fills with 0x00") {
        ConfigurableSlot slot(SlotType::Ram);
        slot.write(0x0000, 0x42);
        slot.write(0x3FFF, 0x42);

        slot.clear();

        REQUIRE(slot.read(0x0000) == 0x00);
        REQUIRE(slot.read(0x3FFF) == 0x00);
    }

    SECTION("Clearing ROM fills with 0xFF") {
        ConfigurableSlot slot(SlotType::Rom);
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        slot.load(data.data(), data.size());

        slot.clear();

        REQUIRE(slot.read(0x0000) == 0xFF);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }

    SECTION("Clearing Empty fills with 0xFF") {
        ConfigurableSlot slot(SlotType::Empty);

        // Even though empty returns 0xFF anyway, clear should reset internal data
        slot.clear();

        // Switch to ROM to check internal data
        slot.set_type(SlotType::Rom);
        REQUIRE(slot.read(0x0000) == 0xFF);
    }
}

TEST_CASE("ConfigurableSlot image name tracking", "[configurable_slot][metadata]") {
    ConfigurableSlot slot(SlotType::Rom);

    SECTION("Initially empty") {
        REQUIRE(slot.image_name().empty());
    }

    SECTION("Can set image name") {
        slot.set_image_name("bbc-basic_2.rom");
        REQUIRE(slot.image_name() == "bbc-basic_2.rom");
    }

    SECTION("Can clear image name") {
        slot.set_image_name("test.rom");
        slot.clear_image_name();
        REQUIRE(slot.image_name().empty());
    }
}

TEST_CASE("ConfigurableSlot address wrapping", "[configurable_slot][addressing]") {
    ConfigurableSlot slot(SlotType::Ram);

    SECTION("Addresses wrap at 16KB boundary") {
        slot.write(0x0000, 0xAA);
        // Address 0x4000 should wrap to 0x0000 (0x4000 % 16384 = 0)
        REQUIRE(slot.read(0x4000) == 0xAA);

        slot.write(0x8000, 0xBB);
        // Address 0x8000 should wrap to 0x0000 (0x8000 % 16384 = 0)
        REQUIRE(slot.read(0x0000) == 0xBB);
    }
}

TEST_CASE("ConfigurableSlot load with smaller data", "[configurable_slot][load]") {
    ConfigurableSlot slot(SlotType::Rom);

    SECTION("Smaller data pads remainder with 0xFF") {
        std::array<uint8_t, 100> small_data;
        small_data.fill(0x42);

        slot.load(small_data.data(), small_data.size());

        REQUIRE(slot.read(0x0000) == 0x42);
        REQUIRE(slot.read(99) == 0x42);
        REQUIRE(slot.read(100) == 0xFF);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }
}

TEST_CASE("ConfigurableSlot direct data access", "[configurable_slot][direct]") {
    ConfigurableSlot slot(SlotType::Ram);

    SECTION("Can access raw data pointer") {
        uint8_t* data = slot.data();
        REQUIRE(data != nullptr);

        data[0] = 0xAA;
        data[16383] = 0xBB;

        REQUIRE(slot.read(0) == 0xAA);
        REQUIRE(slot.read(16383) == 0xBB);
    }

    SECTION("Const access works") {
        slot.write(0, 0x42);
        const ConfigurableSlot& const_slot = slot;
        const uint8_t* data = const_slot.data();
        REQUIRE(data[0] == 0x42);
    }
}
