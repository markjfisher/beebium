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

// test_mode7_helpers.hpp
//
// Shared test infrastructure for MODE 7 BASIC integration tests.
// Provides ROM loading, machine initialization, and boot-to-BASIC functionality.

#ifndef BEEBIUM_TEST_MODE7_HELPERS_HPP
#define BEEBIUM_TEST_MODE7_HELPERS_HPP

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

namespace beebium::test {

// ============================================================================
// ROM Configuration Traits
// ============================================================================

template<typename MachineType>
struct RomConfig;

template<>
struct RomConfig<ModelB> {
    static constexpr const char* os_rom_filename = "acorn-mos_1_20.rom";
    static constexpr const char* basic_rom_filename = "bbc-basic_2.rom";
};

template<>
struct RomConfig<ModelBPlus> {
    static constexpr const char* os_rom_filename = "acorn-mos_2_0.rom";
    static constexpr const char* basic_rom_filename = "bbc-basic_2.rom";
};

// ============================================================================
// ROM Loading Utilities
// ============================================================================

#ifdef BEEBIUM_ROM_DIR

inline std::vector<uint8_t> load_rom_file(const std::filesystem::path& rom_dirpath,
                                           const char* filename) {
    auto filepath = rom_dirpath / filename;
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return {};  // Return empty vector if file not found
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

template<typename MachineType>
bool roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / RomConfig<MachineType>::os_rom_filename) &&
           std::filesystem::exists(rom_dirpath / RomConfig<MachineType>::basic_rom_filename);
}

#endif

// ============================================================================
// MODE 7 Boot Helper
// ============================================================================

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
    for (int i = 0; i < 300'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    return true;
}

// ============================================================================
// MODE 7 BASIC Test Context (RAII helper)
// ============================================================================

// RAII helper that sets up everything needed for a MODE 7 BASIC test:
// - Loads ROMs
// - Creates machine
// - Sets up framebuffer and renderer
// - Boots to BASIC prompt
//
// Usage:
//   Mode7TestContext<ModelB> ctx;
//   REQUIRE(ctx.booted);
//   type_string_with_shift(ctx.machine, ctx.renderer, "PRINT 42\r");
//
template<typename MachineType>
struct Mode7TestContext {
    MachineType machine;
    HeapFrameAllocator allocator;
    FrameBuffer framebuffer;
    FrameRenderer renderer;
    bool booted = false;

#ifdef BEEBIUM_ROM_DIR
    Mode7TestContext()
        : framebuffer(&allocator, 640, 512),
          renderer(&framebuffer)
    {
        const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);

        // Load ROMs
        auto os_rom = load_rom_file(rom_dirpath, RomConfig<MachineType>::os_rom_filename);
        auto basic_rom = load_rom_file(rom_dirpath, RomConfig<MachineType>::basic_rom_filename);

        if (os_rom.empty() || basic_rom.empty()) {
            return;  // ROM loading failed, booted remains false
        }

        // Initialize machine
        machine.memory().load_mos(os_rom.data(), os_rom.size());
        machine.memory().load_basic(basic_rom.data(), basic_rom.size());
        machine.memory().enable_video_output();

        // Boot to BASIC
        booted = boot_to_mode7_basic(machine, renderer);
    }

    // Convenience accessors that match test patterns
    MachineType& get_machine() { return machine; }
    FrameRenderer& get_renderer() { return renderer; }

    // Implicit conversion to bool for easy checking
    explicit operator bool() const { return booted; }
#else
    Mode7TestContext()
        : framebuffer(&allocator, 640, 512),
          renderer(&framebuffer)
    {
        // BEEBIUM_ROM_DIR not defined, tests will be skipped
    }
#endif
};

} // namespace beebium::test

#endif // BEEBIUM_TEST_MODE7_HELPERS_HPP
