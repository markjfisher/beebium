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

// CE2023 divergence trace test.
//
// Boots Chuckie Egg 2023 with a Model B + 65C02 Tube, running host and
// parasite on separate threads (matching the production architecture).
// When the parasite hangs at the R1 poll loop ($09D1), the instruction
// and register traces are analysed to find the divergence point.
//
// The trace captures:
//   - Every parasite instruction (PC, A, X, Y, SP, P, opcode)
//   - Every host R1 data write (sequence number, value)
//   - Every parasite R1 status read (returned value)
//   - Every parasite R1 data read (sequence number, value)

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/disc/FileDiscImage.hpp>
#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeShared.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "test_econet_helpers.hpp"

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

using namespace beebium;
using namespace beebium::test;

namespace {

static constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr const char* DFS_ROM_FILENAME = "acorn-dfs_2_26.rom";
static constexpr const char* DISC_FILENAME = "chuckieEgg2023.ssd";
static constexpr size_t TUBE_ROM_SIZE = 2048;

// Decompressor poll-loop address (parasite hangs here)
static constexpr uint16_t DECOMP_R1_POLL = 0x09D1;
static constexpr uint16_t DECOMP_R1_BPL  = 0x09D4;
// First R4 ack at $0810 (decompressor entry)
static constexpr uint16_t DECOMP_ENTRY   = 0x0810;

bool files_available() {
    auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);

    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom")
        && std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom")
        && std::filesystem::exists(rom_dirpath / DFS_ROM_FILENAME)
        && std::filesystem::exists(rom_dirpath / TUBE_ROM_FILENAME)
        && std::filesystem::exists(assets_dirpath / "discs" / DISC_FILENAME);
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

}  // namespace

TEST_CASE("CE2023 divergence trace", "[tube][ce2023][trace]") {
    if (!files_available()) SKIP("Required ROMs or disc image not available");

    auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);

    // --- Shared memory ---
    TubeShared shared;
    shared.init();

    // --- Host setup: Model B with MOS + BASIC + DFS 2.26 (1770) + Tube ---
    ModelB machine;
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    auto dfs = load_rom(rom_dirpath / DFS_ROM_FILENAME);
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
    machine.memory().install_acorn_1770();

    // Insert CE2023 disc
    auto disc_filepath = assets_dirpath / "discs" / DISC_FILENAME;
    auto disc = FileDiscImage::load(disc_filepath);
    REQUIRE(disc != nullptr);
    machine.memory().disc_drive_0.insert(std::move(disc));

    // Enable Tube and video
    machine.state().memory.tube_socket.enable(&shared);
    machine.memory().enable_video_output();
    machine.memory().set_auto_boot(true);
    machine.reset();

    // Clear lifecycle command
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::None),
        std::memory_order_release);

    // --- Parasite setup ---
    auto tube_rom = load_tube_rom();
    ParasiteRunner parasite(&shared, tube_rom);
    parasite.reset();

    // --- Video (needed for screen text detection) ---
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // --- Enable traces before boot ---
    constexpr size_t INSTRUCTION_TRACE_CAPACITY = 4 * 1024 * 1024;  // 4M entries = 64MB
    constexpr size_t REGISTER_TRACE_CAPACITY = 1 * 1024 * 1024;     // 1M entries = 16MB

    parasite.parasite_cpu().trace().resize(INSTRUCTION_TRACE_CAPACITY);
    parasite.parasite_cpu().trace().set_enabled(true);

    parasite.tube_port().register_trace().resize(REGISTER_TRACE_CAPACITY);
    parasite.tube_port().register_trace().set_enabled(true);

    auto* host_port = machine.state().memory.tube_socket.tube_host_port();
    REQUIRE(host_port != nullptr);
    host_port->register_trace().resize(REGISTER_TRACE_CAPACITY);
    host_port->register_trace().set_enabled(true);

    // --- Boot with auto-boot (interleaved host 2 : parasite 3) ---
    // The decompressor runs, reads 702 R1 bytes, and either completes
    // (game loads) or hangs at $09D4 (BPL $09D1, R1 poll loop).
    // The 2:3 instruction ratio approximates the 2 MHz / 3 MHz clock
    // speeds.  This is NOT instruction-by-instruction stepping -- each
    // side runs a batch before yielding, so timing is non-deterministic
    // within each batch.
    INFO("Booting with auto-boot and traces enabled...");

    bool reached_decompressor = false;
    bool hung_at_poll = false;
    int poll_count = 0;
    constexpr int MAX_ROUNDS = 30'000'000;  // ~30 seconds emulated

    for (int round = 0; round < MAX_ROUNDS; ++round) {
        for (int i = 0; i < 2; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value())
                renderer.process(machine.memory().video_output.value());
        }
        for (int i = 0; i < 3; ++i)
            parasite.step_instruction();

        // Check parasite state periodically (every 1000 rounds)
        if ((round & 0x3FF) == 0) {
            uint16_t pc = parasite.pc();

            if (pc == DECOMP_ENTRY && !reached_decompressor) {
                reached_decompressor = true;
            }

            // Detect hang: stuck at BPL $09D1
            if (pc == DECOMP_R1_BPL) {
                ++poll_count;
                if (poll_count > 1000) {
                    hung_at_poll = true;
                    break;
                }
            } else {
                poll_count = 0;
            }
        }
    }

    // --- Analyse traces ---
    INFO("Screen at end:\n" << dump_screen(machine));
    INFO("Parasite PC: $" << std::hex << parasite.pc());
    INFO("Parasite cycles: " << std::dec << parasite.cycle_count());
    INFO("Reached decompressor: " << reached_decompressor);
    INFO("Hung at R1 poll: " << hung_at_poll);

    auto& itrace = parasite.parasite_cpu().trace();
    auto& ptrace = parasite.tube_port().register_trace();
    auto& htrace = host_port->register_trace();

    INFO("Instruction trace: " << itrace.total_count() << " entries ("
         << itrace.available() << " available)");
    INFO("Parasite register trace: " << ptrace.total_count() << " entries ("
         << ptrace.available() << " available)");
    INFO("Host register trace: " << htrace.total_count() << " entries ("
         << htrace.available() << " available)");

    // Count R1 data reads from parasite trace
    size_t r1_data_reads = 0;
    for (size_t i = 0; i < ptrace.available(); ++i) {
        auto& e = ptrace[i];
        if (e.offset == 1 && e.direction == 0)
            ++r1_data_reads;
    }
    INFO("R1 data reads in trace: " << r1_data_reads);

    // Count R1 data writes from host trace
    size_t r1_data_writes = 0;
    for (size_t i = 0; i < htrace.available(); ++i) {
        auto& e = htrace[i];
        if (e.offset == 1 && e.direction == 1)
            ++r1_data_writes;
    }
    INFO("R1 data writes in trace: " << r1_data_writes);

    // Dump last 50 parasite instructions
    INFO("Last 50 parasite instructions:");
    size_t avail = itrace.available();
    size_t start = avail > 50 ? avail - 50 : 0;
    for (size_t i = start; i < avail; ++i) {
        auto& e = itrace[i];
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "  cyc=%llu PC=$%04X A=$%02X X=$%02X Y=$%02X SP=$%02X P=$%02X op=$%02X",
                 static_cast<unsigned long long>(e.cycle),
                 e.pc, e.a, e.x, e.y, e.sp, e.p, e.opcode);
        INFO(buf);
    }

    // Dump R1 data reads and writes for correlation
    INFO("R1 data writes (host) -- last 20:");
    size_t hw_avail = htrace.available();
    size_t hw_start = hw_avail > 20 ? hw_avail - 20 : 0;
    for (size_t i = hw_start; i < hw_avail; ++i) {
        auto& e = htrace[i];
        if (e.offset == 1 && e.direction == 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  seq=%llu val=$%02X",
                     static_cast<unsigned long long>(e.cycle), e.value);
            INFO(buf);
        }
    }

    INFO("R1 data reads (parasite) -- last 20:");
    size_t pr_avail = ptrace.available();
    size_t pr_start = pr_avail > 20 ? pr_avail - 20 : 0;
    for (size_t i = pr_start; i < pr_avail; ++i) {
        auto& e = ptrace[i];
        if (e.offset == 1 && e.direction == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  seq=%llu val=$%02X",
                     static_cast<unsigned long long>(e.cycle), e.value);
            INFO(buf);
        }
    }

    // Transfer counter summary
    INFO("R1 H2P writes: " << shared.counters.r1_h2p_writes.load());
    INFO("R1 H2P reads: " << shared.counters.r1_h2p_reads.load());
    INFO("R3 H2P writes: " << shared.counters.r3_h2p_writes.load());
    INFO("R3 H2P reads: " << shared.counters.r3_h2p_reads.load());
    INFO("R4 P2H writes: " << shared.counters.r4_p2h_writes.load());
    INFO("R4 P2H reads: " << shared.counters.r4_p2h_reads.load());

    // The test succeeds if it completes (either hang detected or budget
    // exhausted).  The INFO output contains the trace data for analysis.
    // If the game loads successfully, hung_at_poll will be false.
    if (hung_at_poll) {
        WARN("CE2023 hung at R1 poll ($09D4) -- decompressor diverged");
    } else if (!reached_decompressor) {
        WARN("CE2023 did not reach decompressor entry -- boot failed");
    } else {
        WARN("CE2023 did not hang within cycle budget -- may have loaded!");
    }
}
