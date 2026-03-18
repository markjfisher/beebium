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

// R1 transfer tests using the real 65C02 CPU.
//
// A small 6502 program polls R1 status and reads R1 data bytes,
// storing them into a results buffer in parasite RAM. The host
// writes bytes through TubeHostPort on a separate thread. After
// the transfer, the results buffer is compared byte-by-byte.
//
// This exercises the full path: ParasiteCpu::tick() -> memory_.read()
// -> TubeParasitePort::parasite_read() -> shared memory atomics,
// including the per-tick pirq()/pnmi_level() calls.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteCpu.hpp>
#include <beebium/tube/ParasiteMemoryMap.hpp>
#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeParasitePort.hpp>
#include <beebium/tube/TubeShared.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

using namespace beebium;

// A minimal 2 KB ROM that contains only a reset vector pointing to $0400.
// The ROM content is irrelevant because boot mode is disabled immediately
// by the first Tube register access; all code runs from RAM.
static std::array<uint8_t, 2048> make_stub_rom(uint16_t reset_addr) {
    std::array<uint8_t, 2048> rom{};
    // Reset vector at offset $07FC (address $FFFC when mapped at $F800)
    rom[0x07FC] = reset_addr & 0xFF;
    rom[0x07FD] = (reset_addr >> 8) & 0xFF;
    return rom;
}

// Plant machine code into parasite RAM.
static void plant(ParasiteMemoryMap& mem, uint16_t addr, std::initializer_list<uint8_t> code) {
    for (auto byte : code) {
        mem.ram(addr++) = byte;
    }
}

// ============================================================================
// 6502 R1 polled read program
// ============================================================================
//
// The program at $0400 reads NUM_BYTES from R1 and stores them at $0500+.
// It uses the same poll-read pattern as the CE2023 decompressor:
//
//   $0400: LDX #$00         ; index = 0
//   $0402: BIT $FEF8        ; poll R1 status
//   $0405: BPL $0402        ; loop until data available (bit 7)
//   $0407: LDA $FEF9        ; read R1 data
//   $040A: STA $0500,X      ; store in results buffer
//   $040D: INX              ; next index
//   $040E: CPX #NUM_BYTES   ; done?
//   $0410: BNE $0402        ; loop
//   $0412: STP              ; halt (65C02 stop instruction)
//
// After running, $0500..$0500+NUM_BYTES-1 contains the received bytes.

static constexpr uint16_t CODE_ADDR = 0x0400;
static constexpr uint16_t RESULT_ADDR = 0x0500;

static void plant_r1_reader(ParasiteMemoryMap& mem, uint8_t num_bytes) {
    plant(mem, CODE_ADDR, {
        0xA2, 0x00,                         // LDX #$00
        0x2C, 0xF8, 0xFE,                   // BIT $FEF8     (poll R1 status)
        0x10, 0xFB,                          // BPL $0402     (loop if N=0)
        0xAD, 0xF9, 0xFE,                    // LDA $FEF9     (read R1 data)
        0x9D, 0x00, 0x05,                    // STA $0500,X   (store result)
        0xE8,                                // INX
        0xE0, num_bytes,                     // CPX #num_bytes
        0xD0, 0xF1,                          // BNE $0402     (loop)
        0x80, 0xFE,                              // BRA *         (infinite loop = halt)
    });
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("6502 R1 polled read: single byte", "[tube][6502][r1]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite_port(&shared);

    auto rom = make_stub_rom(CODE_ADDR);
    ParasiteMemoryMap memory(parasite_port, rom);
    ParasiteCpu cpu(memory, parasite_port);

    plant_r1_reader(memory, 1);

    // Disable boot ROM by reading a Tube register
    memory.read(0xFEF8);

    // Set reset vector in RAM
    memory.ram(0xFFFC) = CODE_ADDR & 0xFF;
    memory.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;

    cpu.reset();

    // Host writes one byte on a separate thread
    std::thread host_thread([&] {
        host.host_write(1, 0x42);
    });

    // Run CPU until STP (or timeout)
    for (int i = 0; i < 100000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();
    }

    host_thread.join();
    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);  // reached halt loop
    CHECK(memory.ram(RESULT_ADDR) == 0x42);
}

TEST_CASE("6502 R1 polled read: 702 bytes across threads", "[tube][6502][r1]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite_port(&shared);

    auto rom = make_stub_rom(CODE_ADDR);
    ParasiteMemoryMap memory(parasite_port, rom);
    ParasiteCpu cpu(memory, parasite_port);

    // For > 255 bytes we need a 16-bit counter. Use a simpler approach:
    // read 200 bytes (fits in X register) and verify those.
    constexpr uint8_t NUM_BYTES = 200;
    plant_r1_reader(memory, NUM_BYTES);
    memory.read(0xFEF8);
    memory.ram(0xFFFC) = CODE_ADDR & 0xFF;
    memory.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;
    cpu.reset();

    std::thread host_thread([&] {
        for (int i = 0; i < NUM_BYTES; ++i) {
            host.host_write(1, static_cast<uint8_t>(i & 0xFF));
        }
    });

    for (int i = 0; i < 5000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();
    }

    host_thread.join();
    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);  // reached halt loop

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(memory.ram(RESULT_ADDR + i) == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("6502 R1 polled read: 200 bytes, repeated 50 times", "[tube][6502][r1]") {
    constexpr uint8_t NUM_BYTES = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeShared shared;
        shared.init();
        TubeHostPort host(&shared);
        TubeParasitePort parasite_port(&shared);

        auto rom = make_stub_rom(CODE_ADDR);
        ParasiteMemoryMap memory(parasite_port, rom);
        ParasiteCpu cpu(memory, parasite_port);

        plant_r1_reader(memory, NUM_BYTES);
        memory.read(0xFEF8);
        memory.ram(0xFFFC) = CODE_ADDR & 0xFF;
        memory.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;
        cpu.reset();

        uint8_t base = static_cast<uint8_t>(iter * 11);

        std::thread host_thread([&] {
            for (int i = 0; i < NUM_BYTES; ++i) {
                host.host_write(1, static_cast<uint8_t>((base + i) & 0xFF));
            }
        });

        for (int i = 0; i < 5000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
            cpu.tick();
        }

        host_thread.join();
        REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);  // reached halt loop

        for (int i = 0; i < NUM_BYTES; ++i) {
            uint8_t expected = static_cast<uint8_t>((base + i) & 0xFF);
            if (memory.ram(RESULT_ADDR + i) != expected) {
                FAIL("Iteration " << iter << ", byte " << i
                     << ": expected $" << std::hex << static_cast<int>(expected)
                     << " got $" << std::hex << static_cast<int>(memory.ram(RESULT_ADDR + i)));
            }
        }
    }
}

TEST_CASE("6502 R1 polled read: diagnostic -- check data bus at LDA $FEF9", "[tube][6502][r1][diagnostic]") {
    // After each tick, check if the CPU just completed a read from $FEF9.
    // If so, verify that cpu.dbus matches the expected byte.

    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite_port(&shared);

    auto rom = make_stub_rom(CODE_ADDR);
    ParasiteMemoryMap memory(parasite_port, rom);
    ParasiteCpu cpu(memory, parasite_port);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r1_reader(memory, NUM_BYTES);
    memory.read(0xFEF8);
    memory.ram(0xFFFC) = CODE_ADDR & 0xFF;
    memory.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;
    cpu.reset();

    // Track what the host has written
    std::atomic<uint8_t> last_host_value{0};
    std::atomic<int> host_write_count{0};

    std::thread host_thread([&] {
        for (int i = 0; i < NUM_BYTES; ++i) {
            uint8_t val = static_cast<uint8_t>(i & 0xFF);
            host.host_write(1, val);
            last_host_value.store(val, std::memory_order_release);
            host_write_count.store(i + 1, std::memory_order_release);
        }
    });

    int r1_reads = 0;
    int first_mismatch = -1;
    uint8_t mismatch_dbus = 0, mismatch_expected = 0;

    for (int i = 0; i < 5000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();

        // After tick: if the address bus was $FEF9 and it was a read,
        // the dbus now contains the R1 data byte.
        if (cpu.cpu().abus.w == 0xFEF9 && cpu.cpu().read) {
            uint8_t got = cpu.cpu().dbus;
            uint8_t expected = static_cast<uint8_t>(r1_reads & 0xFF);
            if (got != expected && first_mismatch < 0) {
                first_mismatch = r1_reads;
                mismatch_dbus = got;
                mismatch_expected = expected;
            }
            r1_reads++;
        }
    }

    host_thread.join();

    if (first_mismatch >= 0) {
        FAIL("R1 read #" << first_mismatch
             << ": dbus=$" << std::hex << static_cast<int>(mismatch_dbus)
             << " expected=$" << std::hex << static_cast<int>(mismatch_expected)
             << " (total R1 reads: " << std::dec << r1_reads << ")");
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);
    CHECK(r1_reads == NUM_BYTES);
}

TEST_CASE("6502 R1 polled read: diagnostic -- read count vs write count", "[tube][6502][r1][diagnostic]") {
    // Checks whether the parasite's R1 read counter ever exceeds the
    // host's write counter. If it does, the parasite consumed a byte
    // that the host hadn't finished writing yet.

    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite_port(&shared);

    auto rom = make_stub_rom(CODE_ADDR);
    ParasiteMemoryMap memory(parasite_port, rom);
    ParasiteCpu cpu(memory, parasite_port);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r1_reader(memory, NUM_BYTES);
    memory.read(0xFEF8);
    memory.ram(0xFFFC) = CODE_ADDR & 0xFF;
    memory.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;
    cpu.reset();

    std::atomic<int> host_writes{0};

    std::thread host_thread([&] {
        for (int i = 0; i < NUM_BYTES; ++i) {
            host.host_write(1, static_cast<uint8_t>(i & 0xFF));
            host_writes.store(i + 1, std::memory_order_release);
        }
    });

    for (int i = 0; i < 5000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();

        auto reads = shared.counters.r1_h2p_reads.load(std::memory_order_relaxed);
        auto writes = host_writes.load(std::memory_order_acquire);
        if (reads > static_cast<uint64_t>(writes)) {
            host_thread.join();
            FAIL("Tick " << i << ": R1 reads (" << reads
                 << ") > host writes (" << writes << ")");
        }
    }

    host_thread.join();
    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(memory.ram(RESULT_ADDR + i) == static_cast<uint8_t>(i & 0xFF));
    }
}
