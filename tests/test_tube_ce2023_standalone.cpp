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

// CE2023 standalone decompressor test.
//
// Loads the parasite memory snapshot from $0819 (decompressor entry) and
// runs JUST the decompressor code with pre-loaded R1 data.  No host CPU,
// no boot, no disc -- sub-second instead of 30 seconds.
//
// This isolates the 65C02 emulation: if the tree construction produces
// the wrong result here, it's a pure 6502 library bug (shared with B2).

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteCpu.hpp>
#include <beebium/tube/ParasiteMemoryMap.hpp>
#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeParasitePort.hpp>
#include <beebium/tube/TubeShared.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

using namespace beebium;

namespace {

static constexpr const char* SNAPSHOT_FILENAME = "ce2023_parasite_0819.bin";
static constexpr const char* R1_DATA_FILENAME = "ce2023_r1_data.bin";

// First 20 expected output bytes from jsbeeb
static constexpr uint8_t JSBEEB_OUTPUT[] = {
    0x2C, 0xF8, 0xFE, 0x10, 0xFB, 0xAD, 0xF9, 0xFE,
    0x38, 0x6A, 0x85, 0x31, 0x68, 0x60, 0xA4, 0x2F,
    0xB1, 0x33, 0xA0, 0x00,
};

bool assets_available() {
    auto dir = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);
    return std::filesystem::exists(dir / SNAPSHOT_FILENAME)
        && std::filesystem::exists(dir / R1_DATA_FILENAME);
}

}  // namespace

TEST_CASE("CE2023 standalone: decompressor from snapshot", "[tube][ce2023][standalone]") {
    if (!assets_available())
        SKIP("Snapshot or R1 data not available (run the trace test first to generate them)");

    auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);

    // Load 64K snapshot
    std::array<uint8_t, 65536> snapshot{};
    {
        std::ifstream f(assets_dirpath / SNAPSHOT_FILENAME, std::ios::binary);
        REQUIRE(f.good());
        f.read(reinterpret_cast<char*>(snapshot.data()), 65536);
        REQUIRE(f.gcount() == 65536);
    }

    // Load 702 R1 data bytes
    std::vector<uint8_t> r1_data;
    {
        std::ifstream f(assets_dirpath / R1_DATA_FILENAME, std::ios::binary);
        REQUIRE(f.good());
        r1_data.assign(std::istreambuf_iterator<char>(f), {});
        REQUIRE(r1_data.size() == 702);
    }

    // Set up Tube shared memory
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite_port(&shared);

    // Create parasite with stub ROM (vectors match snapshot)
    std::array<uint8_t, 2048> stub_rom{};
    stub_rom[0x07FC] = 0x19; stub_rom[0x07FD] = 0x08;  // RESET -> $0819
    stub_rom[0x07FA] = 0xB3; stub_rom[0x07FB] = 0xFE;  // NMI -> $FEB3
    stub_rom[0x07FE] = 0xE5; stub_rom[0x07FF] = 0xFC;  // IRQ -> $FCE5

    ParasiteMemoryMap memory(parasite_port, stub_rom);
    ParasiteCpu cpu(memory, parasite_port);

    // Disable boot ROM and load snapshot into RAM
    memory.read(0xFEF8);
    for (int addr = 0; addr < 65536; ++addr)
        memory.ram(addr) = snapshot[addr];

    // Set CPU state to match snapshot at $0819
    cpu.reset();
    cpu.cpu().opcode_pc.w = 0x0819;
    cpu.cpu().pc.w = 0x081A;
    cpu.cpu().a = 0xFC;
    cpu.cpu().x = 0x00;
    cpu.cpu().y = 0x00;
    cpu.cpu().s.b.l = 0xF6;
    cpu.cpu().s.b.h = 0x01;
    M6502_SetP(&cpu.cpu(), 0x60);
    cpu.cpu().dbus = memory.peek(0x0819);

    // Enable traces
    cpu.trace().resize(4 * 1024 * 1024);
    cpu.trace().set_enabled(true);
    cpu.set_watch_write_addr(0x0CE6);

    // Feed R1 data: write all 702 bytes as the parasite consumes them
    int r1_idx = 0;
    auto feed_r1 = [&]() {
        while (shared.r1_h2p.ready.load(std::memory_order_acquire) == 0
               && r1_idx < static_cast<int>(r1_data.size())) {
            host.host_write(1, r1_data[r1_idx++]);
            return;
        }
    };

    // Feed first byte
    feed_r1();

    printf("\n=== STANDALONE DECOMPRESSOR ===\n");

    // Run parasite
    bool hung = false;
    int poll_count = 0;

    for (int i = 0; i < 10'000'000; ++i) {
        cpu.step_instruction();

        // Feed R1 data as consumed
        feed_r1();

        uint16_t pc = cpu.cpu().opcode_pc.w;

        // Detect hang at R1 poll ($09D4)
        if (pc == 0x09D4) {
            if (++poll_count > 50000) {
                hung = true;
                printf("Hung at R1 poll after %d instructions, %d R1 bytes fed\n", i, r1_idx);
                break;
            }
        } else {
            poll_count = 0;
        }

        // Detect completion: JMP ($FFFC) at $0816
        if (pc == 0x0816) {
            printf("Decompressor completed at instruction %d! R1 bytes: %d\n", i, r1_idx);
            break;
        }
    }

    // Analysis
    uint8_t val_at_0CE6 = memory.peek(0x0CE6);
    printf("Parasite PC: $%04X\n", cpu.cpu().opcode_pc.w);
    printf("R1 bytes fed: %d / %zu\n", r1_idx, r1_data.size());
    printf("Value at $0CE6: $%02X (expected $38)\n", val_at_0CE6);

    // Watchpoint hits
    auto& itrace = cpu.trace();
    printf("\nWrites to $0CE6:\n");
    for (size_t i = 0; i < itrace.available(); ++i) {
        auto& e = itrace[i];
        if (e.opcode == 0xFF && e.pc == 0x0CE6) {
            printf("  WRITE $0CE6 = $%02X at cyc=%llu",
                   e.a, static_cast<unsigned long long>(e.cycle));
            if (i > 0) {
                auto& prev = itrace[i - 1];
                printf("  (from PC=$%04X op=$%02X A=$%02X X=$%02X Y=$%02X)",
                       prev.pc, prev.opcode, prev.a, prev.x, prev.y);
            }
            printf("\n");
        }
    }

    // Output bytes
    printf("\nOutput ($FC00+, first 20):\n  Beebium:  ");
    for (int i = 0; i < 20; ++i) printf("%02X ", memory.peek(0xFC00 + i));
    printf("\n  Expected: ");
    for (size_t i = 0; i < sizeof(JSBEEB_OUTPUT); ++i) printf("%02X ", JSBEEB_OUTPUT[i]);
    printf("\n");

    // Find first divergence
    int first_diff = -1;
    for (int i = 0; i < static_cast<int>(sizeof(JSBEEB_OUTPUT)); ++i) {
        if (memory.peek(0xFC00 + i) != JSBEEB_OUTPUT[i]) {
            first_diff = i;
            break;
        }
    }
    if (first_diff >= 0) {
        printf("  DIVERGENCE at byte %d ($FC%02X): got $%02X expected $%02X\n",
               first_diff, first_diff,
               memory.peek(0xFC00 + first_diff), JSBEEB_OUTPUT[first_diff]);
    } else {
        printf("  First %zu output bytes MATCH!\n", sizeof(JSBEEB_OUTPUT));
    }

    if (val_at_0CE6 == 0x38) {
        printf("\nRESULT: Table CORRECT\n");
    } else {
        printf("\nRESULT: Table WRONG ($%02X != $38) -- 65C02 emulation bug\n", val_at_0CE6);
    }

    printf("Total instructions: %llu\n",
           static_cast<unsigned long long>(itrace.total_count()));
    printf("=== END STANDALONE ===\n\n");
}
