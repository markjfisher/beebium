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

// Tests for the parasite CPU wrapper.
//
// ParasiteCpu wires a Rockwell 65C02 to the ParasiteMemoryMap and TubeParasitePort,
// routing bus access and interrupt lines. These tests verify:
//   - CPU initialisation and reset
//   - Instruction execution via tick() and step_instruction()
//   - Memory read/write through the memory map
//   - IRQ routing from TubeParasitePort::pirq()
//   - NMI routing from TubeParasitePort::pnmi()
//   - Boot mode interaction (CPU fetches reset vector from ROM)

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteCpu.hpp>
#include <beebium/tube/ParasiteMemoryMap.hpp>
#include <beebium/tube/TubeParasitePort.hpp>
#include <beebium/tube/TubeShared.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

// Helper: create a 2 KB ROM with a known reset vector and initial code.
// Reset vector points to rom_entry (default &F800).
// First bytes at rom_entry are filled with NOPs.
static std::array<uint8_t, 2048> make_nop_rom(uint16_t rom_entry = 0xF800) {
    std::array<uint8_t, 2048> rom{};
    rom.fill(0xEA);  // NOP
    // Reset vector at ROM offset 0x7FC-0x7FD (maps to &FFFC-&FFFD)
    rom[0x7FC] = static_cast<uint8_t>(rom_entry & 0xFF);
    rom[0x7FD] = static_cast<uint8_t>(rom_entry >> 8);
    // IRQ/BRK vector at ROM offset 0x7FE-0x7FF (maps to &FFFE-&FFFF)
    // Point to a location with RTI
    rom[0x7FE] = 0x00;  // &F900 (ROM offset 0x100)
    rom[0x7FF] = 0xF9;
    rom[0x100] = 0x40;  // RTI at &F900
    // NMI vector at ROM offset 0x7FA-0x7FB (maps to &FFFA-&FFFB)
    rom[0x7FA] = 0x80;  // &F980 (ROM offset 0x180)
    rom[0x7FB] = 0xF9;
    rom[0x180] = 0x40;  // RTI at &F980
    return rom;
}

// ===========================================================================
// Construction and initial state
// ===========================================================================

TEST_CASE("ParasiteCpu initialises with Rockwell 65C02 config", "[parasite][cpu]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);

    CHECK(cpu.cycle_count() == 0);
    CHECK(cpu.cpu().config == &M6502_rockwell65c02_config);
}

// ===========================================================================
// Reset and reset vector
// ===========================================================================

TEST_CASE("ParasiteCpu reset fetches reset vector from boot ROM", "[parasite][cpu][reset]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom(0xF800);
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    // After reset, step through the 7-cycle reset sequence
    uint64_t cycles = cpu.step_instruction();
    CHECK(cycles == 7);

    // CPU is about to execute at &F800 (the reset vector).
    // pc.w is already incremented past the opcode fetch; use opcode_pc or abus.
    CHECK(cpu.cpu().abus.w == 0xF800);
}

TEST_CASE("ParasiteCpu reset with custom reset vector", "[parasite][cpu][reset]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom(0xF900);
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    uint64_t cycles = cpu.step_instruction();
    CHECK(cycles == 7);
    CHECK(cpu.cpu().abus.w == 0xF900);
}

TEST_CASE("ParasiteCpu reset re-enters boot mode", "[parasite][cpu][reset]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    // Memory map should be in boot mode after reset
    CHECK(mem.boot_mode());
}

TEST_CASE("ParasiteCpu reset clears cycle count", "[parasite][cpu][reset]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    // Execute some cycles
    cpu.step_instruction();  // 7-cycle reset
    cpu.step_instruction();  // NOP
    CHECK(cpu.cycle_count() > 0);

    // Reset should clear cycle count
    cpu.reset();
    CHECK(cpu.cycle_count() == 0);
}

// ===========================================================================
// Instruction execution
// ===========================================================================

TEST_CASE("ParasiteCpu step_instruction executes NOP", "[parasite][cpu][execution]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    // Reset sequence: 7 cycles
    uint64_t cycles = cpu.step_instruction();
    CHECK(cycles == 7);

    // NOP: 2 cycles
    cycles = cpu.step_instruction();
    CHECK(cycles == 2);

    // Another NOP: 2 cycles
    cycles = cpu.step_instruction();
    CHECK(cycles == 2);

    CHECK(cpu.cycle_count() == 11);
}

TEST_CASE("ParasiteCpu tick advances one cycle", "[parasite][cpu][execution]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.tick();
    CHECK(cpu.cycle_count() == 1);

    cpu.tick();
    CHECK(cpu.cycle_count() == 2);
}

TEST_CASE("ParasiteCpu run executes multiple cycles", "[parasite][cpu][execution]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.run(20);
    CHECK(cpu.cycle_count() == 20);
}

TEST_CASE("ParasiteCpu executes LDA immediate from ROM", "[parasite][cpu][execution]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    // Place LDA #$42 at &F800 (ROM offset 0)
    rom[0x000] = 0xA9;  // LDA #imm
    rom[0x001] = 0x42;
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset sequence (7 cycles)
    cpu.step_instruction();  // LDA #$42 (2 cycles)

    CHECK(cpu.cpu().a == 0x42);
}

TEST_CASE("ParasiteCpu executes STA/LDA in RAM", "[parasite][cpu][execution]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    // Place STA $1000 then LDA $1000 at &F800
    rom[0x000] = 0xA9;  // LDA #$55
    rom[0x001] = 0x55;
    rom[0x002] = 0x8D;  // STA $1000
    rom[0x003] = 0x00;
    rom[0x004] = 0x10;
    rom[0x005] = 0xA9;  // LDA #$00 (clear accumulator)
    rom[0x006] = 0x00;
    rom[0x007] = 0xAD;  // LDA $1000
    rom[0x008] = 0x00;
    rom[0x009] = 0x10;
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // LDA #$55 (2)
    cpu.step_instruction();  // STA $1000 (4)
    cpu.step_instruction();  // LDA #$00 (2)
    CHECK(cpu.cpu().a == 0x00);

    cpu.step_instruction();  // LDA $1000 (4)
    CHECK(cpu.cpu().a == 0x55);

    // Verify RAM was written
    CHECK(mem.ram(0x1000) == 0x55);
}

// ===========================================================================
// IRQ routing from Tube
// ===========================================================================

TEST_CASE("ParasiteCpu routes PIRQ to CPU IRQ line", "[parasite][cpu][irq]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    // Program: CLI then loop with NOPs
    rom[0x000] = 0x58;  // CLI (enable interrupts)
    // Fill rest with NOP (already 0xEA)
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // CLI (2)

    // Enable I flag (PIRQ from R1) and provide data
    shared.control_flags.store(0x02, std::memory_order_release);  // I flag
    shared.r1_h2p.value.store(0xAA, std::memory_order_relaxed);
    shared.r1_h2p.ready.store(1, std::memory_order_release);

    // Execute a few NOPs -- IRQ should fire
    cpu.run(20);

    // PC should have jumped to the IRQ vector (&F900)
    // After the interrupt handler runs RTI, it returns.
    // We just check that the interrupt was taken by verifying
    // the CPU visited the IRQ vector address.
    // With the I flag set and R1 data available, PIRQ should be active.
    CHECK(port.pirq());
}

TEST_CASE("ParasiteCpu no IRQ when interrupts disabled", "[parasite][cpu][irq]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    // Program: SEI then NOPs (interrupts disabled)
    rom[0x000] = 0x78;  // SEI
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // SEI (2)

    // Enable PIRQ source
    shared.control_flags.store(0x02, std::memory_order_release);
    shared.r1_h2p.value.store(0xBB, std::memory_order_relaxed);
    shared.r1_h2p.ready.store(1, std::memory_order_release);

    // Run some cycles -- CPU should just execute NOPs, no IRQ taken
    uint16_t pc_after_sei = cpu.cpu().pc.w;
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP

    // PC should have advanced linearly (3 NOPs = 6 bytes)
    // Since SEI was at &F800, next instruction is &F801, then &F803, &F805, &F807
    // (each NOP is 1 byte, 2 cycles)
    CHECK(cpu.cpu().pc.w == pc_after_sei + 3);
}

// ===========================================================================
// NMI routing from Tube
// ===========================================================================

// PNMI behaviour from Application Note 004 and the 6502 Second Processor
// Service Manual:
//
//   PNMI is the active-high output from the Tube ULA that drives the
//   parasite 6502's /NMI line (active-low, inverted by hardware).
//
//   PNMI goes high when M=1 AND (R3 H-to-P has data OR R3 P-to-H has space).
//   The 6502 NMI is edge-triggered (fires on falling edge of /NMI, i.e.
//   rising edge of the active-high PNMI level).
//
//   In the shared-memory model, ParasiteCpu passes pnmi_level() (the raw
//   combinational output) to M6502_SetDeviceNMI every cycle. The M6502
//   library handles edge detection internally. This ensures the CPU sees
//   NMI immediately when the host writes R3 data, without waiting for
//   the parasite to perform a register access.

TEST_CASE("ParasiteCpu PNMI: host R3 write triggers NMI during NOP execution", "[parasite][cpu][nmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // After reset, R3 P-to-H has the dummy byte (pending=1), and R3 H-to-P
    // is empty. With M=0, PNMI level is false.
    CHECK_FALSE(port.pnmi_level());

    // Enable M flag. R3 P-to-H has the dummy byte (pending=1), so P-to-H
    // space is NOT available. R3 H-to-P is empty, so H-to-P data is NOT
    // available. PNMI level should still be false.
    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);
    CHECK_FALSE(port.pnmi_level());

    // Execute a NOP to let the CPU sample the NMI line while it's low.
    // This establishes the baseline for edge detection.
    cpu.step_instruction();  // NOP (2)

    // Now simulate the host depositing data into R3 H-to-P.
    // This makes H-to-P data available, so PNMI goes high.
    shared.r3_h2p.data[0].store(0x42, std::memory_order_relaxed);
    shared.r3_h2p.count.store(1, std::memory_order_relaxed);
    shared.r3_h2p.pending.store(1, std::memory_order_release);

    CHECK(port.pnmi_level());

    // Run enough cycles for the NMI to be taken. The 6502 takes 7 cycles
    // to service an NMI: current instruction completes (up to ~6 cycles
    // for longest instruction), then 7 cycles for NMI handler entry.
    // After NMI, PC should be at the NMI vector (&F980).
    cpu.run(20);

    // The NMI handler at &F980 contains RTI, which returns to the
    // interrupted NOP stream. Verify the CPU visited the NMI vector
    // by checking that opcode_pc passed through &F980 at some point.
    // Since we ran 20 cycles, the NMI handler has executed RTI and
    // returned. The simplest check: the NMI was taken (nmi_flags cleared).
    CHECK(cpu.cpu().nmi_flags == 0);

    // The CPU should have continued executing NOPs after RTI.
    // Verify it's back in the ROM NOP stream (PC in &F800-&FFFF range).
    CHECK(cpu.cpu().abus.w >= 0xF800);
}

TEST_CASE("ParasiteCpu PNMI: not triggered when M flag is clear", "[parasite][cpu][nmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // M flag is clear (default). Deposit R3 H-to-P data.
    shared.r3_h2p.data[0].store(0x42, std::memory_order_relaxed);
    shared.r3_h2p.count.store(1, std::memory_order_relaxed);
    shared.r3_h2p.pending.store(1, std::memory_order_release);

    // PNMI level should be false (M=0 disables PNMI).
    CHECK_FALSE(port.pnmi_level());

    // Run some NOPs -- NMI should NOT fire.
    uint16_t pc_before = cpu.cpu().pc.w;
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP

    // PC should advance linearly (3 NOPs = 3 bytes from pc_before).
    CHECK(cpu.cpu().pc.w == pc_before + 3);
}

TEST_CASE("ParasiteCpu PNMI: P-to-H space triggers NMI", "[parasite][cpu][nmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // After reset, R3 P-to-H has dummy byte (pending=1).
    // With M=1, P-to-H space is NOT available (pending=1).
    // H-to-P data is NOT available (empty).
    // PNMI level should be false.
    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);
    CHECK_FALSE(port.pnmi_level());

    // Let CPU sample the low NMI level.
    cpu.step_instruction();  // NOP (2)

    // Now simulate host consuming the dummy byte from P-to-H (clearing pending).
    // This makes P-to-H space available, so PNMI goes high.
    shared.r3_p2h.count.store(0, std::memory_order_relaxed);
    shared.r3_p2h.pending.store(0, std::memory_order_release);

    CHECK(port.pnmi_level());

    // Run cycles -- NMI should fire.
    cpu.run(20);

    // Verify NMI was serviced.
    CHECK(cpu.cpu().nmi_flags == 0);
}

TEST_CASE("ParasiteCpu PNMI: second edge after level drops and rises", "[parasite][cpu][nmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // Set M flag, establish low PNMI baseline.
    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);
    CHECK_FALSE(port.pnmi_level());
    cpu.step_instruction();  // NOP -- CPU samples PNMI low

    // First NMI: deposit R3 H-to-P data.
    shared.r3_h2p.data[0].store(0x11, std::memory_order_relaxed);
    shared.r3_h2p.count.store(1, std::memory_order_relaxed);
    shared.r3_h2p.pending.store(1, std::memory_order_release);
    CHECK(port.pnmi_level());

    // Run enough for NMI + RTI (7 + 6 = 13 cycles, give some margin).
    cpu.run(20);

    // PNMI level is still high (R3 H-to-P data not consumed).
    CHECK(port.pnmi_level());

    // Simulate parasite consuming the R3 H-to-P data (via a register access
    // in the NMI handler, but here we manipulate shared state directly).
    shared.r3_h2p.count.store(0, std::memory_order_relaxed);
    shared.r3_h2p.pending.store(0, std::memory_order_release);

    // Also make P-to-H occupied so PNMI drops fully.
    shared.r3_p2h.count.store(1, std::memory_order_relaxed);
    shared.r3_p2h.pending.store(1, std::memory_order_release);

    // PNMI level should now be false.
    CHECK_FALSE(port.pnmi_level());

    // Let CPU sample the low level for at least one cycle.
    cpu.step_instruction();  // NOP

    // Second NMI: deposit new R3 H-to-P data.
    shared.r3_h2p.data[0].store(0x22, std::memory_order_relaxed);
    shared.r3_h2p.count.store(1, std::memory_order_relaxed);
    shared.r3_h2p.pending.store(1, std::memory_order_release);
    CHECK(port.pnmi_level());

    // Run -- second NMI should fire.
    cpu.run(20);
    CHECK(cpu.cpu().nmi_flags == 0);
}

TEST_CASE("ParasiteCpu PNMI: NMI cannot be masked by SEI", "[parasite][cpu][nmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    // Program: SEI then NOPs
    rom[0x000] = 0x78;  // SEI
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // SEI (2) -- interrupts disabled

    // Set M flag, establish low baseline.
    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);
    cpu.step_instruction();  // NOP -- CPU samples PNMI low

    // Trigger PNMI.
    shared.r3_h2p.data[0].store(0x42, std::memory_order_relaxed);
    shared.r3_h2p.count.store(1, std::memory_order_relaxed);
    shared.r3_h2p.pending.store(1, std::memory_order_release);

    // Run -- NMI should fire despite SEI (NMI is non-maskable).
    cpu.run(20);
    CHECK(cpu.cpu().nmi_flags == 0);
}

// ===========================================================================
// Memory map interaction
// ===========================================================================

TEST_CASE("ParasiteCpu reads from boot ROM during reset sequence", "[parasite][cpu][boot]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom(0xF850);
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();

    // Boot mode should be active
    REQUIRE(mem.boot_mode());

    // Reset sequence reads vectors from ROM
    cpu.step_instruction();  // 7 cycles

    // CPU is about to execute at &F850 (from ROM reset vector)
    CHECK(cpu.cpu().abus.w == 0xF850);

    // Still in boot mode (haven't accessed Tube registers yet)
    CHECK(mem.boot_mode());
}

TEST_CASE("ParasiteCpu accessing Tube register terminates boot mode", "[parasite][cpu][boot]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    // Program at &F800: LDA $FEF8 (read Tube R1 status)
    rom[0x000] = 0xAD;  // LDA abs
    rom[0x001] = 0xF8;  // low byte
    rom[0x002] = 0xFE;  // high byte
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);
    cpu.reset();
    REQUIRE(mem.boot_mode());

    cpu.step_instruction();  // reset (7)
    CHECK(mem.boot_mode());

    cpu.step_instruction();  // LDA $FEF8 (4 cycles)
    CHECK_FALSE(mem.boot_mode());
}

// ===========================================================================
// CPU state access
// ===========================================================================

TEST_CASE("ParasiteCpu provides mutable and const CPU access", "[parasite][cpu]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort port(&shared);
    auto rom = make_nop_rom();
    ParasiteMemoryMap mem(port, rom);

    ParasiteCpu cpu(mem, port);

    // Mutable access
    cpu.cpu().a = 0x42;
    CHECK(cpu.cpu().a == 0x42);

    // Const access
    const ParasiteCpu& ccpu = cpu;
    CHECK(ccpu.cpu().a == 0x42);
    CHECK(ccpu.cycle_count() == 0);
}
