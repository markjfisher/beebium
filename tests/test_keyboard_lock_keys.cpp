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

// test_keyboard_lock_keys.cpp
//
// Tests for Caps Lock and Shift Lock key behavior in the emulated BBC Micro.
// These tests verify:
// - Initial LED state after MOS boot
// - Lock key toggle behavior (LED changes)
// - Effect on typed characters (case and symbols)

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
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        // Check startUpOptions at $028F after initial boot period
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

// ============================================================================
// Test 1: LED Initial State After MOS Boot
// MOS boots with Caps Lock ON, Shift Lock OFF (standard BBC Micro behavior)
// ============================================================================

TEMPLATE_TEST_CASE("LED initial state after MOS boot", "[keyboard][locks][boot]",
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

    INFO("Caps Lock LED: " << caps_lock_led_on(machine));
    INFO("Shift Lock LED: " << shift_lock_led_on(machine));

    // Standard BBC Micro boot state: Caps Lock ON, Shift Lock OFF
    CHECK(caps_lock_led_on(machine) == true);
    CHECK(shift_lock_led_on(machine) == false);
}

// ============================================================================
// Test 2: Caps Lock Key Toggles LED
// ============================================================================

TEMPLATE_TEST_CASE("Caps Lock key toggles LED", "[keyboard][locks][capslock]",
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

    // Record initial state (don't assert - just verify toggling works)
    bool initial_caps = caps_lock_led_on(machine);
    INFO("Initial Caps Lock LED: " << initial_caps);

    // Toggle Caps Lock - should flip state
    toggle_caps_lock(machine, renderer);
    bool after_first_toggle = caps_lock_led_on(machine);
    INFO("After first toggle: " << after_first_toggle);
    CHECK(after_first_toggle != initial_caps);

    // Toggle again - should return to initial state
    toggle_caps_lock(machine, renderer);
    bool after_second_toggle = caps_lock_led_on(machine);
    INFO("After second toggle: " << after_second_toggle);
    CHECK(after_second_toggle == initial_caps);
}

// ============================================================================
// Test 3: Shift Lock Key Toggles LED
// ============================================================================

TEMPLATE_TEST_CASE("Shift Lock key toggles LED", "[keyboard][locks][shiftlock]",
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

    // Record initial state (don't assert - just verify toggling works)
    bool initial_shift_lock = shift_lock_led_on(machine);
    INFO("Initial Shift Lock LED: " << initial_shift_lock);

    // Toggle Shift Lock - should flip state
    toggle_shift_lock(machine, renderer);
    bool after_first_toggle = shift_lock_led_on(machine);
    INFO("After first toggle: " << after_first_toggle);
    CHECK(after_first_toggle != initial_shift_lock);

    // Toggle again - should return to initial state
    toggle_shift_lock(machine, renderer);
    bool after_second_toggle = shift_lock_led_on(machine);
    INFO("After second toggle: " << after_second_toggle);
    CHECK(after_second_toggle == initial_shift_lock);
}

// ============================================================================
// Test 4: Caps Lock Affects Letter Case
// NOTE: This test explicitly sets lock states to avoid depending on boot defaults
// ============================================================================

TEMPLATE_TEST_CASE("Caps Lock affects typed letters", "[keyboard][locks][typing][capslock]",
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

    // Debug: Show raw latch value immediately after boot (before any toggles)
    // This value is set ONLY by MOS ROM execution, not by test code
    // Note: LEDs are active-low (bit clear = LED on)
    uint8_t boot_latch = machine.memory().addressable_latch.value;
    WARN("Raw IC32 latch value after boot: 0x" << std::hex << (int)boot_latch);
    WARN("  Boot Caps Lock LED (bit 6, active-low): " << ((boot_latch & 0x40) ? "OFF" : "ON"));
    WARN("  Boot Shift Lock LED (bit 7, active-low): " << ((boot_latch & 0x80) ? "OFF" : "ON"));

    // First, ensure Shift Lock is OFF (toggle if needed)
    if (shift_lock_led_on(machine)) {
        WARN("Toggling Shift Lock to OFF...");
        toggle_shift_lock(machine, renderer);
        uint8_t latch_after_sl = machine.memory().addressable_latch.value;
        WARN("  Latch after Shift Lock toggle: 0x" << std::hex << (int)latch_after_sl);
    }
    CHECK(shift_lock_led_on(machine) == false);

    // Ensure Caps Lock is ON (toggle if needed)
    if (!caps_lock_led_on(machine)) {
        WARN("Toggling Caps Lock to ON...");
        toggle_caps_lock(machine, renderer);
        uint8_t latch_after_cl = machine.memory().addressable_latch.value;
        WARN("  Latch after Caps Lock toggle: 0x" << std::hex << (int)latch_after_cl);
    }
    CHECK(caps_lock_led_on(machine) == true);

    // Debug: Check MOS keyboard status bytes
    // $025A: Keyboard status (OSBYTE &CA)
    // Bit 4: Caps Lock (0=on, 1=off)
    // Bit 5: Shift Lock (0=on, 1=off)
    uint8_t kstat_258 = machine.read(0x0258);
    uint8_t kstat_259 = machine.read(0x0259);
    uint8_t kstat_25A = machine.read(0x025A);
    uint8_t kstat_25B = machine.read(0x025B);
    WARN("Keyboard state bytes before typing:");
    WARN("  $0258 = 0x" << std::hex << (int)kstat_258);
    WARN("  $0259 = 0x" << std::hex << (int)kstat_259);
    WARN("  $025A = 0x" << std::hex << (int)kstat_25A);
    WARN("  $025B = 0x" << std::hex << (int)kstat_25B);
    WARN("  Caps Lock via $025A: " << ((kstat_25A & 0x10) ? "OFF" : "ON"));
    WARN("  Shift Lock via $025A: " << ((kstat_25A & 0x20) ? "OFF" : "ON"));

    // Type "abc" - with Caps Lock ON, should appear as "ABC"
    type_string_with_shift(machine, renderer, "abc", 100000);
    run_cycles(machine, renderer, 50000);

    bool found_upper = find_string_on_screen(machine, "ABC");
    if (!found_upper) {
        INFO("Expected 'ABC' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found_upper);

    // Now toggle Caps Lock OFF
    toggle_caps_lock(machine, renderer);
    CHECK(caps_lock_led_on(machine) == false);

    // Debug: Check keyboard state after toggle
    uint8_t kstat_258_after = machine.read(0x0258);
    uint8_t kstat_259_after = machine.read(0x0259);
    uint8_t kstat_25A_after = machine.read(0x025A);
    uint8_t kstat_25B_after = machine.read(0x025B);
    WARN("Keyboard state bytes after toggle:");
    WARN("  $0258 = 0x" << std::hex << (int)kstat_258_after);
    WARN("  $0259 = 0x" << std::hex << (int)kstat_259_after);
    WARN("  $025A = 0x" << std::hex << (int)kstat_25A_after);
    WARN("  $025B = 0x" << std::hex << (int)kstat_25B_after);
    WARN("  Caps Lock via $025A: " << ((kstat_25A_after & 0x10) ? "OFF" : "ON"));
    WARN("  Shift Lock via $025A: " << ((kstat_25A_after & 0x20) ? "OFF" : "ON"));

    // Type "def" - with Caps Lock OFF, should appear as lowercase "def"
    type_string_with_shift(machine, renderer, "def", 100000);
    run_cycles(machine, renderer, 50000);

    // Debug: Print what's actually on screen
    WARN("After typing 'def' with Caps Lock OFF:");
    for (int row = 0; row < 10; ++row) {
        std::string screen_row = read_mode7_screen(machine, row, 0, 40);
        WARN("  Row " << row << ": [" << screen_row << "]");
    }

    bool found_lower = find_string_on_screen(machine, "def");
    if (!found_lower) {
        // Also check for uppercase in case Caps Lock didn't take effect
        bool found_upper_instead = find_string_on_screen(machine, "DEF");
        INFO("Expected 'def' not found. Found 'DEF' instead: " << found_upper_instead);
    }
    CHECK(found_lower);
}

// ============================================================================
// Test 5: Caps Lock Only Affects Letters, Not Numbers
// NOTE: This test explicitly sets lock states to avoid depending on boot defaults
// ============================================================================

TEMPLATE_TEST_CASE("Caps Lock only affects letters, not numbers", "[keyboard][locks][typing]",
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

    // Ensure Shift Lock is OFF
    if (shift_lock_led_on(machine)) {
        toggle_shift_lock(machine, renderer);
    }
    CHECK(shift_lock_led_on(machine) == false);

    // Ensure Caps Lock is ON
    if (!caps_lock_led_on(machine)) {
        toggle_caps_lock(machine, renderer);
    }
    CHECK(caps_lock_led_on(machine) == true);

    // Type "a1b2" with Caps Lock ON
    // Letters should be uppercase, numbers should remain digits
    type_string_with_shift(machine, renderer, "a1b2", 100000);
    run_cycles(machine, renderer, 50000);

    bool found = find_string_on_screen(machine, "A1B2");
    if (!found) {
        INFO("Expected 'A1B2' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

// ============================================================================
// Test 7: Shift Lock Affects Both Letters and Numbers
// NOTE: This test explicitly sets lock states to avoid depending on boot defaults
// ============================================================================

TEMPLATE_TEST_CASE("Shift Lock affects both letters and numbers", "[keyboard][locks][typing]",
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

    // Ensure Caps Lock is OFF
    if (caps_lock_led_on(machine)) {
        toggle_caps_lock(machine, renderer);
    }
    CHECK(caps_lock_led_on(machine) == false);

    // Ensure Shift Lock is ON
    if (!shift_lock_led_on(machine)) {
        toggle_shift_lock(machine, renderer);
    }
    CHECK(shift_lock_led_on(machine) == true);

    // Type "a1" with Shift Lock ON
    // Both should be shifted: 'a' -> 'A', '1' -> '!' (SHIFT+1 on BBC keyboard)
    type_string_with_shift(machine, renderer, "a1", 100000);
    run_cycles(machine, renderer, 50000);

    bool found = find_string_on_screen(machine, "A!");
    if (!found) {
        INFO("Expected 'A!' not found. Screen content:");
        dump_screen(machine);
    }
    CHECK(found);
}

#endif // BEEBIUM_ROM_DIR
