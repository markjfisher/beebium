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
#include <beebium/devices/ConfigurableBankedMemory.hpp>
#include <array>

using namespace beebium;

TEST_CASE("ConfigurableBankedMemory initialization", "[configurable_memory][init]") {
    ConfigurableBankedMemory mem;

    SECTION("Default bank is 0") {
        REQUIRE(mem.selected_bank() == 0);
    }

    SECTION("All slots default to Empty") {
        for (uint8_t i = 0; i < 16; ++i) {
            REQUIRE(mem.slot(i).type() == SlotType::Empty);
        }
    }

    SECTION("Empty slots return 0xFF") {
        REQUIRE(mem.read(0x0000) == 0xFF);
        REQUIRE(mem.read(0x3FFF) == 0xFF);
    }

    SECTION("has_aliasing trait is false") {
        REQUIRE(ConfigurableBankedMemory::has_aliasing == false);
    }
}

TEST_CASE("ConfigurableBankedMemory slot configuration", "[configurable_memory][config]") {
    ConfigurableBankedMemory mem;

    SECTION("Configured RAM slots are writable and start at 0x00") {
        mem.configure_slot(0, SlotType::Ram);
        mem.select_bank(0);

        // RAM starts cleared to 0x00
        REQUIRE(mem.read(0x1000) == 0x00);

        // Can write and read back
        mem.write(0x1000, 0x42);
        REQUIRE(mem.read(0x1000) == 0x42);
    }

    SECTION("Configured ROM slots ignore writes") {
        mem.configure_slot(15, SlotType::Rom);
        mem.select_bank(15);

        // ROM starts at 0xFF (unpopulated)
        REQUIRE(mem.read(0x1000) == 0xFF);

        // Writes are ignored
        mem.write(0x1000, 0x42);
        REQUIRE(mem.read(0x1000) == 0xFF);
    }

    SECTION("Configure multiple slots for jsbeeb-compatible layout") {
        // Configure slots 0-7 as RAM (matches jsbeeb SWRAM)
        for (uint8_t i = 0; i < 8; ++i) {
            mem.configure_slot(i, SlotType::Ram);
        }
        // Configure slots 8-15 as ROM
        for (uint8_t i = 8; i < 16; ++i) {
            mem.configure_slot(i, SlotType::Rom);
        }

        // Verify slot types
        for (uint8_t i = 0; i < 8; ++i) {
            REQUIRE(mem.slot(i).type() == SlotType::Ram);
        }
        for (uint8_t i = 8; i < 16; ++i) {
            REQUIRE(mem.slot(i).type() == SlotType::Rom);
        }

        // Verify RAM slots start at 0x00
        mem.select_bank(0);
        REQUIRE(mem.read(0x0000) == 0x00);
    }
}

TEST_CASE("ConfigurableBankedMemory no aliasing", "[configurable_memory][independent]") {
    ConfigurableBankedMemory mem;

    // Configure all 16 slots with different data
    for (uint8_t i = 0; i < 16; ++i) {
        mem.configure_slot(i, SlotType::Rom);
        std::array<uint8_t, 16384> data;
        data.fill(i);  // Each slot has its slot number as content
        mem.load_rom(i, data.data(), data.size());
    }

    SECTION("Each slot has unique data") {
        for (uint8_t i = 0; i < 16; ++i) {
            mem.select_bank(i);
            REQUIRE(mem.read(0x0000) == i);
        }
    }

    SECTION("Slots 3, 7, 11, 15 are independent (unlike aliased Model B)") {
        mem.select_bank(3);
        REQUIRE(mem.read(0x0000) == 3);

        mem.select_bank(7);
        REQUIRE(mem.read(0x0000) == 7);

        mem.select_bank(11);
        REQUIRE(mem.read(0x0000) == 11);

        mem.select_bank(15);
        REQUIRE(mem.read(0x0000) == 15);
    }
}

TEST_CASE("ConfigurableBankedMemory bank selection", "[configurable_memory][banking]") {
    ConfigurableBankedMemory mem;

    SECTION("select_bank updates selected_bank") {
        mem.select_bank(5);
        REQUIRE(mem.selected_bank() == 5);
    }

    SECTION("select_bank masks to 4 bits") {
        mem.select_bank(0xFF);
        REQUIRE(mem.selected_bank() == 15);
    }

    SECTION("Different slots are fully independent") {
        mem.configure_slot(0, SlotType::Ram);
        mem.configure_slot(4, SlotType::Ram);

        mem.select_bank(0);
        mem.write(0x1000, 0xAA);

        mem.select_bank(4);
        mem.write(0x1000, 0xBB);

        // Data in slot 0 and slot 4 are independent
        mem.select_bank(0);
        REQUIRE(mem.read(0x1000) == 0xAA);

        mem.select_bank(4);
        REQUIRE(mem.read(0x1000) == 0xBB);
    }
}

TEST_CASE("ConfigurableBankedMemory RAM operations", "[configurable_memory][ram]") {
    ConfigurableBankedMemory mem;

    mem.configure_slot(0, SlotType::Ram);
    mem.configure_slot(1, SlotType::Ram);

    SECTION("Can write and read back") {
        mem.select_bank(0);
        mem.write(0x1234, 0x42);
        REQUIRE(mem.read(0x1234) == 0x42);
    }

    SECTION("Multiple RAM slots are independent") {
        mem.select_bank(0);
        mem.write(0x0000, 0xAA);

        mem.select_bank(1);
        mem.write(0x0000, 0xBB);

        mem.select_bank(0);
        REQUIRE(mem.read(0x0000) == 0xAA);

        mem.select_bank(1);
        REQUIRE(mem.read(0x0000) == 0xBB);
    }
}

TEST_CASE("ConfigurableBankedMemory ROM operations", "[configurable_memory][rom]") {
    ConfigurableBankedMemory mem;

    mem.configure_slot(15, SlotType::Rom);
    std::array<uint8_t, 16384> rom_data;
    rom_data.fill(0x55);
    mem.load_rom(15, rom_data.data(), rom_data.size());

    SECTION("Can read ROM data") {
        mem.select_bank(15);
        REQUIRE(mem.read(0x0000) == 0x55);
        REQUIRE(mem.read(0x3FFF) == 0x55);
    }

    SECTION("Writes to ROM are ignored") {
        mem.select_bank(15);
        mem.write(0x0000, 0xAA);
        REQUIRE(mem.read(0x0000) == 0x55);
    }
}

TEST_CASE("ConfigurableBankedMemory load_rom_to_slot", "[configurable_memory][load]") {
    ConfigurableBankedMemory mem;

    std::array<uint8_t, 16384> rom_data;
    rom_data.fill(0x77);

    SECTION("Sets type to ROM and loads data") {
        mem.load_rom_to_slot(14, rom_data.data(), rom_data.size());

        REQUIRE(mem.slot(14).type() == SlotType::Rom);
        mem.select_bank(14);
        REQUIRE(mem.read(0x0000) == 0x77);
    }
}

TEST_CASE("ConfigurableBankedMemory direct bank access", "[configurable_memory][direct]") {
    ConfigurableBankedMemory mem;

    mem.configure_slot(5, SlotType::Rom);
    std::array<uint8_t, 16384> data;
    data.fill(0x88);
    mem.load_rom(5, data.data(), data.size());

    SECTION("peek_bank reads from specified bank") {
        REQUIRE(mem.peek_bank(5, 0x0000) == 0x88);
        REQUIRE(mem.peek_bank(5, 0x3FFF) == 0x88);
    }

    SECTION("read_bank reads from specified bank") {
        REQUIRE(mem.read_bank(5, 0x0000) == 0x88);
    }

    SECTION("write_bank writes to specified bank") {
        mem.configure_slot(6, SlotType::Ram);
        mem.write_bank(6, 0x1000, 0xEE);
        REQUIRE(mem.read_bank(6, 0x1000) == 0xEE);
    }

    SECTION("Out of range bank returns 0xFF") {
        REQUIRE(mem.peek_bank(255, 0x0000) == 0xFF);
    }
}

TEST_CASE("ConfigurableBankedMemory is_bank_populated", "[configurable_memory][populated]") {
    ConfigurableBankedMemory mem;

    SECTION("Empty slot is not populated") {
        REQUIRE_FALSE(mem.is_bank_populated(0));
    }

    SECTION("Loaded ROM is populated") {
        mem.configure_slot(15, SlotType::Rom);
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        mem.load_rom(15, data.data(), data.size());

        REQUIRE(mem.is_bank_populated(15));
    }

    SECTION("RAM with data is populated") {
        mem.configure_slot(0, SlotType::Ram);
        mem.select_bank(0);
        mem.write(0x0000, 0x42);

        REQUIRE(mem.is_bank_populated(0));
    }
}

TEST_CASE("ConfigurableBankedMemory bank_type", "[configurable_memory][type]") {
    ConfigurableBankedMemory mem;

    SECTION("Returns correct type for configured slots") {
        mem.configure_slot(0, SlotType::Ram);
        mem.configure_slot(15, SlotType::Rom);

        REQUIRE(mem.bank_type(0) == SlotType::Ram);
        REQUIRE(mem.bank_type(15) == SlotType::Rom);
        REQUIRE(mem.bank_type(5) == SlotType::Empty);
    }
}

TEST_CASE("ConfigurableBankedMemory slot metadata", "[configurable_memory][metadata]") {
    ConfigurableBankedMemory mem;

    SECTION("Can set and get slot image name") {
        mem.set_slot_image_name(15, "bbc-basic_2.rom");
        REQUIRE(mem.slot_image_name(15) == "bbc-basic_2.rom");
    }

    SECTION("Different slots have independent names") {
        mem.set_slot_image_name(14, "acorn-dfs_2_26.rom");
        mem.set_slot_image_name(15, "bbc-basic_2.rom");

        REQUIRE(mem.slot_image_name(14) == "acorn-dfs_2_26.rom");
        REQUIRE(mem.slot_image_name(15) == "bbc-basic_2.rom");
    }

    SECTION("clear_slot clears image name") {
        mem.configure_slot(0, SlotType::Ram);
        mem.set_slot_image_name(0, "test.bin");
        mem.select_bank(0);
        mem.write(0x0000, 0x42);

        mem.clear_slot(0);

        REQUIRE(mem.slot_image_name(0).empty());
        REQUIRE(mem.read(0x0000) == 0x00);  // RAM cleared to 0x00
    }
}

TEST_CASE("ConfigurableBankedMemory typical ROM/RAM board configuration", "[configurable_memory][typical]") {
    ConfigurableBankedMemory mem;

    // Configure slots 0-7 as RAM (typical SWRAM configuration)
    for (uint8_t i = 0; i < 8; ++i) {
        mem.configure_slot(i, SlotType::Ram);
    }

    // Load ROMs into typical positions (use load_rom_to_slot which sets type)
    std::array<uint8_t, 16384> basic_data;
    basic_data.fill(0xBA);
    mem.load_rom_to_slot(15, basic_data.data(), basic_data.size());
    mem.set_slot_image_name(15, "bbc-basic_2.rom");

    std::array<uint8_t, 16384> dfs_data;
    dfs_data.fill(0xDF);
    mem.load_rom_to_slot(14, dfs_data.data(), dfs_data.size());
    mem.set_slot_image_name(14, "acorn-dfs_2_26.rom");

    std::array<uint8_t, 16384> adfs_data;
    adfs_data.fill(0xAD);
    mem.load_rom_to_slot(13, adfs_data.data(), adfs_data.size());
    mem.set_slot_image_name(13, "acorn-adfs_1_30.rom");

    SECTION("BASIC at slot 15") {
        mem.select_bank(15);
        REQUIRE(mem.read(0x0000) == 0xBA);
    }

    SECTION("DFS at slot 14") {
        mem.select_bank(14);
        REQUIRE(mem.read(0x0000) == 0xDF);
    }

    SECTION("ADFS at slot 13") {
        mem.select_bank(13);
        REQUIRE(mem.read(0x0000) == 0xAD);
    }

    SECTION("Sideways RAM at slots 0-7") {
        for (uint8_t i = 0; i < 8; ++i) {
            mem.select_bank(i);
            mem.write(0x0000, i);
            REQUIRE(mem.read(0x0000) == i);
        }
    }

    SECTION("All 8 RAM banks are independent") {
        // Write unique value to each
        for (uint8_t i = 0; i < 8; ++i) {
            mem.select_bank(i);
            mem.write(0x0000, i + 0xA0);
        }

        // Verify each has its own value
        for (uint8_t i = 0; i < 8; ++i) {
            mem.select_bank(i);
            REQUIRE(mem.read(0x0000) == i + 0xA0);
        }
    }
}
