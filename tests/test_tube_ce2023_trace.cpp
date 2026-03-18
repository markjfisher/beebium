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
// Boots Chuckie Egg 2023 with a Model B + 65C02 Tube using interleaved
// execution.  When the parasite hangs at the R1 poll loop ($09D1), the
// instruction and register traces are analysed to find the divergence.
//
// The hang reproduces under interleaved 2:3 execution (same architecture
// as B2, MAME, and other failing emulators).  Working emulators (B-Em,
// jsbeeb) use tighter 1:1 coupling.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/disc/FileDiscImage.hpp>
#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeShared.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
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

// Decompressor addresses
static constexpr uint16_t DECOMP_R1_BPL  = 0x09D4;  // BPL $09D1 (hang point)
static constexpr uint16_t DECOMP_R1_READ = 0x09D6;  // LDA $FEF9 (R1 data read)
static constexpr uint16_t DECOMP_OUTPUT  = 0x0A00;  // STA ($2F)  (output write)

// First 20 expected output bytes from jsbeeb (the decompressor writes from $FC00)
static constexpr uint8_t JSBEEB_OUTPUT[] = {
    0x2C, 0xF8, 0xFE, 0x10, 0xFB, 0xAD, 0xF9, 0xFE,
    0x38, 0x6A, 0x85, 0x31, 0x68, 0x60, 0xA4, 0x2F,
    0xB1, 0x33, 0xA0, 0x00,
};

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

struct TestFixture {
    TubeShared shared;
    ModelB machine;
    std::unique_ptr<ParasiteRunner> parasite;
    HeapFrameAllocator allocator;
    FrameBuffer fb;
    FrameRenderer renderer;
    TubeHostPort* host_port = nullptr;

    TestFixture()
        : fb(&allocator, 640, 512)
        , renderer(&fb)
    {
        shared.init();

        auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
        auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);

        auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
        auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
        auto dfs = load_rom(rom_dirpath / DFS_ROM_FILENAME);
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
        machine.memory().install_acorn_1770();

        auto disc_filepath = assets_dirpath / "discs" / DISC_FILENAME;
        auto disc = FileDiscImage::load(disc_filepath);
        machine.memory().disc_drive_0.insert(std::move(disc));

        machine.state().memory.tube_socket.enable(&shared);
        machine.memory().enable_video_output();
        machine.memory().set_auto_boot(true);
        machine.reset();

        shared.host_command.store(
            static_cast<uint8_t>(TubeLifecycleCommand::None),
            std::memory_order_release);

        auto tube_rom = load_tube_rom();
        parasite = std::make_unique<ParasiteRunner>(&shared, tube_rom);
        parasite->reset();

        host_port = machine.state().memory.tube_socket.tube_host_port();
    }

    // Run interleaved with given batch sizes.
    // Returns true if hang detected, false if budget exhausted.
    bool run_until_hang(int host_batch, int parasite_batch, int max_rounds) {
        int poll_count = 0;

        for (int round = 0; round < max_rounds; ++round) {
            for (int i = 0; i < host_batch; ++i) {
                machine.step();
                if (machine.memory().video_output.has_value())
                    renderer.process(machine.memory().video_output.value());
            }
            for (int i = 0; i < parasite_batch; ++i)
                parasite->step_instruction();

            // Check for hang periodically
            if ((round & 0x3FF) == 0) {
                if (parasite->pc() == DECOMP_R1_BPL) {
                    if (++poll_count > 1000) return true;
                } else {
                    poll_count = 0;
                }
            }
        }
        return false;
    }

    void enable_traces() {
        parasite->parasite_cpu().trace().resize(4 * 1024 * 1024);
        parasite->parasite_cpu().trace().set_enabled(true);

        // Watch writes to $0CE6 (the divergent table entry)
        parasite->parasite_cpu().set_watch_write_addr(0x0CE6);

        parasite->tube_port().register_trace().resize(1 * 1024 * 1024);
        parasite->tube_port().register_trace().set_enabled(true);

        host_port->register_trace().resize(1 * 1024 * 1024);
        host_port->register_trace().set_enabled(true);
    }

    void print_analysis() {
        auto& itrace = parasite->parasite_cpu().trace();
        auto& ptrace = parasite->tube_port().register_trace();
        auto& htrace = host_port->register_trace();

        printf("\n=== CE2023 TRACE ANALYSIS ===\n");
        printf("Parasite PC: $%04X  Cycles: %llu\n",
               parasite->pc(), static_cast<unsigned long long>(parasite->cycle_count()));
        printf("Instruction trace: %llu total, %zu available\n",
               static_cast<unsigned long long>(itrace.total_count()), itrace.available());
        printf("Parasite register trace: %llu total, %zu available\n",
               static_cast<unsigned long long>(ptrace.total_count()), ptrace.available());
        printf("Host register trace: %llu total, %zu available\n",
               static_cast<unsigned long long>(htrace.total_count()), htrace.available());

        // Transfer counters
        printf("\nTransfer counters:\n");
        printf("  R1 H2P: w=%llu r=%llu\n",
               static_cast<unsigned long long>(shared.counters.r1_h2p_writes.load()),
               static_cast<unsigned long long>(shared.counters.r1_h2p_reads.load()));
        printf("  R3 H2P: w=%llu r=%llu\n",
               static_cast<unsigned long long>(shared.counters.r3_h2p_writes.load()),
               static_cast<unsigned long long>(shared.counters.r3_h2p_reads.load()));
        printf("  R4 P2H: w=%llu r=%llu\n",
               static_cast<unsigned long long>(shared.counters.r4_p2h_writes.load()),
               static_cast<unsigned long long>(shared.counters.r4_p2h_reads.load()));

        // R1 data read values from parasite trace
        printf("\nR1 data reads (parasite, all %zu available):\n", ptrace.available());
        int r1_count = 0;
        for (size_t i = 0; i < ptrace.available(); ++i) {
            auto& e = ptrace[i];
            if (e.offset == 1 && e.direction == 0) {
                if (r1_count < 20 || r1_count >= 690)
                    printf("  [%d] seq=%llu val=$%02X\n",
                           r1_count, static_cast<unsigned long long>(e.cycle), e.value);
                else if (r1_count == 20)
                    printf("  ...\n");
                ++r1_count;
            }
        }
        printf("  Total R1 data reads in trace: %d\n", r1_count);

        // R1 data write values from host trace
        printf("\nR1 data writes (host, all %zu available):\n", htrace.available());
        int hw_count = 0;
        for (size_t i = 0; i < htrace.available(); ++i) {
            auto& e = htrace[i];
            if (e.offset == 1 && e.direction == 1) {
                if (hw_count < 20 || hw_count >= 690)
                    printf("  [%d] seq=%llu val=$%02X\n",
                           hw_count, static_cast<unsigned long long>(e.cycle), e.value);
                else if (hw_count == 20)
                    printf("  ...\n");
                ++hw_count;
            }
        }
        printf("  Total R1 data writes in trace: %d\n", hw_count);

        // Save R1 data to file for standalone test
        if (hw_count == 702) {
            auto r1_filepath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "ce2023_r1_data.bin";
            std::ofstream r1_file(r1_filepath, std::ios::binary);
            for (size_t i = 0; i < htrace.available(); ++i) {
                auto& e = htrace[i];
                if (e.offset == 1 && e.direction == 1)
                    r1_file.write(reinterpret_cast<const char*>(&e.value), 1);
            }
            printf("  R1 data saved to: %s\n", r1_filepath.string().c_str());
        }

        // Decompressor output: check parasite RAM at $FC00+
        printf("\nDecompressor output ($FC00+, first 32 bytes):\n");
        printf("  Beebium: ");
        for (int i = 0; i < 32; ++i)
            printf("%02X ", parasite->memory_map().peek(0xFC00 + i));
        printf("\n");
        printf("  jsbeeb:  ");
        for (int i = 0; i < 20; ++i)
            printf("%02X ", JSBEEB_OUTPUT[i]);
        printf("...\n");

        // Find first output divergence
        int first_diff = -1;
        for (int i = 0; i < static_cast<int>(sizeof(JSBEEB_OUTPUT)); ++i) {
            if (parasite->memory_map().peek(0xFC00 + i) != JSBEEB_OUTPUT[i]) {
                first_diff = i;
                break;
            }
        }
        if (first_diff >= 0) {
            printf("\n  FIRST OUTPUT DIVERGENCE at $%04X (byte %d): "
                   "beebium=$%02X jsbeeb=$%02X\n",
                   0xFC00 + first_diff, first_diff,
                   parasite->memory_map().peek(0xFC00 + first_diff),
                   JSBEEB_OUTPUT[first_diff]);
        } else {
            printf("\n  First %zu output bytes MATCH jsbeeb\n", sizeof(JSBEEB_OUTPUT));
        }

        // Decompressor code at key addresses
        printf("\nDecompressor code:\n");
        printf("  $0990: ");
        for (int i = 0; i < 16; ++i) printf("%02X ", parasite->memory_map().peek(0x0990 + i));
        printf("\n  $09A0: ");
        for (int i = 0; i < 16; ++i) printf("%02X ", parasite->memory_map().peek(0x09A0 + i));
        printf("\n  $09C8: ");
        for (int i = 0; i < 32; ++i) printf("%02X ", parasite->memory_map().peek(0x09C8 + i));
        printf("\n  $09EF: ");
        for (int i = 0; i < 24; ++i) printf("%02X ", parasite->memory_map().peek(0x09EF + i));
        printf("\n");

        // The LDA abs,X at $0997 reads from base+X.  Show the base address.
        uint16_t table_base = parasite->memory_map().peek(0x0998)
                            | (parasite->memory_map().peek(0x0999) << 8);
        printf("\n  Table LDA base ($0998/$0999): $%04X\n", table_base);
        printf("  Table contents at base+$20..base+$30:\n    ");
        for (int i = 0x20; i < 0x30; ++i)
            printf("%02X ", parasite->memory_map().peek(table_base + i));
        printf("\n    ");
        for (int i = 0x30; i < 0x40; ++i)
            printf("%02X ", parasite->memory_map().peek(table_base + i));
        printf("\n");

        // Show what the table lookup SHOULD return for the first few X values
        // from the output trace
        printf("  Table lookups for first 9 output X values:\n");
        uint8_t test_x[] = {0x9B, 0x7E, 0x21, 0x34, 0x81, 0x5C, 0x7F, 0x21, 0x28};
        for (int i = 0; i < 9; ++i) {
            uint16_t addr = table_base + test_x[i];
            printf("    X=$%02X -> $%04X = $%02X\n",
                   test_x[i], addr, parasite->memory_map().peek(addr));
        }

        // Decompressor zero page state
        printf("\nDecompressor ZP state:\n");
        printf("  $2F/$30 (output ptr): $%02X%02X\n",
               parasite->memory_map().peek(0x30), parasite->memory_map().peek(0x2F));
        printf("  $31 (bit buffer): $%02X\n", parasite->memory_map().peek(0x31));
        printf("  $2C (source flag): $%02X\n", parasite->memory_map().peek(0x2C));
        printf("  $2D/$2E (mem ptr): $%02X%02X\n",
               parasite->memory_map().peek(0x2E), parasite->memory_map().peek(0x2D));
        printf("  $33/$34 (backref ptr): $%02X%02X\n",
               parasite->memory_map().peek(0x34), parasite->memory_map().peek(0x33));

        // Search instruction trace for output writes (STA ($2F) at $0A00)
        // and R1 data reads (LDA $FEF9 at $09D6).  This shows the
        // decompressor's execution from the emulator's perspective.
        printf("\nDecompressor events from instruction trace:\n");
        size_t avail = itrace.available();
        int output_count = 0;
        int r1_read_count = 0;
        for (size_t i = 0; i < avail; ++i) {
            auto& e = itrace[i];
            if (e.pc == DECOMP_OUTPUT && e.opcode == 0x92) {
                // STA ($2F) -- opcode $92 on 65C02 (STA (zp))
                // A = value being written, output addr is in ZP $2F/$30
                // We can't read ZP from the trace, but we have A.
                if (output_count < 20) {
                    printf("  OUTPUT[%d] cyc=%llu A=$%02X\n",
                           output_count, static_cast<unsigned long long>(e.cycle), e.a);
                }
                ++output_count;
            }
            if (e.pc == DECOMP_R1_READ && e.opcode == 0xAD) {
                // LDA $FEF9 -- reading R1 data
                ++r1_read_count;
            }
        }
        printf("  Total output writes in trace: %d\n", output_count);
        printf("  Total R1 reads (LDA $FEF9) in trace: %d\n", r1_read_count);

        // Search backwards for the decompressor code around the output
        // write.  Find the last few STA ($2F) and dump context around each.
        printf("\nContext around first 12 output writes:\n");
        output_count = 0;
        for (size_t i = 0; i < avail && output_count < 12; ++i) {
            auto& e = itrace[i];
            if (e.pc == DECOMP_OUTPUT && e.opcode == 0x92) {
                printf("  --- OUTPUT[%d] ---\n", output_count);
                // Dump 10 instructions before and 5 after
                size_t ctx_start = (i >= 10) ? i - 10 : 0;
                size_t ctx_end = (i + 5 < avail) ? i + 5 : avail;
                for (size_t j = ctx_start; j < ctx_end; ++j) {
                    auto& c = itrace[j];
                    printf("  %s cyc=%-10llu PC=$%04X A=$%02X X=$%02X Y=$%02X SP=$%02X P=$%02X op=$%02X\n",
                           (j == i) ? ">>" : "  ",
                           static_cast<unsigned long long>(c.cycle),
                           c.pc, c.a, c.x, c.y, c.sp, c.p, c.opcode);
                }
                ++output_count;
            }
        }

        // Search for watchpoint hits (writes to $0CE6, opcode=$FF in trace)
        printf("\nWrites to $0CE6 (watchpoint hits):\n");
        for (size_t i = 0; i < avail; ++i) {
            auto& e = itrace[i];
            if (e.opcode == 0xFF && e.pc == 0x0CE6) {
                // A = written value, find the preceding instruction
                printf("  WRITE $0CE6 = $%02X at cyc=%llu",
                       e.a, static_cast<unsigned long long>(e.cycle));
                // Show the instruction that caused the write (previous entry)
                if (i > 0) {
                    auto& prev = itrace[i - 1];
                    printf("  (from PC=$%04X op=$%02X A=$%02X X=$%02X Y=$%02X)",
                           prev.pc, prev.opcode, prev.a, prev.x, prev.y);
                }
                printf("\n");
                // Show 5 instructions before the write for context
                size_t ctx = (i >= 6) ? i - 6 : 0;
                for (size_t j = ctx; j < i; ++j) {
                    auto& c = itrace[j];
                    printf("    cyc=%-10llu PC=$%04X A=$%02X X=$%02X Y=$%02X op=$%02X\n",
                           static_cast<unsigned long long>(c.cycle),
                           c.pc, c.a, c.x, c.y, c.opcode);
                }
            }
        }

        // Dump decompressor code from $0800 to understand the init phase
        printf("\nDecompressor code $0800-$0860:\n");
        for (int row = 0; row < 6; ++row) {
            printf("  $%04X: ", 0x0800 + row * 16);
            for (int col = 0; col < 16; ++col)
                printf("%02X ", parasite->memory_map().peek(0x0800 + row * 16 + col));
            printf("\n");
        }

        // Dump the table area around $0CE6
        printf("\nTable area $0CD0-$0D00:\n");
        for (int row = 0; row < 3; ++row) {
            printf("  $%04X: ", 0x0CD0 + row * 16);
            for (int col = 0; col < 16; ++col)
                printf("%02X ", parasite->memory_map().peek(0x0CD0 + row * 16 + col));
            printf("\n");
        }

        // The key question: what SHOULD be at $0CE6?
        // The table is 256 bytes at $0CBE-$0DBE.
        // Offset $28 into the table ($0CE6) should be $38.
        // Let me check if the first 256 bytes written by the decompressor
        // (to $FC00+) could explain this.  The decompressor writes from
        // $FC00 and wraps through 64K.  Address $0CE6 is at output offset
        // $0CE6 - $FC00 = $10E6 (mod $10000) = $10E6 = 4326 bytes into
        // the output.  So $0CE6 gets overwritten after 4326 output bytes.
        // But we only produce 992 output bytes before hanging, so $0CE6
        // was NOT overwritten by the decompressor output.
        //
        // Therefore $0CE6 must have been written during the INIT phase
        // ($0800-$0812) or during the R3 NMI transfer.

        // Search instruction trace for STA instructions that write to the
        // table area ($0C00-$0E00).  Look for STA abs,X ($9D) where the
        // base + X lands in this range.
        printf("\nSearching for STA abs,X ($9D) instructions in $0980-$09A0 range:\n");
        int sta_count = 0;
        for (size_t i = 0; i < avail && sta_count < 30; ++i) {
            auto& e = itrace[i];
            // Look for any STA variant that might write to $0C00-$0E00
            // The $0997 instruction is BD (LDA abs,X), and nearby there
            // might be STA instructions that build the table
            if (e.pc >= 0x0800 && e.pc <= 0x0A36 &&
                (e.opcode == 0x9D || e.opcode == 0x99 || e.opcode == 0x91 || e.opcode == 0x92)) {
                printf("  [%zu] cyc=%llu PC=$%04X A=$%02X X=$%02X Y=$%02X op=$%02X\n",
                       i, static_cast<unsigned long long>(e.cycle),
                       e.pc, e.a, e.x, e.y, e.opcode);
                ++sta_count;
            }
        }

        // Last 30 instructions before hang
        printf("\nLast 30 parasite instructions:\n");
        size_t istart = avail > 30 ? avail - 30 : 0;
        for (size_t i = istart; i < avail; ++i) {
            auto& e = itrace[i];
            printf("  cyc=%-10llu PC=$%04X A=$%02X X=$%02X Y=$%02X SP=$%02X P=$%02X op=$%02X\n",
                   static_cast<unsigned long long>(e.cycle),
                   e.pc, e.a, e.x, e.y, e.sp, e.p, e.opcode);
        }

        printf("=== END TRACE ANALYSIS ===\n\n");
    }
};

}  // namespace

TEST_CASE("CE2023 memory snapshot at decompressor entry", "[tube][ce2023][trace]") {
    if (!files_available()) SKIP("Required ROMs or disc image not available");

    TestFixture fix;

    // Run interleaved until parasite reaches $0810 (JSR $0A2D = write R4 ack)
    // or $0819 (start of main decompressor loop, after the R4 ack).
    bool reached = false;
    for (int round = 0; round < 30'000'000; ++round) {
        for (int i = 0; i < 2; ++i) {
            fix.machine.step();
            if (fix.machine.memory().video_output.has_value())
                fix.renderer.process(fix.machine.memory().video_output.value());
        }
        fix.parasite->step_instruction();

        uint16_t pc = fix.parasite->pc();
        if (pc == 0x0819) {
            reached = true;
            break;
        }
    }

    if (!reached) {
        WARN("Did not reach $0819 (decompressor main loop entry)");
        printf("Parasite PC: $%04X\n", fix.parasite->pc());
        return;
    }

    printf("\n=== MEMORY SNAPSHOT AT $0819 (decompressor entry) ===\n");
    printf("Parasite PC: $%04X  A=$%02X X=$%02X Y=$%02X SP=$%02X P=$%02X\n",
           fix.parasite->pc(), fix.parasite->a(), fix.parasite->x(),
           fix.parasite->y(), fix.parasite->sp(), fix.parasite->p());

    // Dump decompressor code $0800-$0A36 (566 bytes)
    printf("\nDecompressor code ($0800-$0A35, 566 bytes):\n");
    for (int addr = 0x0800; addr < 0x0A36; addr += 16) {
        printf("  $%04X:", addr);
        for (int i = 0; i < 16 && (addr + i) < 0x0A36; ++i)
            printf(" %02X", fix.parasite->memory_map().peek(addr + i));
        printf("\n");
    }

    // Dump I/O helper relocation area at $FC00-$FC1A
    printf("\nRelocated I/O helpers ($FC00-$FC1A):\n  $FC00:");
    for (int i = 0; i < 27; ++i)
        printf(" %02X", fix.parasite->memory_map().peek(0xFC00 + i));
    printf("\n");

    // Dump zero page
    printf("\nZero page:\n");
    for (int row = 0; row < 16; ++row) {
        printf("  $%02X:", row * 16);
        for (int col = 0; col < 16; ++col)
            printf(" %02X", fix.parasite->memory_map().peek(row * 16 + col));
        printf("\n");
    }

    // Dump stack page
    printf("\nStack page ($01E0-$01FF):\n  $01E0:");
    for (int i = 0; i < 32; ++i)
        printf(" %02X", fix.parasite->memory_map().peek(0x01E0 + i));
    printf("\n");

    // Check that the decompressor table areas are zeroed
    // ($0B00-$0E00 should be zero before tree construction)
    int nonzero_count = 0;
    uint16_t first_nonzero = 0;
    for (int addr = 0x0A36; addr < 0x0E00; ++addr) {
        if (fix.parasite->memory_map().peek(addr) != 0) {
            if (nonzero_count == 0) first_nonzero = addr;
            ++nonzero_count;
        }
    }
    printf("\nTable area $0A36-$0DFF: %d non-zero bytes", nonzero_count);
    if (nonzero_count > 0)
        printf(" (first at $%04X = $%02X)", first_nonzero,
               fix.parasite->memory_map().peek(first_nonzero));
    printf("\n");

    // Dump vectors
    printf("\nVectors: NMI=$%02X%02X IRQ=$%02X%02X RESET=$%02X%02X\n",
           fix.parasite->memory_map().peek(0xFFFB), fix.parasite->memory_map().peek(0xFFFA),
           fix.parasite->memory_map().peek(0xFFFF), fix.parasite->memory_map().peek(0xFFFE),
           fix.parasite->memory_map().peek(0xFFFD), fix.parasite->memory_map().peek(0xFFFC));

    // Verify key known values
    printf("\nR3 transfer counters: w=%llu r=%llu\n",
           static_cast<unsigned long long>(fix.shared.counters.r3_h2p_writes.load()),
           static_cast<unsigned long long>(fix.shared.counters.r3_h2p_reads.load()));

    // Write full 64K dump to file for offline comparison
    auto dump_filepath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "ce2023_parasite_0819.bin";
    {
        std::ofstream dump(dump_filepath, std::ios::binary);
        for (int addr = 0; addr < 65536; ++addr) {
            uint8_t byte = fix.parasite->memory_map().peek(addr);
            dump.write(reinterpret_cast<char*>(&byte), 1);
        }
    }
    printf("\nFull 64K dump written to: %s\n", dump_filepath.string().c_str());
    printf("=== END MEMORY SNAPSHOT ===\n\n");
}

TEST_CASE("CE2023 tree build: parasite-only after boot", "[tube][ce2023][trace]") {
    // Run host + parasite interleaved to $0819 (decompressor entry),
    // then run ONLY the parasite (no host ticking) until the first
    // R1 data read or hang.  If the tree builds correctly without the
    // host running, the bug is in host-parasite interaction during
    // tree construction.  If it still fails, it's a pure 65C02 bug.
    if (!files_available()) SKIP("Required ROMs or disc image not available");

    TestFixture fix;

    // Boot to decompressor entry
    bool reached = false;
    for (int round = 0; round < 30'000'000; ++round) {
        for (int i = 0; i < 2; ++i) {
            fix.machine.step();
            if (fix.machine.memory().video_output.has_value())
                fix.renderer.process(fix.machine.memory().video_output.value());
        }
        fix.parasite->step_instruction();
        if (fix.parasite->pc() == 0x0819) { reached = true; break; }
    }
    REQUIRE(reached);

    // Now run ONLY the parasite.  The host is frozen.
    // The R1 data was already written by the host during the interleaved
    // boot, so it should be available in the latch.
    // But wait -- the tree construction reads bits from R1 before the
    // main decompression phase.  The host needs to have written the R1
    // bytes.  Let's check if R1 has data.
    printf("\n=== PARASITE-ONLY TREE BUILD ===\n");
    printf("R1 H2P ready: %d\n", fix.shared.r1_h2p.ready.load());
    printf("R1 H2P writes so far: %llu\n",
           static_cast<unsigned long long>(fix.shared.counters.r1_h2p_writes.load()));

    // Enable traces
    fix.parasite->parasite_cpu().trace().resize(4 * 1024 * 1024);
    fix.parasite->parasite_cpu().trace().set_enabled(true);
    fix.parasite->parasite_cpu().set_watch_write_addr(0x0CE6);

    // Run parasite only, but we need the host to supply R1 data!
    // Run host in large bursts between parasite instructions to ensure
    // R1 data is available.  This is "host far ahead" mode.
    for (int i = 0; i < 2'000'000; ++i) {
        // Run host for 100 cycles to keep R1 supplied
        for (int h = 0; h < 100; ++h) {
            fix.machine.step();
            if (fix.machine.memory().video_output.has_value())
                fix.renderer.process(fix.machine.memory().video_output.value());
        }
        fix.parasite->step_instruction();

        if (fix.parasite->pc() == DECOMP_R1_BPL) {
            // Check if stuck
            int poll = 0;
            while (fix.parasite->pc() == DECOMP_R1_BPL && poll < 10000) {
                // Give host time to write
                for (int h = 0; h < 1000; ++h) {
                    fix.machine.step();
                    if (fix.machine.memory().video_output.has_value())
                        fix.renderer.process(fix.machine.memory().video_output.value());
                }
                fix.parasite->step_instruction();
                ++poll;
            }
            if (fix.parasite->pc() == DECOMP_R1_BPL) {
                printf("Hung at R1 poll after tree build\n");
                break;
            }
        }
    }

    // Check the table
    uint16_t table_base = fix.parasite->memory_map().peek(0x0998)
                        | (fix.parasite->memory_map().peek(0x0999) << 8);
    uint8_t val_at_0CE6 = fix.parasite->memory_map().peek(0x0CE6);
    printf("Parasite PC: $%04X\n", fix.parasite->pc());
    printf("Table base: $%04X\n", table_base);
    printf("Value at $0CE6: $%02X (expected $38)\n", val_at_0CE6);

    // Check watchpoint hits
    auto& itrace = fix.parasite->parasite_cpu().trace();
    printf("Instruction trace: %llu entries\n",
           static_cast<unsigned long long>(itrace.total_count()));
    printf("Writes to $0CE6:\n");
    for (size_t i = 0; i < itrace.available(); ++i) {
        auto& e = itrace[i];
        if (e.opcode == 0xFF && e.pc == 0x0CE6) {
            printf("  WRITE $0CE6 = $%02X at cyc=%llu\n",
                   e.a, static_cast<unsigned long long>(e.cycle));
        }
    }

    // Decompressor output first 20 bytes
    printf("Output ($FC00+): ");
    for (int i = 0; i < 20; ++i)
        printf("%02X ", fix.parasite->memory_map().peek(0xFC00 + i));
    printf("\n");
    printf("Expected:         ");
    for (int i = 0; i < 20; ++i)
        printf("%02X ", JSBEEB_OUTPUT[i]);
    printf("\n");

    printf("R1 H2P: w=%llu r=%llu\n",
           static_cast<unsigned long long>(fix.shared.counters.r1_h2p_writes.load()),
           static_cast<unsigned long long>(fix.shared.counters.r1_h2p_reads.load()));

    if (val_at_0CE6 == 0x38) {
        printf("TABLE IS CORRECT with host-far-ahead!\n");
    } else {
        printf("TABLE IS WRONG even with host-far-ahead: $%02X != $38\n", val_at_0CE6);
    }
    printf("=== END PARASITE-ONLY TREE BUILD ===\n\n");
}

TEST_CASE("CE2023 divergence trace: 2:3 interleaved", "[tube][ce2023][trace]") {
    if (!files_available()) SKIP("Required ROMs or disc image not available");

    TestFixture fix;
    fix.enable_traces();

    bool hung = fix.run_until_hang(2, 3, 30'000'000);

    fix.print_analysis();

    if (hung) {
        WARN("CE2023 HUNG at R1 poll ($09D4) with 2:3 interleaving");
    } else {
        WARN("CE2023 did not hang within budget (may have loaded!)");
    }
}

TEST_CASE("CE2023 divergence trace: 1:1 interleaved", "[tube][ce2023][trace]") {
    if (!files_available()) SKIP("Required ROMs or disc image not available");

    TestFixture fix;
    fix.enable_traces();

    // 1:1 interleaving: tightest coupling, similar to jsbeeb/B-Em.
    // If this works, the bug is timing-dependent on batch size.
    bool hung = fix.run_until_hang(1, 1, 60'000'000);

    fix.print_analysis();

    if (hung) {
        WARN("CE2023 HUNG at R1 poll ($09D4) with 1:1 interleaving");
    } else {
        WARN("CE2023 did not hang within budget with 1:1 interleaving (may have loaded!)");
    }
}
