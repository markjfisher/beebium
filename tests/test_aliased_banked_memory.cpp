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
#include <beebium/devices/AliasedBankedMemory.hpp>
#include <array>

using namespace beebium;

TEST_CASE("AliasedBankedMemory slot-to-socket mapping", "[aliased_memory][mapping]") {
    SECTION("Bottom 2 bits determine socket") {
        REQUIRE(AliasedBankedMemory::slot_to_socket(0) == 0);
        REQUIRE(AliasedBankedMemory::slot_to_socket(1) == 1);
        REQUIRE(AliasedBankedMemory::slot_to_socket(2) == 2);
        REQUIRE(AliasedBankedMemory::slot_to_socket(3) == 3);
    }

    SECTION("Socket 0 (IC52) is accessed by slots 0, 4, 8, 12") {
        REQUIRE(AliasedBankedMemory::slot_to_socket(0) == 0);
        REQUIRE(AliasedBankedMemory::slot_to_socket(4) == 0);
        REQUIRE(AliasedBankedMemory::slot_to_socket(8) == 0);
        REQUIRE(AliasedBankedMemory::slot_to_socket(12) == 0);
    }

    SECTION("Socket 1 (IC88) is accessed by slots 1, 5, 9, 13") {
        REQUIRE(AliasedBankedMemory::slot_to_socket(1) == 1);
        REQUIRE(AliasedBankedMemory::slot_to_socket(5) == 1);
        REQUIRE(AliasedBankedMemory::slot_to_socket(9) == 1);
        REQUIRE(AliasedBankedMemory::slot_to_socket(13) == 1);
    }

    SECTION("Socket 2 (IC100) is accessed by slots 2, 6, 10, 14") {
        REQUIRE(AliasedBankedMemory::slot_to_socket(2) == 2);
        REQUIRE(AliasedBankedMemory::slot_to_socket(6) == 2);
        REQUIRE(AliasedBankedMemory::slot_to_socket(10) == 2);
        REQUIRE(AliasedBankedMemory::slot_to_socket(14) == 2);
    }

    SECTION("Socket 3 (IC101) is accessed by slots 3, 7, 11, 15") {
        REQUIRE(AliasedBankedMemory::slot_to_socket(3) == 3);
        REQUIRE(AliasedBankedMemory::slot_to_socket(7) == 3);
        REQUIRE(AliasedBankedMemory::slot_to_socket(11) == 3);
        REQUIRE(AliasedBankedMemory::slot_to_socket(15) == 3);
    }

    SECTION("socket_aliased_slots returns correct aliases") {
        auto slots_0 = AliasedBankedMemory::socket_aliased_slots(0);
        REQUIRE(slots_0 == std::array<uint8_t, 4>{0, 4, 8, 12});

        auto slots_1 = AliasedBankedMemory::socket_aliased_slots(1);
        REQUIRE(slots_1 == std::array<uint8_t, 4>{1, 5, 9, 13});

        auto slots_2 = AliasedBankedMemory::socket_aliased_slots(2);
        REQUIRE(slots_2 == std::array<uint8_t, 4>{2, 6, 10, 14});

        auto slots_3 = AliasedBankedMemory::socket_aliased_slots(3);
        REQUIRE(slots_3 == std::array<uint8_t, 4>{3, 7, 11, 15});
    }
}

TEST_CASE("AliasedBankedMemory initialization", "[aliased_memory][init]") {
    AliasedBankedMemory mem;

    SECTION("Default bank is 0") {
        REQUIRE(mem.selected_bank() == 0);
    }

    SECTION("All sockets default to Empty") {
        REQUIRE(mem.socket(0).type() == SlotType::Empty);
        REQUIRE(mem.socket(1).type() == SlotType::Empty);
        REQUIRE(mem.socket(2).type() == SlotType::Empty);
        REQUIRE(mem.socket(3).type() == SlotType::Empty);
    }

    SECTION("Empty sockets return 0xFF") {
        REQUIRE(mem.read(0x0000) == 0xFF);
        REQUIRE(mem.read(0x3FFF) == 0xFF);
    }

    SECTION("has_aliasing trait is true") {
        REQUIRE(AliasedBankedMemory::has_aliasing == true);
    }
}

TEST_CASE("AliasedBankedMemory bank selection", "[aliased_memory][banking]") {
    AliasedBankedMemory mem;

    SECTION("select_bank updates selected_bank") {
        mem.select_bank(5);
        REQUIRE(mem.selected_bank() == 5);
    }

    SECTION("select_bank masks to 4 bits") {
        mem.select_bank(0xFF);
        REQUIRE(mem.selected_bank() == 15);
    }

    SECTION("selected_socket reflects aliasing") {
        mem.select_bank(0);
        REQUIRE(mem.selected_socket() == 0);

        mem.select_bank(4);
        REQUIRE(mem.selected_socket() == 0);  // Same socket as bank 0

        mem.select_bank(15);
        REQUIRE(mem.selected_socket() == 3);  // IC101

        mem.select_bank(3);
        REQUIRE(mem.selected_socket() == 3);  // Same socket as bank 15
    }
}

TEST_CASE("AliasedBankedMemory aliasing behavior", "[aliased_memory][aliasing]") {
    AliasedBankedMemory mem;

    // Configure socket 3 (IC101, BASIC) as ROM
    mem.configure_socket(AliasedBankedMemory::SOCKET_IC101, SlotType::Rom);
    std::array<uint8_t, 16384> basic_data;
    basic_data.fill(0x42);
    basic_data[0] = 0xAA;  // Unique marker at start
    mem.load_rom_to_socket(AliasedBankedMemory::SOCKET_IC101, basic_data.data(), basic_data.size());

    SECTION("Same data visible from all aliased slots") {
        // Slot 3 -> socket 3
        mem.select_bank(3);
        REQUIRE(mem.read(0x0000) == 0xAA);
        REQUIRE(mem.read(0x3FFF) == 0x42);

        // Slot 7 -> socket 3 (same)
        mem.select_bank(7);
        REQUIRE(mem.read(0x0000) == 0xAA);

        // Slot 11 -> socket 3 (same)
        mem.select_bank(11);
        REQUIRE(mem.read(0x0000) == 0xAA);

        // Slot 15 -> socket 3 (same, where MOS finds BASIC)
        mem.select_bank(15);
        REQUIRE(mem.read(0x0000) == 0xAA);
    }

    SECTION("Different sockets have different data") {
        // Socket 0 (IC52) still empty
        mem.select_bank(0);
        REQUIRE(mem.read(0x0000) == 0xFF);

        // Socket 1 (IC88) still empty
        mem.select_bank(1);
        REQUIRE(mem.read(0x0000) == 0xFF);
    }
}

TEST_CASE("AliasedBankedMemory configure_slot uses aliasing", "[aliased_memory][config]") {
    AliasedBankedMemory mem;

    SECTION("configure_slot on slot 15 affects socket 3") {
        mem.configure_slot(15, SlotType::Ram);

        // Socket 3 should be RAM
        REQUIRE(mem.socket(3).type() == SlotType::Ram);

        // All aliased slots see the same type
        REQUIRE(mem.bank_type(3) == SlotType::Ram);
        REQUIRE(mem.bank_type(7) == SlotType::Ram);
        REQUIRE(mem.bank_type(11) == SlotType::Ram);
        REQUIRE(mem.bank_type(15) == SlotType::Ram);
    }

    SECTION("load_rom by slot uses aliasing") {
        mem.configure_slot(13, SlotType::Rom);  // DFS slot -> socket 1
        std::array<uint8_t, 16384> dfs_data;
        dfs_data.fill(0xDF);
        mem.load_rom(13, dfs_data.data(), dfs_data.size());

        // All slots aliasing to socket 1 see DFS data
        mem.select_bank(1);
        REQUIRE(mem.read(0x0000) == 0xDF);

        mem.select_bank(5);
        REQUIRE(mem.read(0x0000) == 0xDF);

        mem.select_bank(9);
        REQUIRE(mem.read(0x0000) == 0xDF);

        mem.select_bank(13);
        REQUIRE(mem.read(0x0000) == 0xDF);
    }
}

TEST_CASE("AliasedBankedMemory RAM socket", "[aliased_memory][ram]") {
    AliasedBankedMemory mem;

    // Configure socket 0 (IC52) as RAM
    mem.configure_socket(AliasedBankedMemory::SOCKET_IC52, SlotType::Ram);

    SECTION("Can write to RAM socket") {
        mem.select_bank(0);
        mem.write(0x1000, 0x42);
        REQUIRE(mem.read(0x1000) == 0x42);
    }

    SECTION("Writes visible from all aliased slots") {
        mem.select_bank(0);
        mem.write(0x1000, 0xAB);

        // Same data visible from aliased slots
        mem.select_bank(4);
        REQUIRE(mem.read(0x1000) == 0xAB);

        mem.select_bank(8);
        REQUIRE(mem.read(0x1000) == 0xAB);

        mem.select_bank(12);
        REQUIRE(mem.read(0x1000) == 0xAB);
    }

    SECTION("Write from one aliased slot, read from another") {
        mem.select_bank(8);
        mem.write(0x2000, 0xCD);

        mem.select_bank(0);
        REQUIRE(mem.read(0x2000) == 0xCD);
    }
}

TEST_CASE("AliasedBankedMemory ROM socket write protection", "[aliased_memory][rom]") {
    AliasedBankedMemory mem;

    // Configure socket 3 (IC101) as ROM
    mem.configure_socket(AliasedBankedMemory::SOCKET_IC101, SlotType::Rom);
    std::array<uint8_t, 16384> rom_data;
    rom_data.fill(0x55);
    mem.load_rom_to_socket(AliasedBankedMemory::SOCKET_IC101, rom_data.data(), rom_data.size());

    SECTION("Writes to ROM socket are ignored") {
        mem.select_bank(15);
        mem.write(0x0000, 0xAA);
        REQUIRE(mem.read(0x0000) == 0x55);
    }

    SECTION("Writes from any aliased slot are ignored") {
        mem.select_bank(3);
        mem.write(0x0000, 0xBB);
        REQUIRE(mem.read(0x0000) == 0x55);

        mem.select_bank(7);
        mem.write(0x0000, 0xCC);

        mem.select_bank(15);
        REQUIRE(mem.read(0x0000) == 0x55);
    }
}

TEST_CASE("AliasedBankedMemory direct bank access", "[aliased_memory][direct]") {
    AliasedBankedMemory mem;

    // Configure socket 3 as ROM
    mem.configure_socket(3, SlotType::Rom);
    std::array<uint8_t, 16384> data;
    data.fill(0x77);
    mem.load_rom_to_socket(3, data.data(), data.size());

    SECTION("peek_bank respects aliasing") {
        REQUIRE(mem.peek_bank(3, 0x0000) == 0x77);
        REQUIRE(mem.peek_bank(7, 0x0000) == 0x77);
        REQUIRE(mem.peek_bank(11, 0x0000) == 0x77);
        REQUIRE(mem.peek_bank(15, 0x0000) == 0x77);
    }

    SECTION("read_bank respects aliasing") {
        REQUIRE(mem.read_bank(3, 0x0000) == 0x77);
        REQUIRE(mem.read_bank(15, 0x0000) == 0x77);
    }

    SECTION("write_bank to RAM respects aliasing") {
        mem.configure_socket(0, SlotType::Ram);

        mem.write_bank(0, 0x1000, 0xEE);
        REQUIRE(mem.read_bank(4, 0x1000) == 0xEE);
        REQUIRE(mem.read_bank(8, 0x1000) == 0xEE);
    }
}

TEST_CASE("AliasedBankedMemory is_bank_populated", "[aliased_memory][populated]") {
    AliasedBankedMemory mem;

    SECTION("Empty sockets report unpopulated for all aliases") {
        REQUIRE_FALSE(mem.is_bank_populated(0));
        REQUIRE_FALSE(mem.is_bank_populated(4));
        REQUIRE_FALSE(mem.is_bank_populated(8));
        REQUIRE_FALSE(mem.is_bank_populated(12));
    }

    SECTION("Loaded ROM reports populated for all aliases") {
        mem.configure_socket(3, SlotType::Rom);
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        mem.load_rom_to_socket(3, data.data(), data.size());

        REQUIRE(mem.is_bank_populated(3));
        REQUIRE(mem.is_bank_populated(7));
        REQUIRE(mem.is_bank_populated(11));
        REQUIRE(mem.is_bank_populated(15));
    }
}

TEST_CASE("AliasedBankedMemory socket metadata", "[aliased_memory][metadata]") {
    AliasedBankedMemory mem;

    SECTION("Socket names are accessible") {
        REQUIRE(AliasedBankedMemory::socket_names[0] == "IC52");
        REQUIRE(AliasedBankedMemory::socket_names[1] == "IC88");
        REQUIRE(AliasedBankedMemory::socket_names[2] == "IC100");
        REQUIRE(AliasedBankedMemory::socket_names[3] == "IC101");
    }

    SECTION("Can set and get socket image name") {
        mem.set_socket_image_name(3, "bbc-basic_2.rom");
        REQUIRE(mem.socket_image_name(3) == "bbc-basic_2.rom");
    }

    SECTION("Clear socket clears image name too") {
        mem.configure_socket(3, SlotType::Rom);
        mem.set_socket_image_name(3, "test.rom");
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        mem.load_rom_to_socket(3, data.data(), data.size());

        mem.clear_socket(3);

        REQUIRE(mem.socket_image_name(3).empty());
        // Content should be cleared (0xFF for ROM type)
        mem.select_bank(15);
        REQUIRE(mem.read(0x0000) == 0xFF);
    }
}

TEST_CASE("AliasedBankedMemory typical BBC Model B configuration", "[aliased_memory][bbc]") {
    AliasedBankedMemory mem;

    // Set up typical BBC Model B configuration:
    // Socket 3 (IC101) = BASIC ROM
    // Socket 1 (IC88) = DFS ROM
    // Socket 0 (IC52) = Empty
    // Socket 2 (IC100) = Empty

    mem.configure_socket(AliasedBankedMemory::SOCKET_IC101, SlotType::Rom);
    std::array<uint8_t, 16384> basic_data;
    basic_data.fill(0xBA);  // BASIC signature
    mem.load_rom_to_socket(AliasedBankedMemory::SOCKET_IC101, basic_data.data(), basic_data.size());
    mem.set_socket_image_name(AliasedBankedMemory::SOCKET_IC101, "bbc-basic_2.rom");

    mem.configure_socket(AliasedBankedMemory::SOCKET_IC88, SlotType::Rom);
    std::array<uint8_t, 16384> dfs_data;
    dfs_data.fill(0xDF);  // DFS signature
    mem.load_rom_to_socket(AliasedBankedMemory::SOCKET_IC88, dfs_data.data(), dfs_data.size());
    mem.set_socket_image_name(AliasedBankedMemory::SOCKET_IC88, "acorn-dfs_1_00.rom");

    SECTION("BASIC visible at slot 15 (where MOS looks for language ROM)") {
        mem.select_bank(15);
        REQUIRE(mem.read(0x0000) == 0xBA);
        REQUIRE(mem.socket_image_name(AliasedBankedMemory::SOCKET_IC101) == "bbc-basic_2.rom");
    }

    SECTION("DFS visible at slots 1, 5, 9, 13") {
        mem.select_bank(13);
        REQUIRE(mem.read(0x0000) == 0xDF);

        mem.select_bank(1);
        REQUIRE(mem.read(0x0000) == 0xDF);
    }

    SECTION("Empty sockets at 0, 2, 4, 6, 8, 10, 12, 14") {
        mem.select_bank(0);
        REQUIRE(mem.read(0x0000) == 0xFF);

        mem.select_bank(2);
        REQUIRE(mem.read(0x0000) == 0xFF);

        mem.select_bank(14);
        REQUIRE(mem.read(0x0000) == 0xFF);
    }

    SECTION("BASIC also visible at aliased slots 3, 7, 11") {
        mem.select_bank(3);
        REQUIRE(mem.read(0x0000) == 0xBA);

        mem.select_bank(7);
        REQUIRE(mem.read(0x0000) == 0xBA);

        mem.select_bank(11);
        REQUIRE(mem.read(0x0000) == 0xBA);
    }
}
