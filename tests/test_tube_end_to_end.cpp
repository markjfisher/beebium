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

// End-to-end tests for host/parasite communication via shared memory.
//
// These tests wire TubeHostPort (host side) and ParasiteRunner (parasite side)
// to the same TubeShared instance. The parasite runs the real Acorn Tube 6502
// Client ROM (v1.10), proving the full shared-memory data path works with
// real CPU execution.
//
// Unlike test_tube_shared_threads.cpp (which tests atomics directly) and
// test_parasite_boot.cpp (which tests the parasite side alone), these tests
// exercise the complete round-trip: host writes -> shared memory -> parasite
// CPU reads, and vice versa.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeShared.hpp>
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
// R1 P-to-H: Banner read via host port
// ===========================================================================

TEST_CASE("End-to-end: host reads banner from R1 FIFO", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // R1 status should show DATA_AVAILABLE (banner in FIFO)
    uint8_t status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read all 24 bytes through the host port
    static constexpr uint8_t expected_banner[] = {
        0x0A,                                           // LF
        'A', 'c', 'o', 'r', 'n', ' ',                  // "Acorn "
        'T', 'U', 'B', 'E', ' ',                        // "TUBE "
        '6', '5', '0', '2', ' ',                        // "6502 "
        '6', '4', 'K',                                  // "64K"
        0x0A, 0x0A, 0x0D, 0x00                          // LF LF CR NUL
    };
    static_assert(sizeof(expected_banner) == 24);

    for (int i = 0; i < 24; ++i) {
        INFO("FIFO position: " << i);
        CHECK(host.host_read(1) == expected_banner[i]);
    }

    // FIFO is now empty -- DATA_AVAILABLE should be clear
    status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// R1 status transitions as FIFO drains
// ===========================================================================

TEST_CASE("End-to-end: R1 status reflects FIFO occupancy", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // R1 status bit 7 (DATA_AVAILABLE) reflects P-to-H FIFO count > 0.
    // R1 status bit 6 (SPACE_AVAILABLE) reflects H-to-P latch empty (host can
    // write), which is independent of the FIFO and stays set throughout.

    // FIFO full (24 bytes) -- DATA_AVAILABLE set
    uint8_t status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Drain 23 bytes -- DATA_AVAILABLE still set (1 byte remains)
    for (int i = 0; i < 23; ++i) {
        host.host_read(1);
    }
    status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Drain last byte -- FIFO empty, DATA_AVAILABLE clears
    host.host_read(1);
    status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// R2 H-to-P: Host unblocks WaitByte
// ===========================================================================

TEST_CASE("End-to-end: host delivers R2 data to parasite", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // CPU should be spinning in WaitByte (F975-F979)
    uint16_t pc = runner.cpu().abus.w;
    REQUIRE(pc >= 0xF975);
    REQUIRE(pc <= 0xF979);

    // R2 status: SPACE_AVAILABLE (H-to-P latch empty, host can write)
    uint8_t r2_status = host.host_read(2);
    CHECK((r2_status & TubeUla::SPACE_AVAILABLE) != 0);

    // Host writes a byte to R2 data
    host.host_write(3, 0x42);

    // R2 status: no space (latch full, awaiting parasite read)
    r2_status = host.host_read(2);
    CHECK((r2_status & TubeUla::SPACE_AVAILABLE) == 0);

    // Run parasite -- WaitByte will see DATA_AVAILABLE and read the byte
    runner.run(200);

    // R2 H-to-P latch consumed -- SPACE_AVAILABLE returns
    r2_status = host.host_read(2);
    CHECK((r2_status & TubeUla::SPACE_AVAILABLE) != 0);
}

// ===========================================================================
// Control flags: host writes, status register reflects
// ===========================================================================

TEST_CASE("End-to-end: host control flag set/clear via status register", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // Initially all control flags are clear
    uint8_t status = host.host_read(0);
    CHECK((status & 0x3F) == 0);

    // Set Q and I flags (S bit = set mode)
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);

    status = host.host_read(0);
    CHECK((status & TubeUla::FLAG_Q) != 0);
    CHECK((status & TubeUla::FLAG_I) != 0);
    CHECK((status & TubeUla::FLAG_J) == 0);

    // Clear I flag (no S bit = clear mode)
    host.host_write(0, TubeUla::FLAG_I);

    status = host.host_read(0);
    CHECK((status & TubeUla::FLAG_Q) != 0);
    CHECK((status & TubeUla::FLAG_I) == 0);
}

// ===========================================================================
// Control flags: parasite sees flags set by host
// ===========================================================================

TEST_CASE("End-to-end: control flags visible to parasite via shared memory", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // Host sets M and V flags
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M | TubeUla::FLAG_V);

    // Parasite should see the same flags via shared memory
    uint8_t flags = shared.control_flags.load(std::memory_order_acquire);
    CHECK((flags & TubeUla::FLAG_M) != 0);
    CHECK((flags & TubeUla::FLAG_V) != 0);
    CHECK((flags & TubeUla::FLAG_Q) == 0);
}

// ===========================================================================
// Host reset: lifecycle mailbox triggers re-boot
// ===========================================================================

TEST_CASE("End-to-end: host reset re-runs boot sequence", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // Drain the banner
    for (int i = 0; i < 24; ++i) {
        host.host_read(1);
    }
    CHECK((host.host_read(0) & TubeUla::DATA_AVAILABLE) == 0);

    // Host initiates reset (clears registers, sets lifecycle command)
    host.reset();

    // Run parasite -- poll_mailbox picks up reset, re-boots
    runner.run(BOOT_CYCLES);

    // Banner should be back in the FIFO
    uint8_t status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Verify banner content (first 6 bytes: LF "Acorn")
    CHECK(host.host_read(1) == 0x0A);
    CHECK(host.host_read(1) == 'A');
    CHECK(host.host_read(1) == 'c');
    CHECK(host.host_read(1) == 'o');
    CHECK(host.host_read(1) == 'r');
    CHECK(host.host_read(1) == 'n');
}

// ===========================================================================
// Lifecycle acknowledgement
// ===========================================================================

TEST_CASE("End-to-end: parasite acknowledges host reset", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // Host initiates reset
    host.reset();

    // Run enough cycles for mailbox poll to fire (interval = 1024)
    runner.run(2048);

    // Parasite should have acknowledged the reset
    auto ack = static_cast<TubeLifecycleAck>(
        shared.parasite_ack.load(std::memory_order_acquire));
    CHECK(ack == TubeLifecycleAck::ResetDone);

    // Host command should have been consumed (cleared to None)
    auto cmd = static_cast<TubeLifecycleCommand>(
        shared.host_command.load(std::memory_order_acquire));
    CHECK(cmd == TubeLifecycleCommand::None);
}

// ===========================================================================
// R1 H-to-P: Host writes data for parasite
// ===========================================================================

TEST_CASE("End-to-end: host R1 H-to-P write reaches parasite memory map", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // Host writes to R1 H-to-P data register
    host.host_write(1, 0xAB);

    // The H-to-P latch should have data ready in shared memory
    CHECK(shared.r1_h2p.ready.load(std::memory_order_acquire) != 0);
    CHECK(shared.r1_h2p.value.load(std::memory_order_acquire) == 0xAB);

    // R1 status from host: SPACE_AVAILABLE should be clear (latch full)
    uint8_t status = host.host_read(0);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

// ===========================================================================
// Soft reset via T flag: clears registers but preserves control flags
// ===========================================================================

TEST_CASE("End-to-end: soft reset via T flag clears FIFO but keeps flags", "[tube][e2e]") {
    auto rom = load_rom();
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    ParasiteRunner runner(&shared, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // Set some control flags
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_J);

    // FIFO has banner data
    CHECK((host.host_read(0) & TubeUla::DATA_AVAILABLE) != 0);

    // Trigger soft reset via T flag (with S for set mode)
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);

    // FIFO should be cleared
    CHECK((host.host_read(0) & TubeUla::DATA_AVAILABLE) == 0);

    // Control flags Q and J should still be set (T is not stored)
    uint8_t status = host.host_read(0);
    CHECK((status & TubeUla::FLAG_Q) != 0);
    CHECK((status & TubeUla::FLAG_J) != 0);
}
