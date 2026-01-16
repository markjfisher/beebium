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
#include <beebium/ModelBHardware.hpp>
#include <beebium/Via6522.hpp>
#include <beebium/devices/ConfigurableSlot.hpp>  // for SlotType
#include <array>

using namespace beebium;

TEST_CASE("ModelBHardware initialization", "[memory][init]") {
    ModelBHardware hw;

    SECTION("RAM is zeroed on construction") {
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            REQUIRE(hw.read(addr) == 0x00);
        }
    }

    SECTION("Empty sideways slots return 0xFF") {
        // Default bank 0 maps to socket 0 (IC52), which is empty by default
        // Empty slots return 0xFF (bus pull-ups on real hardware)
        REQUIRE(hw.read(0x8000) == 0xFF);

        // Bank 2 also maps to socket 2 (IC100), which is empty
        hw.write(0xFE30, 2);
        REQUIRE(hw.read(0x8000) == 0xFF);
    }

    SECTION("MOS ROM returns 0x00 initially (unloaded)") {
        // MOS area (excluding I/O at 0xFE00-0xFEFF)
        for (uint16_t addr = 0xC000; addr < 0xFE00; ++addr) {
            REQUIRE(hw.read(addr) == 0x00);  // Unloaded MOS ROM is zero
        }
    }

    SECTION("Default ROM bank is 0") {
        REQUIRE(hw.sideways.selected_bank() == 0);
    }
}

TEST_CASE("ModelBHardware RAM read/write", "[memory][ram]") {
    ModelBHardware hw;

    SECTION("Write and read back single byte") {
        hw.write(0x1234, 0xAB);
        REQUIRE(hw.read(0x1234) == 0xAB);
    }

    SECTION("Write to zero page") {
        hw.write(0x00, 0x12);
        hw.write(0xFF, 0x34);
        REQUIRE(hw.read(0x00) == 0x12);
        REQUIRE(hw.read(0xFF) == 0x34);
    }

    SECTION("Write to stack page") {
        hw.write(0x01FF, 0x42);
        hw.write(0x0100, 0x24);
        REQUIRE(hw.read(0x01FF) == 0x42);
        REQUIRE(hw.read(0x0100) == 0x24);
    }

    SECTION("Write entire RAM range") {
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            hw.write(addr, static_cast<uint8_t>(addr & 0xFF));
        }
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            REQUIRE(hw.read(addr) == static_cast<uint8_t>(addr & 0xFF));
        }
    }
}

TEST_CASE("ModelBHardware ROM is read-only", "[memory][rom]") {
    ModelBHardware hw;

    // Load some test data into MOS
    std::array<uint8_t, 16384> mos_data;
    std::fill(mos_data.begin(), mos_data.end(), 0x42);
    hw.load_mos(mos_data.data(), mos_data.size());

    SECTION("MOS ROM can be read") {
        REQUIRE(hw.read(0xC000) == 0x42);
        // Note: 0xFExx is I/O region, not MOS ROM
        REQUIRE(hw.read(0xFDFF) == 0x42);
    }

    SECTION("Writes to MOS ROM are ignored") {
        hw.write(0xC000, 0x00);
        REQUIRE(hw.read(0xC000) == 0x42);
    }

    // Load some test data into BASIC ROM (socket 3, slots 3/7/11/15)
    std::array<uint8_t, 16384> rom_data;
    std::fill(rom_data.begin(), rom_data.end(), 0x24);
    hw.load_basic(rom_data.data(), rom_data.size());

    SECTION("Paged ROM can be read at correct slot") {
        // BASIC is at socket 3, accessed via slots 3, 7, 11, or 15
        hw.write(0xFE30, 15);  // Select slot 15 (socket 3)
        REQUIRE(hw.read(0x8000) == 0x24);
        REQUIRE(hw.read(0xBFFF) == 0x24);
    }

    SECTION("Writes to paged ROM are ignored") {
        hw.write(0xFE30, 15);  // Select slot 15 (socket 3)
        hw.write(0x8000, 0x00);
        REQUIRE(hw.read(0x8000) == 0x24);
    }
}

TEST_CASE("ModelBHardware ROM bank switching", "[memory][rom][banking]") {
    ModelBHardware hw;

    // Load different data into ROM sockets:
    // - BASIC goes to socket 3 (IC101, slots 3/7/11/15)
    // - DFS goes to socket 1 (IC88, slots 1/5/9/13)
    std::array<uint8_t, 16384> basic_data, dfs_data;
    std::fill(basic_data.begin(), basic_data.end(), 0xBA);  // 0xBA for BASIC
    std::fill(dfs_data.begin(), dfs_data.end(), 0xDF);      // 0xDF for DFS

    hw.load_basic(basic_data.data(), basic_data.size());
    hw.load_dfs(dfs_data.data(), dfs_data.size());

    SECTION("Default bank is 0 (socket 0 is empty)") {
        // Default bank 0 maps to socket 0 (IC52), which is empty
        REQUIRE(hw.read(0x8000) == 0xFF);  // Empty socket returns 0xFF
    }

    SECTION("BASIC at slot 15 (socket 3)") {
        hw.sideways.select_bank(15);
        REQUIRE(hw.sideways.selected_bank() == 15);
        REQUIRE(hw.read(0x8000) == 0xBA);
    }

    SECTION("DFS at slot 13 (socket 1)") {
        hw.sideways.select_bank(13);
        REQUIRE(hw.sideways.selected_bank() == 13);
        REQUIRE(hw.read(0x8000) == 0xDF);
    }

    SECTION("Socket aliasing - BASIC at slots 3, 7, 11, 15") {
        // All these slots map to socket 3 (IC101)
        for (uint8_t slot : {3, 7, 11, 15}) {
            hw.write(0xFE30, slot);
            REQUIRE(hw.read(0x8000) == 0xBA);
        }
    }

    SECTION("Socket aliasing - DFS at slots 1, 5, 9, 13") {
        // All these slots map to socket 1 (IC88)
        for (uint8_t slot : {1, 5, 9, 13}) {
            hw.write(0xFE30, slot);
            REQUIRE(hw.read(0x8000) == 0xDF);
        }
    }

    SECTION("Empty sockets return 0xFF (IC52 and IC100)") {
        // Socket 0 (IC52): slots 0, 4, 8, 12
        for (uint8_t slot : {0, 4, 8, 12}) {
            hw.write(0xFE30, slot);
            REQUIRE(hw.read(0x8000) == 0xFF);
        }
        // Socket 2 (IC100): slots 2, 6, 10, 14
        for (uint8_t slot : {2, 6, 10, 14}) {
            hw.write(0xFE30, slot);
            REQUIRE(hw.read(0x8000) == 0xFF);
        }
    }

    SECTION("ROMSEL write switches bank") {
        hw.write(0xFE30, 15);
        REQUIRE(hw.sideways.selected_bank() == 15);
        REQUIRE(hw.read(0x8000) == 0xBA);  // BASIC

        hw.write(0xFE30, 13);
        REQUIRE(hw.sideways.selected_bank() == 13);
        REQUIRE(hw.read(0x8000) == 0xDF);  // DFS
    }

    SECTION("Sideways RAM is writable when configured") {
        // Configure socket 0 (IC52, slots 0/4/8/12) as RAM
        hw.configure_slot(0, SlotType::Ram);

        hw.write(0xFE30, 0);  // Select slot 0 (maps to socket 0)
        hw.write(0x8000, 0x42);
        REQUIRE(hw.read(0x8000) == 0x42);

        // Verify aliased slots also see the same RAM
        hw.write(0xFE30, 4);  // Slot 4 also maps to socket 0
        REQUIRE(hw.read(0x8000) == 0x42);

        // Switch to a different socket (BASIC at slot 15 = socket 3)
        hw.write(0xFE30, 15);
        REQUIRE(hw.read(0x8000) == 0xBA);  // BASIC ROM
    }
}

TEST_CASE("ModelBHardware I/O address handling", "[memory][io]") {
    ModelBHardware hw;

    SECTION("ROMSEL read returns 0xFF (write-only)") {
        REQUIRE(hw.read(0xFE30) == 0xFF);
    }

    SECTION("VIA addresses are handled directly") {
        // System VIA at FE40 - write DDRA (register 3)
        hw.system_via.write(Via6522::REG_DDRA, 0xFF);
        REQUIRE(hw.read(0xFE43) == 0xFF);

        // User VIA at FE60 - write DDRB (register 2)
        hw.user_via.write(Via6522::REG_DDRB, 0xAA);
        REQUIRE(hw.read(0xFE62) == 0xAA);
    }

    SECTION("VIA mirroring works") {
        // System VIA mirrors at 0xFE50
        hw.write(0xFE43, 0x55);  // DDRA at base
        REQUIRE(hw.read(0xFE53) == 0x55);  // Mirrored

        // User VIA mirrors at 0xFE70
        hw.write(0xFE62, 0xAA);  // DDRB at base
        REQUIRE(hw.read(0xFE72) == 0xAA);  // Mirrored
    }
}

TEST_CASE("ModelBHardware reset", "[memory][reset]") {
    ModelBHardware hw;

    // Write some data
    hw.write(0x1000, 0x42);
    hw.write(0xFE30, 5);  // Change ROM bank

    // Reset
    hw.reset();

    SECTION("RAM is cleared on reset") {
        REQUIRE(hw.read(0x1000) == 0x00);
    }

    SECTION("ROM bank is reset to 0") {
        REQUIRE(hw.sideways.selected_bank() == 0);
    }
}

TEST_CASE("ModelBHardware direct device access", "[memory][devices]") {
    ModelBHardware hw;

    SECTION("Can access main_ram directly") {
        hw.main_ram.write(0x100, 0x42);
        REQUIRE(hw.read(0x100) == 0x42);
    }

    SECTION("Can access system_via directly") {
        hw.system_via.write(Via6522::REG_DDRA, 0xFF);
        hw.system_via.write(Via6522::REG_ORA, 0x55);
        // Read back via memory map
        REQUIRE((hw.read(0xFE41) & 0xFF) != 0);
    }

    SECTION("Can access sideways directly via load_basic") {
        // Load data via load_basic (goes to socket 3, slots 3/7/11/15)
        uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        hw.load_basic(data, 4);
        hw.sideways.select_bank(15);  // Select slot 15 (socket 3, BASIC)
        REQUIRE(hw.read(0x8000) == 0xAA);

        // Aliased slots should see same data
        hw.sideways.select_bank(3);
        REQUIRE(hw.read(0x8000) == 0xAA);
    }

    SECTION("Can write to sideways RAM directly") {
        // Configure socket 0 (IC52, slots 0/4/8/12) as RAM
        hw.configure_slot(0, SlotType::Ram);

        // Write via sideways.write_bank (direct access)
        hw.sideways.write_bank(0, 0x100, 0x42);

        // Select slot 0 and verify via memory map
        hw.sideways.select_bank(0);
        REQUIRE(hw.read(0x8100) == 0x42);

        // Verify aliased slot 4 also sees the data
        hw.sideways.select_bank(4);
        REQUIRE(hw.read(0x8100) == 0x42);
    }
}

TEST_CASE("ModelBHardware peripheral clocking", "[memory][peripherals]") {
    ModelBHardware hw;

    SECTION("poll_irq runs without crash") {
        // Tick VIAs directly and poll IRQ
        for (uint64_t cycle = 0; cycle < 100; ++cycle) {
            hw.system_via.tick_falling();
            hw.user_via.tick_falling();
            hw.poll_irq();
        }
    }

    SECTION("VIA timers decrement over time") {
        // Set up a timer
        hw.system_via.write(Via6522::REG_T1LL, 0xFF);
        hw.system_via.write(Via6522::REG_T1LH, 0x00);
        hw.system_via.write(Via6522::REG_T1CH, 0x00);  // Start timer at 0x00FF

        // Run some cycles - tick VIA directly
        for (int cycle = 0; cycle < 10; ++cycle) {
            hw.system_via.tick_falling();
        }

        // Timer should have decremented
        uint8_t low = hw.system_via.read(Via6522::REG_T1CL);
        REQUIRE(low < 0xFF);
    }
}
