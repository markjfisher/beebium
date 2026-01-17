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
#include <beebium/ModelBRomRamBoardHardware.hpp>
#include <beebium/Machines.hpp>
#include <array>
#include <stdexcept>

using namespace beebium;

TEST_CASE("ModelBRomRamBoardHardware load_sideways_rom", "[hardware][romram]") {
    ModelBRomRamBoardHardware memory;

    std::array<uint8_t, 16384> rom_data;
    rom_data.fill(0x42);
    rom_data[0] = 0xAA;
    rom_data[1] = 0xBB;
    rom_data[2] = 0xCC;

    SECTION("Loads ROM into sideways slot") {
        memory.load_sideways_rom(15, rom_data.data(), rom_data.size());

        // Verify via sideways.peek_bank
        REQUIRE(memory.sideways.peek_bank(15, 0) == 0xAA);
        REQUIRE(memory.sideways.peek_bank(15, 1) == 0xBB);
        REQUIRE(memory.sideways.peek_bank(15, 2) == 0xCC);
    }
}

TEST_CASE("ModelBRomRamBoard Machine peek_region via memory()", "[machine][romram][peek_region]") {
    // Test using the full Machine template, same as gRPC server uses
    ModelBRomRamBoard machine;
    auto& memory = machine.state().memory;

    std::array<uint8_t, 16384> rom_data;
    rom_data.fill(0x42);
    rom_data[0] = 0xAA;
    rom_data[1] = 0xBB;
    rom_data[2] = 0xCC;

    SECTION("machine.memory().peek_region returns loaded ROM data") {
        memory.load_sideways_rom(15, rom_data.data(), rom_data.size());

        // Access via machine.memory() - same path as gRPC service
        REQUIRE(machine.memory().peek_region("bank_15", 0x8000) == 0xAA);
        REQUIRE(machine.memory().peek_region("bank_15", 0x8001) == 0xBB);
        REQUIRE(machine.memory().peek_region("bank_15", 0x8002) == 0xCC);
    }
}

TEST_CASE("ModelBRomRamBoardHardware peek_region for sideways banks", "[hardware][romram][peek_region]") {
    ModelBRomRamBoardHardware memory;

    std::array<uint8_t, 16384> rom_data;
    rom_data.fill(0x42);
    rom_data[0] = 0xAA;
    rom_data[1] = 0xBB;
    rom_data[2] = 0xCC;

    SECTION("peek_region returns loaded ROM data via bank_N") {
        memory.load_sideways_rom(15, rom_data.data(), rom_data.size());

        // peek_region("bank_15", 0x8000) should return first byte
        REQUIRE(memory.peek_region("bank_15", 0x8000) == 0xAA);
        REQUIRE(memory.peek_region("bank_15", 0x8001) == 0xBB);
        REQUIRE(memory.peek_region("bank_15", 0x8002) == 0xCC);
        REQUIRE(memory.peek_region("bank_15", 0x8064) == 0x42);  // offset 100
    }

    SECTION("peek_region works for all bank names") {
        // Load ROMs into multiple slots
        for (uint8_t slot = 0; slot < 16; ++slot) {
            std::array<uint8_t, 16384> data;
            data.fill(slot);  // Each slot has its number as content
            memory.load_sideways_rom(slot, data.data(), data.size());
        }

        // Verify each bank via peek_region
        REQUIRE(memory.peek_region("bank_0", 0x8000) == 0);
        REQUIRE(memory.peek_region("bank_5", 0x8000) == 5);
        REQUIRE(memory.peek_region("bank_10", 0x8000) == 10);
        REQUIRE(memory.peek_region("bank_15", 0x8000) == 15);
    }

    SECTION("peek_region throws for out-of-range address") {
        memory.load_sideways_rom(15, rom_data.data(), rom_data.size());

        // Address below 0x8000 for bank region
        REQUIRE_THROWS_AS(memory.peek_region("bank_15", 0x7FFF), std::invalid_argument);
        // Address at or above 0xC000 for bank region
        REQUIRE_THROWS_AS(memory.peek_region("bank_15", 0xC000), std::invalid_argument);
    }

    SECTION("peek_region throws for invalid region name") {
        memory.load_sideways_rom(15, rom_data.data(), rom_data.size());

        REQUIRE_THROWS_AS(memory.peek_region("invalid", 0x8000), std::invalid_argument);
        REQUIRE_THROWS_AS(memory.peek_region("bank_16", 0x8000), std::invalid_argument);
        REQUIRE_THROWS_AS(memory.peek_region("bank_", 0x8000), std::invalid_argument);
    }
}
