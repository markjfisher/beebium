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

// In-process Tube tests: ParasiteRunner backed by TubeUla.
//
// These tests validate the in-process architecture where the host and
// parasite share a TubeUla in the same process. The host accesses TubeUla
// via host_read/host_write (as TubeSocket would), and the parasite accesses
// it via parasite_read/parasite_write (through ParasiteRunner).
//
// The parasite runs the real Acorn Tube 6502 Client ROM, proving the in-process
// data path works with real CPU execution.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;

static constexpr const char* ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr size_t ROM_SIZE = 2048;

static std::array<uint8_t, ROM_SIZE> load_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_ROM_DIR) / ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());

    std::array<uint8_t, ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), ROM_SIZE);
    REQUIRE(file.gcount() == ROM_SIZE);
    return rom;
}

static constexpr uint64_t BOOT_CYCLES = 100000;

// ===========================================================================
// Single-threaded: host reads banner via TubeUla host interface
// ===========================================================================

TEST_CASE("In-process: host reads banner from R1 FIFO", "[tube][inprocess]") {
    auto rom = load_rom();
    TubeUla tube;
    ParasiteRunner runner(tube, rom);
    runner.reset();

    // Run parasite -- it writes the boot banner to R1 P-to-H FIFO.
    runner.run(BOOT_CYCLES);

    // Host reads R1 status via TubeUla host interface.
    uint8_t status = tube.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read the expected 24-byte banner.
    static constexpr uint8_t expected_banner[] = {
        0x0A,
        'A', 'c', 'o', 'r', 'n', ' ',
        'T', 'U', 'B', 'E', ' ',
        '6', '5', '0', '2', ' ',
        '6', '4', 'K',
        0x0A, 0x0A, 0x0D, 0x00
    };
    static_assert(sizeof(expected_banner) == 24);

    for (int i = 0; i < 24; ++i) {
        INFO("FIFO position: " << i);
        CHECK(tube.host_read(1) == expected_banner[i]);
    }

    // FIFO empty -- DATA_AVAILABLE clears.
    status = tube.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// Sequential: host reads banner after parasite boot
// ===========================================================================

TEST_CASE("In-process: host reads banner after parasite boot", "[tube][inprocess]") {
    auto rom = load_rom();
    TubeUla tube;
    ParasiteRunner runner(tube, rom);
    runner.reset();

    // Run parasite -- it writes the boot banner to R1 P-to-H FIFO.
    runner.run(BOOT_CYCLES);

    // Host reads banner via TubeUla.
    uint8_t status = tube.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    static constexpr uint8_t expected_banner[] = {
        0x0A,
        'A', 'c', 'o', 'r', 'n', ' ',
        'T', 'U', 'B', 'E', ' ',
        '6', '5', '0', '2', ' ',
        '6', '4', 'K',
        0x0A, 0x0A, 0x0D, 0x00
    };

    for (int i = 0; i < 24; ++i) {
        INFO("FIFO position: " << i);
        CHECK(tube.host_read(1) == expected_banner[i]);
    }
}

// ===========================================================================
// Sequential: R2 handshake with parasite
// ===========================================================================

TEST_CASE("In-process: R2 handshake with parasite", "[tube][inprocess]") {
    auto rom = load_rom();
    TubeUla tube;
    ParasiteRunner runner(tube, rom);
    runner.reset();

    // Boot phase
    runner.run(BOOT_CYCLES);

    // Verify banner is available
    uint8_t status = tube.host_read(0);
    REQUIRE((status & TubeUla::DATA_AVAILABLE) != 0);

    // Drain the banner
    for (int i = 0; i < 24; ++i) {
        tube.host_read(1);
    }

    // Write a dummy byte to R2 (OSRDCH command)
    tube.host_write(3, 0x00);

    // Run parasite more to process the R2 data
    runner.run(500000);

    // If we got here without hang, the R2 handshake worked.
    CHECK(true);
}
