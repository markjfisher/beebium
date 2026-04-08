// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Tests for the parasite execution runner.
//
// ParasiteRunner owns the parasite emulation engine: CPU, memory map,
// Tube port, and execution loop. It is the parasite's analogue of
// Machine<Hardware> on the host side.
//
// These tests verify:
//   - Construction and ROM loading
//   - Execution via run() and step_instruction()
//   - Pause/resume for debugger integration
//   - Clean shutdown coordination

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <thread>

using namespace beebium;

// Helper: create a 2 KB ROM with a known reset vector and NOP fill.
static std::array<uint8_t, 2048> make_nop_rom(uint16_t entry = 0xF800) {
    std::array<uint8_t, 2048> rom{};
    rom.fill(0xEA);  // NOP
    // Reset vector at ROM offset 0x7FC-0x7FD (maps to &FFFC-&FFFD)
    rom[0x7FC] = static_cast<uint8_t>(entry & 0xFF);
    rom[0x7FD] = static_cast<uint8_t>(entry >> 8);
    // IRQ vector -> &F900 (ROM offset 0x100), with RTI
    rom[0x7FE] = 0x00;
    rom[0x7FF] = 0xF9;
    rom[0x100] = 0x40;  // RTI
    // NMI vector -> &F980 (ROM offset 0x180), with RTI
    rom[0x7FA] = 0x80;
    rom[0x7FB] = 0xF9;
    rom[0x180] = 0x40;  // RTI
    return rom;
}

// ===========================================================================
// Construction and initial state
// ===========================================================================

TEST_CASE("ParasiteRunner construction with ROM", "[parasite][runner]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);

    CHECK(runner.cycle_count() == 0);
    CHECK(runner.memory_map().boot_mode());
}

TEST_CASE("ParasiteRunner reset initialises CPU at reset vector", "[parasite][runner]") {
    TubeUla tube;
    auto rom = make_nop_rom(0xF850);

    ParasiteRunner runner(tube, rom);
    runner.reset();

    // Execute reset sequence (7 cycles)
    uint64_t cycles = runner.step_instruction();
    CHECK(cycles == 7);
    CHECK(runner.cpu().abus.w == 0xF850);
}

// ===========================================================================
// Execution
// ===========================================================================

TEST_CASE("ParasiteRunner run executes cycles", "[parasite][runner][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);
    runner.reset();

    runner.run(100);
    CHECK(runner.cycle_count() == 100);
}

TEST_CASE("ParasiteRunner step_instruction returns cycle count", "[parasite][runner][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);
    runner.reset();

    uint64_t reset_cycles = runner.step_instruction();
    CHECK(reset_cycles == 7);

    uint64_t nop_cycles = runner.step_instruction();
    CHECK(nop_cycles == 2);
}

// ===========================================================================
// Pause/resume (debugger support)
// ===========================================================================

TEST_CASE("ParasiteRunner pause stops execution", "[parasite][runner][debug]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);
    runner.reset();

    runner.pause();
    CHECK(runner.is_paused());

    // run() returns immediately when paused (single-threaded: no blocking)
    auto cycle_before = runner.cycle_count();
    runner.run(1000);
    CHECK(runner.cycle_count() == cycle_before);

    // Resume and run
    runner.resume();
    CHECK_FALSE(runner.is_paused());
    runner.run(1000);
    CHECK(runner.cycle_count() > cycle_before);
}

// ===========================================================================
// Component access
// ===========================================================================

TEST_CASE("ParasiteRunner provides access to components", "[parasite][runner]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);

    // CPU access
    CHECK(runner.cpu().config == &M6502_rockwell65c02_config);

    // Memory map access
    CHECK(runner.memory_map().boot_mode());

    // Tube port access
    CHECK_FALSE(runner.tube_port().pirq());

    // Const access
    const ParasiteRunner& crunner = runner;
    CHECK(crunner.cycle_count() == 0);
    CHECK(crunner.cpu().config == &M6502_rockwell65c02_config);
}
