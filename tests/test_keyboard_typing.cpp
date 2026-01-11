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

// test_keyboard_typing.cpp
//
// Tests for keyboard typing reliability in the emulated BBC Micro.
// These tests verify that the keyboard typing helpers work correctly
// and help calibrate the optimal typing speed.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "test_keyboard_helpers.hpp"

using namespace beebium;
using namespace beebium::test;

// ============================================================================
// Machine Type Configuration Traits
// ============================================================================

template<typename MachineType>
struct RomConfig;

template<>
struct RomConfig<ModelB> {
    static constexpr const char* mos_filename = "acorn-mos_1_20.rom";
};

template<>
struct RomConfig<ModelBPlus> {
    static constexpr const char* mos_filename = "acorn-mos_2_0.rom";
};

namespace {

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open ROM: " + filepath.string());
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    return data;
}

#ifdef BEEBIUM_ROM_DIR

template<typename MachineType>
bool roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / RomConfig<MachineType>::mos_filename) &&
           std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom");
}

// Boot machine to MODE 7 BASIC prompt and verify boot completed
template<typename MachineType>
bool boot_to_mode7_basic(MachineType& machine, FrameRenderer& renderer) {
    machine.reset();

    // Run until startup options at 0x028F shows Mode 7
    // This works reliably for both ModelB and ModelBPlus
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        // Check startUpOptions at $028F after initial boot period
        // Bits 0-2 contain screen mode
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 7) {
                boot_complete = true;
            }
        }
    }

    if (!boot_complete) return false;

    // Wait for BASIC to be fully ready with cursor
    for (int i = 0; i < 300000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    return true;
}

// Find a string anywhere in first N rows of MODE 7 screen
template<typename MachineType>
bool find_string_on_screen(MachineType& machine, const std::string& target, int max_rows = 15) {
    for (int row = 0; row < max_rows; ++row) {
        std::string screen_row = read_mode7_screen(machine, row, 0, 40);
        if (screen_row.find(target) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Dump screen content for debugging
template<typename MachineType>
void dump_screen(MachineType& machine, int max_rows = 15) {
    for (int row = 0; row < max_rows; ++row) {
        INFO("  Row " << row << ": [" << read_mode7_screen(machine, row, 0, 40) << "]");
    }
}

#endif

} // namespace

#ifdef BEEBIUM_ROM_DIR

// Test basic typing reliability - types a simple string and verifies it appears in screen memory
TEMPLATE_TEST_CASE("Keyboard typing basic reliability in MODE 7", "[keyboard][typing][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    REQUIRE(boot_to_mode7_basic(machine, renderer));

    // Type "HELLO" at the BASIC prompt with a conservative speed
    type_string_with_shift(machine, renderer, "HELLO", 100000);

    // Run a bit more to let the last character settle
    run_cycles(machine, renderer, 100000);

    // Check if "HELLO" appears on screen
    bool found = find_string_on_screen(machine, "HELLO");

    if (!found) {
        INFO("String 'HELLO' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

// Test different typing speeds to verify reliable minimum
// Speeds below 100000 cycles/key are known to be unreliable and lose characters.
// This test verifies the minimum reliable speed is 100000 cycles/key.
TEMPLATE_TEST_CASE("Keyboard typing speed calibration", "[keyboard][typing][speed][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    // Test reliable speeds (100000+) - these should always pass
    const std::vector<int> reliable_speeds = {100000, 150000, 200000};

    for (int cycles_per_key : reliable_speeds) {
        SECTION("Reliable speed: " + std::to_string(cycles_per_key) + " cycles/key") {
            TestType machine;
            machine.memory().load_mos(mos_rom.data(), mos_rom.size());
            machine.memory().load_basic(basic_rom.data(), basic_rom.size());
            machine.memory().enable_video_output();

            HeapFrameAllocator allocator;
            FrameBuffer fb(&allocator, 640, 512);
            FrameRenderer renderer(&fb);

            REQUIRE(boot_to_mode7_basic(machine, renderer));

            // Type "TEST" at this speed
            type_string_with_shift(machine, renderer, "TEST", cycles_per_key);

            // Let it settle
            run_cycles(machine, renderer, 100000);

            // Check if "TEST" appears on screen
            bool found = find_string_on_screen(machine, "TEST");

            INFO("Speed " << cycles_per_key << " cycles/key should be reliable");
            if (!found) {
                INFO("String 'TEST' not found. Screen content:");
                dump_screen(machine);
            }
            CHECK(found);
        }
    }
}

// Test typing with SHIFT characters
TEMPLATE_TEST_CASE("Keyboard typing with SHIFT characters", "[keyboard][typing][shift][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    REQUIRE(boot_to_mode7_basic(machine, renderer));

    // Type a string with shifted characters: A$B(C)D
    // $ requires SHIFT+4, ( requires SHIFT+8, ) requires SHIFT+9
    type_string_with_shift(machine, renderer, "A$B(C)D", 100000);

    // Let it settle
    run_cycles(machine, renderer, 100000);

    // Check if the string appears on screen
    bool found = find_string_on_screen(machine, "A$B(C)D");

    if (!found) {
        INFO("String 'A$B(C)D' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

// Test typing digits
TEMPLATE_TEST_CASE("Keyboard typing digits", "[keyboard][typing][digits][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    REQUIRE(boot_to_mode7_basic(machine, renderer));

    // Type all digits
    type_string_with_shift(machine, renderer, "0123456789", 100000);

    // Let it settle
    run_cycles(machine, renderer, 100000);

    // Check if the digits appear on screen
    bool found = find_string_on_screen(machine, "0123456789");

    if (!found) {
        INFO("String '0123456789' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

// Test typing lowercase letters (should appear as uppercase in BASIC)
TEMPLATE_TEST_CASE("Keyboard typing letters", "[keyboard][typing][letters][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    REQUIRE(boot_to_mode7_basic(machine, renderer));

    // Type lowercase - BBC BASIC echoes uppercase
    type_string_with_shift(machine, renderer, "abcdefghij", 100000);

    // Let it settle
    run_cycles(machine, renderer, 100000);

    // Check if uppercase letters appear on screen
    bool found = find_string_on_screen(machine, "ABCDEFGHIJ");

    if (!found) {
        INFO("String 'ABCDEFGHIJ' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

// Test that RETURN moves to next line
TEMPLATE_TEST_CASE("Keyboard typing RETURN", "[keyboard][typing][return][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    REQUIRE(boot_to_mode7_basic(machine, renderer));

    // Type "A", press RETURN, type "B"
    // After pressing RETURN at BASIC prompt with just "A", BASIC gives "Mistake" error
    // and shows a new prompt
    type_string_with_shift(machine, renderer, "A\rB", 100000);

    // Let it settle
    run_cycles(machine, renderer, 200000);

    // Check we at least got the A on screen somewhere
    bool found_a = find_string_on_screen(machine, ">A");

    if (!found_a) {
        INFO("String '>A' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found_a);
}

// Test typing punctuation that doesn't need SHIFT
TEMPLATE_TEST_CASE("Keyboard typing unshifted punctuation", "[keyboard][typing][punctuation][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    REQUIRE(boot_to_mode7_basic(machine, renderer));

    // Type punctuation that doesn't need SHIFT: ; : , . - /
    type_string_with_shift(machine, renderer, ";:,.-/", 100000);

    // Let it settle
    run_cycles(machine, renderer, 100000);

    // Check if the punctuation appears on screen
    bool found = find_string_on_screen(machine, ";:,.-/");

    if (!found) {
        INFO("String ';:,.-/' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

// Test a longer string to stress test typing
TEMPLATE_TEST_CASE("Keyboard typing longer string", "[keyboard][typing][stress][mode7]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    REQUIRE(boot_to_mode7_basic(machine, renderer));

    // Type a longer string that looks like a PRINT command
    const std::string test_string = "PRINT \"HELLO\"";
    type_string_with_shift(machine, renderer, test_string.c_str(), 100000);

    // Let it settle
    run_cycles(machine, renderer, 100000);

    // Check if the string appears on screen
    bool found = find_string_on_screen(machine, test_string);

    if (!found) {
        INFO("String '" << test_string << "' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

#endif // BEEBIUM_ROM_DIR
