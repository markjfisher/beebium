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

// test_boot_tube.cpp
//
// Integration test for BBC Model B boot with a 65C02 second processor.
//
// This is the full-stack integration test: a host Model B and a parasite
// 65C02 share a TubeShared region, both run their real boot ROMs, and
// the test verifies that the parasite's banner ("Acorn TUBE 6502 64K")
// appears on the host's MODE 7 screen.
//
// The boot sequence:
//   1. Host resets, MOS starts initialisation
//   2. Parasite boots from its 2 KB client ROM, fills R1 P-to-H FIFO
//      with the banner string (24 bytes)
//   3. Host MOS writes &81 to &FEE0 (set Q), reads back, detects Tube ULA
//   4. Host MOS issues service call &FF -- DNFS ROM handles this and
//      sets the Tube presence flag at &027A
//   5. Host MOS reads the banner from R1 and prints it on screen
//   6. Host MOS attempts language transfer via R2/R4 (parasite responds)
//
// The DNFS ROM is required because MOS issues service call &FF after
// detecting Tube hardware. Without a ROM that claims this call, the
// Tube presence flag is never set, and MOS ignores the Tube.
//
// Host and parasite are interleaved on the same thread, stepping in
// chunks that approximate the 2 MHz / 3 MHz clock ratio.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeShared.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_econet_helpers.hpp"

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

#ifndef BEEBIUM_TEST_ROM_DIR
#error "BEEBIUM_TEST_ROM_DIR must be defined"
#endif

using namespace beebium;
using namespace beebium::test;

namespace {

static constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr const char* DNFS_ROM_FILENAME = "acorn-dnfs_3_02.rom";
static constexpr size_t TUBE_ROM_SIZE = 2048;

bool tube_rom_available() {
    return std::filesystem::exists(
        std::filesystem::path(BEEBIUM_ROM_DIR) / TUBE_ROM_FILENAME);
}

bool dnfs_rom_available() {
    return std::filesystem::exists(
               std::filesystem::path(BEEBIUM_ROM_DIR) / DNFS_ROM_FILENAME) ||
           std::filesystem::exists(
               std::filesystem::path(BEEBIUM_TEST_ROM_DIR) / DNFS_ROM_FILENAME);
}

std::array<uint8_t, TUBE_ROM_SIZE> load_tube_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_ROM_DIR) / TUBE_ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());

    std::array<uint8_t, TUBE_ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), TUBE_ROM_SIZE);
    REQUIRE(file.gcount() == static_cast<std::streamsize>(TUBE_ROM_SIZE));
    return rom;
}

// Set up a Model B with MOS + BASIC + DNFS (which includes Tube Host code).
// DNFS handles service call &FF, required for MOS Tube initialisation.
void setup_tube_machine(ModelB& machine) {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    // Load DNFS into slot 13 (standard DFS position).
    auto dnfs_filepath = rom_dirpath / DNFS_ROM_FILENAME;
    if (!std::filesystem::exists(dnfs_filepath)) {
        dnfs_filepath = std::filesystem::path(BEEBIUM_TEST_ROM_DIR) / DNFS_ROM_FILENAME;
    }
    auto dnfs = load_rom(dnfs_filepath);
    machine.memory().load_sideways_rom(13, dnfs.data(), dnfs.size());
}

// Interleave host and parasite execution.
//
// Approximates the 2 MHz / 3 MHz clock ratio by stepping the host for
// host_batch instructions, then the parasite for parasite_batch instructions,
// repeated for the given number of rounds.
void interleaved_boot(ModelB& host, ParasiteRunner& parasite,
                      int rounds, int host_batch, int parasite_batch)
{
    for (int round = 0; round < rounds; ++round) {
        for (int i = 0; i < host_batch; ++i) {
            host.step_instruction();
        }
        for (int i = 0; i < parasite_batch; ++i) {
            parasite.step_instruction();
        }
    }
}

}  // namespace

// =============================================================================
// The coup de theatre: full host + parasite boot
// =============================================================================

TEST_CASE("Model B with 65C02 second processor boots with Tube banner",
          "[boot][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    if (!dnfs_rom_available()) SKIP("DNFS ROM not available");

    // --- Shared memory (in-process, stack allocated) ---
    TubeShared shared;
    shared.init();

    // --- Host setup ---
    ModelB machine;
    setup_tube_machine(machine);

    // Enable Tube socket with shared memory BEFORE reset, so MOS sees it
    machine.state().memory.tube_socket.enable(&shared);

    // Reset the host (this clears shared memory and sends a lifecycle
    // Reset command via TubeHostPort::reset)
    machine.reset();

    // Clear the lifecycle Reset command -- we manage the parasite ourselves
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::None),
        std::memory_order_release);

    // --- Parasite setup ---
    auto tube_rom = load_tube_rom();
    ParasiteRunner parasite(&shared, tube_rom);
    parasite.reset();

    // --- Interleaved boot ---
    // Host at 2 MHz, parasite at 3 MHz. Step ratio ~2:3.
    // 3500 rounds * 1000 instructions = 3.5M host instructions
    // 3500 rounds * 1500 instructions = 5.25M parasite instructions
    //
    // The parasite boot completes in ~30K cycles. After that it spins
    // in WaitByte polling R2 for host data. The host MOS needs ~200K
    // instructions to reach Tube detection, then reads the banner.
    interleaved_boot(machine, parasite, 3500, 1000, 1500);

    // --- Verify screen ---
    INFO("Screen:\n" << dump_screen(machine));

    // The Tube banner replaces the normal "BBC Computer 32K" header
    CHECK(screen_contains(machine, "Acorn TUBE 6502 64K"));

    // The normal "BBC Computer 32K" line should NOT appear
    CHECK_FALSE(screen_contains(machine, "BBC Computer 32K"));
}

TEST_CASE("Model B with Tube shows 64K memory (not 32K)",
          "[boot][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    if (!dnfs_rom_available()) SKIP("DNFS ROM not available");

    TubeShared shared;
    shared.init();

    ModelB machine;
    setup_tube_machine(machine);

    machine.state().memory.tube_socket.enable(&shared);
    machine.reset();

    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::None),
        std::memory_order_release);

    auto tube_rom = load_tube_rom();
    ParasiteRunner parasite(&shared, tube_rom);
    parasite.reset();

    interleaved_boot(machine, parasite, 3500, 1000, 1500);

    INFO("Screen:\n" << dump_screen(machine));

    // The banner includes "64K" (the parasite's memory size)
    CHECK(screen_contains(machine, "64K"));
}

TEST_CASE("Model B without Tube boots normally",
          "[boot][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");

    ModelB machine;
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    machine.reset();
    for (uint64_t i = 0; i < 3'500'000; ++i) {
        machine.step_instruction();
    }

    INFO("Screen:\n" << dump_screen(machine));

    CHECK(screen_contains(machine, "BBC Computer 32K"));
    CHECK(screen_contains(machine, "BASIC"));
    CHECK_FALSE(screen_contains(machine, "Acorn TUBE"));
}
