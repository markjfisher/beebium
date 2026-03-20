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

// 6502/65C02 dead cycle bus behaviour tests.
//
// Verifies what address appears on the bus and what M6502ReadType is used
// during dead cycles (cycles where the CPU does internal work).
//
// Reference: "6502 Dead Cycles, V1.1, 2018" by Drass and Dr Jefyll of
// 6502.org. See docs/datasheets/6502 Dead Cycles.pdf
//
// Related: B2 issues #570, #571.

#include <catch2/catch_test_macros.hpp>
#include <6502/6502.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

// -----------------------------------------------------------------------
// Test harness
// -----------------------------------------------------------------------

struct CycleRecord {
    uint16_t abus;
    uint8_t dbus;
    uint8_t read;  // M6502ReadType value; 0 = write
};

class TestCpu {
public:
    explicit TestCpu(const M6502Config* config) {
        ram_.fill(0);
        M6502_Init(&cpu_, config);
    }

    ~TestCpu() {
        M6502_Destroy(&cpu_);
    }

    void plant(uint16_t addr, std::initializer_list<uint8_t> bytes) {
        auto it = bytes.begin();
        for (uint16_t a = addr; it != bytes.end(); ++a, ++it) {
            ram_[a] = *it;
        }
    }

    void set_pc(uint16_t pc) {
        cpu_.tfn = &M6502_NextInstruction;
        cpu_.pc.w = pc;
        // Tick once to enter opcode-fetch state (abus=PC, read=Opcode).
        // Don't service the bus -- run_instruction will record this as cycle 1.
        (*cpu_.tfn)(&cpu_);
    }

    void set_a(uint8_t v) { cpu_.a = v; }
    void set_x(uint8_t v) { cpu_.x = v; }
    void set_y(uint8_t v) { cpu_.y = v; }

    void set_p(uint8_t v) { M6502_SetP(&cpu_, v); }

    // Set the Z flag (for branch tests).
    void set_z(bool z) {
        auto p = M6502_GetP(&cpu_);
        if (z) p.bits.z = 1; else p.bits.z = 0;
        M6502_SetP(&cpu_, p.value);
    }

    // Set the D flag (for BCD tests).
    void set_d(bool d) {
        auto p = M6502_GetP(&cpu_);
        if (d) p.bits.d = 1; else p.bits.d = 0;
        M6502_SetP(&cpu_, p.value);
    }

    // Set the C flag (for SBC/ROL/ROR tests).
    void set_c(bool c) {
        auto p = M6502_GetP(&cpu_);
        if (c) p.bits.c = 1; else p.bits.c = 0;
        M6502_SetP(&cpu_, p.value);
    }

    // Run one instruction, recording every cycle.
    // Assumes the CPU is in opcode-fetch state (after set_pc).
    //
    // The 6502 model: each tfn call runs phi2 of the current cycle and
    // phi1 of the next.  After set_pc, the CPU has abus=PC, read=Opcode
    // (opcode fetch is set up but not yet serviced).  We record that as
    // cycle 1, service the bus, then tick until the next opcode fetch.
    void run_instruction() {
        cycles_.clear();

        // Record the opcode fetch (already set up by set_pc)
        cycles_.push_back({cpu_.abus.w, cpu_.dbus, cpu_.read});
        cpu_.dbus = ram_[cpu_.abus.w];

        for (int safety = 0; safety < 20; ++safety) {
            (*cpu_.tfn)(&cpu_);

            // Stop when the next instruction's opcode fetch is set up
            if (M6502_IsAboutToExecute(&cpu_)) {
                break;
            }

            cycles_.push_back({cpu_.abus.w, cpu_.dbus, cpu_.read});

            // Service the bus
            if (cpu_.read) {
                cpu_.dbus = ram_[cpu_.abus.w];
            } else {
                ram_[cpu_.abus.w] = cpu_.dbus;
            }
        }
    }

    const std::vector<CycleRecord>& cycles() const { return cycles_; }
    size_t num_cycles() const { return cycles_.size(); }

    M6502& cpu() { return cpu_; }
    uint8_t ram(uint16_t addr) const { return ram_[addr]; }

private:
    M6502 cpu_{};
    std::array<uint8_t, 65536> ram_{};
    std::vector<CycleRecord> cycles_;
};

// Shorthand for read types
constexpr uint8_t WRITE       = 0;
constexpr uint8_t DATA        = M6502ReadType_Data;
constexpr uint8_t INSTRUCTION = M6502ReadType_Instruction;
constexpr uint8_t UNINTERESTING = M6502ReadType_Uninteresting;
constexpr uint8_t OPCODE      = M6502ReadType_Opcode;

}  // namespace

// =======================================================================
// Category 3: Simple dead cycles (Rows 19, 24-26)
// =======================================================================

TEST_CASE("Row 19: implied instruction dead cycle", "[6502][dead-cycles]") {
    // All 1-byte opcodes have a dead cycle at cycle 2 that reads PC.
    // Same for both NMOS and 65C02.

    SECTION("NMOS INX") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xE8});  // INX
        cpu.set_pc(0x0400);
        cpu.set_x(0x41);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 2);
        CHECK(cpu.cycles()[0].abus == 0x0400);
        CHECK(cpu.cycles()[0].read == OPCODE);
        // Cycle 2: dead cycle reads PC (address of next instruction)
        CHECK(cpu.cycles()[1].abus == 0x0401);
        CHECK(cpu.cycles()[1].read == INSTRUCTION);
        CHECK(cpu.cpu().x == 0x42);
    }

    SECTION("65C02 NOP") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xEA});  // NOP
        cpu.set_pc(0x0400);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 2);
        CHECK(cpu.cycles()[0].abus == 0x0400);
        CHECK(cpu.cycles()[0].read == OPCODE);
        CHECK(cpu.cycles()[1].abus == 0x0401);
        CHECK(cpu.cycles()[1].read == INSTRUCTION);
    }
}

TEST_CASE("Row 24: branch taken no page cross", "[6502][dead-cycles]") {
    // Dead cycle 3 reads PC (address after the branch instruction).

    SECTION("NMOS BEQ taken") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xF0, 0x10});  // BEQ +$10 -> $0412
        cpu.set_pc(0x0400);
        cpu.set_z(true);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 3);
        CHECK(cpu.cycles()[0].abus == 0x0400);
        CHECK(cpu.cycles()[0].read == OPCODE);
        CHECK(cpu.cycles()[1].abus == 0x0401);
        CHECK(cpu.cycles()[1].read == INSTRUCTION);
        // Dead cycle: reads PC
        CHECK(cpu.cycles()[2].abus == 0x0402);
        CHECK(cpu.cycles()[2].read == INSTRUCTION);
        // Verify PC ends up at target
        CHECK(cpu.cpu().abus.w == 0x0412);
    }

    SECTION("NMOS BEQ not taken - no dead cycle") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xF0, 0x10});
        cpu.set_pc(0x0400);
        cpu.set_z(false);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 2);
        CHECK(cpu.cpu().abus.w == 0x0402);
    }
}

TEST_CASE("Row 25: branch taken with page crossing", "[6502][dead-cycles]") {
    // Dead cycle 3 reads PC, dead cycle 4 reads uncorrected target.

    SECTION("Forward crossing") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x04F0, {0xF0, 0x70});  // BEQ +$70 -> $04F2+$70=$0562
        cpu.set_pc(0x04F0);
        cpu.set_z(true);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 4);
        CHECK(cpu.cycles()[0].abus == 0x04F0);
        CHECK(cpu.cycles()[0].read == OPCODE);
        CHECK(cpu.cycles()[1].abus == 0x04F1);
        CHECK(cpu.cycles()[1].read == INSTRUCTION);
        // Dead cycle 3: reads PC
        CHECK(cpu.cycles()[2].abus == 0x04F2);
        CHECK(cpu.cycles()[2].read == INSTRUCTION);
        // Dead cycle 4: uncorrected target (old page, new low byte)
        CHECK(cpu.cycles()[3].abus == 0x0462);
        CHECK(cpu.cycles()[3].read == INSTRUCTION);
        // Final PC
        CHECK(cpu.cpu().abus.w == 0x0562);
    }

    SECTION("Backward crossing") {
        TestCpu cpu(&M6502_nmos6502_config);
        // BEQ -$80 at $0410 -> target $0412 - $80 = $0392
        cpu.plant(0x0410, {0xF0, 0x80});
        cpu.set_pc(0x0410);
        cpu.set_z(true);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 4);
        // Dead cycle 4: old page ($04), new low byte ($92)
        CHECK(cpu.cycles()[3].abus == 0x0492);
        CHECK(cpu.cycles()[3].read == INSTRUCTION);
        CHECK(cpu.cpu().abus.w == 0x0392);
    }
}

TEST_CASE("Row 26: BCD extra cycle (65C02 only)", "[6502][dead-cycles]") {

    SECTION("ADC immediate with D=1") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0x69, 0x09});  // ADC #$09
        cpu.set_pc(0x0400);
        cpu.set_a(0x01);
        cpu.set_d(true);
        cpu.set_c(false);
        cpu.run_instruction();

        // 65C02 BCD: 3 cycles (opcode, operand, BCD adjust)
        REQUIRE(cpu.num_cycles() == 3);
        CHECK(cpu.cycles()[0].read == OPCODE);
        CHECK(cpu.cycles()[1].read == INSTRUCTION);
        // Dead cycle: BCD adjust, reads with Uninteresting
        CHECK(cpu.cycles()[2].read == UNINTERESTING);
        // Result: $01 + $09 = $10 (BCD)
        CHECK(cpu.cpu().a == 0x10);
    }

    SECTION("ADC immediate with D=0 - no extra cycle") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0x69, 0x09});
        cpu.set_pc(0x0400);
        cpu.set_a(0x01);
        cpu.set_d(false);
        cpu.set_c(false);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 2);
        CHECK(cpu.cpu().a == 0x0A);
    }

    SECTION("SBC immediate with D=1") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xE9, 0x05});  // SBC #$05
        cpu.set_pc(0x0400);
        cpu.set_a(0x20);
        cpu.set_d(true);
        cpu.set_c(true);  // C=1 means no borrow
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 3);
        CHECK(cpu.cycles()[2].read == UNINTERESTING);
        // Result: $20 - $05 = $15 (BCD)
        CHECK(cpu.cpu().a == 0x15);
    }

    SECTION("NMOS ADC immediate with D=1 - no extra cycle") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0x69, 0x09});
        cpu.set_pc(0x0400);
        cpu.set_a(0x01);
        cpu.set_d(true);
        cpu.set_c(false);
        cpu.run_instruction();

        // NMOS: 2 cycles regardless of D flag
        REQUIRE(cpu.num_cycles() == 2);
    }
}

// =======================================================================
// Category 1: Indexed addressing dead cycles (Rows 1-8)
// =======================================================================

TEST_CASE("Row 1: zpg,X dead cycle", "[6502][dead-cycles]") {
    // Cycle 3 is the dead cycle where BAL+X is computed internally.
    // NMOS reads PFA (unindexed ZP address), 65C02 should read PBA.

    SECTION("NMOS LDA zpg,X") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xB5, 0x80});  // LDA $80,X
        cpu.plant(0x0090, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 4);
        CHECK(cpu.cycles()[0].abus == 0x0400);  // Opcode fetch
        CHECK(cpu.cycles()[1].abus == 0x0401);  // Operand fetch
        // Dead cycle: PFA = unindexed ZP address
        CHECK(cpu.cycles()[2].abus == 0x0080);
        CHECK(cpu.cycles()[2].read == UNINTERESTING);
        CHECK(cpu.cycles()[3].abus == 0x0090);  // Effective address
        CHECK(cpu.cycles()[3].read == DATA);
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("65C02 LDA zpg,X") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xB5, 0x80});
        cpu.plant(0x0090, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 4);
        // Dead cycles reference: 65C02 should read PBA ($0401)
        // Actual: reads PFA ($0080) -- shared NMOS code, see B2 issue #570
        CHECK(cpu.cycles()[2].abus == 0x0080);  // PFA (NMOS behaviour)
        CHECK(cpu.cycles()[2].read == UNINTERESTING);
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("NMOS zero page wrapping") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xB5, 0x80});  // LDA $80,X
        cpu.plant(0x0010, {0x77});
        cpu.set_pc(0x0400);
        cpu.set_x(0x90);  // $80 + $90 = $10 (wraps)
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 4);
        CHECK(cpu.cycles()[3].abus == 0x0010);  // Wrapped effective address
        CHECK(cpu.cpu().a == 0x77);
    }
}

TEST_CASE("Row 2: abs,X/Y read with page crossing", "[6502][dead-cycles]") {
    // Cycle 4 is the dead cycle when page crossing occurs.

    SECTION("NMOS LDA abs,X page cross") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xBD, 0xFE, 0x10});  // LDA $10FE,X
        cpu.plant(0x110E, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        CHECK(cpu.cycles()[0].abus == 0x0400);
        CHECK(cpu.cycles()[1].abus == 0x0401);
        CHECK(cpu.cycles()[2].abus == 0x0402);
        // Dead cycle: PFA (uncorrected high byte)
        CHECK(cpu.cycles()[3].abus == 0x100E);
        CHECK(cpu.cycles()[3].read != WRITE);
        CHECK(cpu.cycles()[4].abus == 0x110E);
        CHECK(cpu.cycles()[4].read == DATA);
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("65C02 LDA abs,X page cross") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xBD, 0xFE, 0x10});
        cpu.plant(0x110E, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        // Dead cycle: 65C02 hand-written code correctly reads PBA ($0402)
        CHECK(cpu.cycles()[3].abus == 0x0402);
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
        CHECK(cpu.cycles()[4].abus == 0x110E);
        CHECK(cpu.cycles()[4].read == DATA);
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("65C02 LDA abs,Y page cross") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xB9, 0xFE, 0x10});  // LDA $10FE,Y
        cpu.plant(0x110E, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_y(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        CHECK(cpu.cycles()[3].abus == 0x0402);  // PBA
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("No page crossing - no dead cycle") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xBD, 0xFE, 0x10});  // LDA $10FE,X
        cpu.plant(0x10FF, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_x(0x01);  // $10FE + $01 = $10FF, no cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 4);
        CHECK(cpu.cycles()[3].abus == 0x10FF);
        CHECK(cpu.cycles()[3].read == DATA);
    }
}

TEST_CASE("Rows 3-4: abs,X write dead cycle", "[6502][dead-cycles]") {
    // Write operations always have a dead cycle at cycle 4.

    SECTION("Row 3: NMOS no page crossing - reads FFA") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0x9D, 0xFE, 0x10});  // STA $10FE,X
        cpu.set_pc(0x0400);
        cpu.set_a(0x42);
        cpu.set_x(0x01);  // Effective = $10FF, no cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        // Dead cycle: FFA for NMOS (no page crossing)
        CHECK(cpu.cycles()[3].abus == 0x10FF);
        CHECK(cpu.cycles()[4].abus == 0x10FF);
        CHECK(cpu.cycles()[4].read == WRITE);
        CHECK(cpu.ram(0x10FF) == 0x42);
    }

    SECTION("Row 4: NMOS page crossing - reads PFA") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0x9D, 0xFE, 0x10});
        cpu.set_pc(0x0400);
        cpu.set_a(0x42);
        cpu.set_x(0x10);  // Effective = $110E, cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        // Dead cycle: PFA (uncorrected)
        CHECK(cpu.cycles()[3].abus == 0x100E);
        CHECK(cpu.cycles()[4].abus == 0x110E);
        CHECK(cpu.cycles()[4].read == WRITE);
    }

    SECTION("Row 3: 65C02 no page crossing - reads FFA") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0x9D, 0xFE, 0x10});
        cpu.set_pc(0x0400);
        cpu.set_a(0x42);
        cpu.set_x(0x01);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        // 65C02: FFA when no page crossing
        CHECK(cpu.cycles()[3].abus == 0x10FF);
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
    }

    SECTION("Row 4: 65C02 page crossing - reads PBA") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0x9D, 0xFE, 0x10});
        cpu.set_pc(0x0400);
        cpu.set_a(0x42);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        // 65C02: PBA when page crossing
        CHECK(cpu.cycles()[3].abus == 0x0402);
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
    }
}

TEST_CASE("Row 5: (zpg),Y read with page crossing", "[6502][dead-cycles]") {
    // This is the CE2023 bug row.

    SECTION("NMOS") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xB1, 0x10});      // LDA ($10),Y
        cpu.plant(0x0010, {0x80, 0xFE});       // Pointer = $FE80
        cpu.plant(0xFF10, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_y(0x90);  // $FE80 + $90 = $FF10, page cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        CHECK(cpu.cycles()[0].abus == 0x0400);  // Opcode
        CHECK(cpu.cycles()[1].abus == 0x0401);  // Operand
        CHECK(cpu.cycles()[2].abus == 0x0010);  // Ptr low
        CHECK(cpu.cycles()[3].abus == 0x0011);  // Ptr high
        // Dead cycle: PFA (uncorrected address)
        CHECK(cpu.cycles()[4].abus == 0xFE10);
        CHECK(cpu.cycles()[5].abus == 0xFF10);  // Corrected
        CHECK(cpu.cycles()[5].read == DATA);
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("65C02") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xB1, 0x10});
        cpu.plant(0x0010, {0x80, 0xFE});
        cpu.plant(0xFF10, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_y(0x90);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        // Dead cycles reference: 65C02 should read PBA ($0011)
        // Actual: reads PFA ($FE10) -- shared NMOS code, see B2 issue #570
        CHECK(cpu.cycles()[4].abus == 0xFE10);  // PFA (NMOS behaviour)
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("No page crossing - no dead cycle") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xB1, 0x10});
        cpu.plant(0x0010, {0x80, 0xFE});
        cpu.plant(0xFE81, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_y(0x01);  // $FE80 + $01 = $FE81, no cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        CHECK(cpu.cycles()[4].abus == 0xFE81);
        CHECK(cpu.cycles()[4].read == DATA);
    }
}

TEST_CASE("Rows 6-7: (zpg),Y write dead cycle", "[6502][dead-cycles]") {

    SECTION("Row 6: no page crossing") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0x91, 0x10});       // STA ($10),Y
        cpu.plant(0x0010, {0x80, 0xFE});       // Pointer = $FE80
        cpu.set_pc(0x0400);
        cpu.set_a(0x42);
        cpu.set_y(0x01);  // $FE80 + $01 = $FE81, no cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        // Dead cycle: PFA = FFA when no page crossing
        CHECK(cpu.cycles()[4].abus == 0xFE81);
        CHECK(cpu.cycles()[5].abus == 0xFE81);
        CHECK(cpu.cycles()[5].read == WRITE);
        CHECK(cpu.ram(0xFE81) == 0x42);
    }

    SECTION("Row 7: NMOS page crossing - reads PFA") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0x91, 0x10});
        cpu.plant(0x0010, {0x80, 0xFE});
        cpu.set_pc(0x0400);
        cpu.set_a(0x42);
        cpu.set_y(0x90);  // $FE80 + $90 = $FF10, cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        CHECK(cpu.cycles()[4].abus == 0xFE10);  // PFA
        CHECK(cpu.cycles()[5].abus == 0xFF10);
        CHECK(cpu.cycles()[5].read == WRITE);
    }

    SECTION("Row 7: 65C02 page crossing") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0x91, 0x10});
        cpu.plant(0x0010, {0x80, 0xFE});
        cpu.set_pc(0x0400);
        cpu.set_a(0x42);
        cpu.set_y(0x90);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        // Dead cycles reference: 65C02 should read PBA ($0011)
        // Actual: reads PFA ($FE10) -- shared NMOS code, see B2 issue #570
        CHECK(cpu.cycles()[4].abus == 0xFE10);  // PFA (NMOS behaviour)
    }
}

TEST_CASE("Row 8: (zpg,X) dead cycle", "[6502][dead-cycles]") {
    // Cycle 3 is the dead cycle where BAL+X is computed.

    SECTION("NMOS") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xA1, 0x80});       // LDA ($80,X)
        cpu.plant(0x0090, {0x10, 0xFE});       // Pointer at $90 = $FE10
        cpu.plant(0xFE10, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);  // $80 + $10 = $90
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        CHECK(cpu.cycles()[0].abus == 0x0400);  // Opcode
        CHECK(cpu.cycles()[1].abus == 0x0401);  // Operand
        // Dead cycle: PFA (unindexed ZP address)
        CHECK(cpu.cycles()[2].abus == 0x0080);
        CHECK(cpu.cycles()[2].read == UNINTERESTING);
        CHECK(cpu.cycles()[3].abus == 0x0090);  // Ptr low
        CHECK(cpu.cycles()[4].abus == 0x0091);  // Ptr high
        CHECK(cpu.cycles()[5].abus == 0xFE10);  // Effective
        CHECK(cpu.cpu().a == 0x42);
    }

    SECTION("65C02") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xA1, 0x80});
        cpu.plant(0x0090, {0x10, 0xFE});
        cpu.plant(0xFE10, {0x42});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        // Dead cycles reference: 65C02 should read PBA ($0401)
        // Actual: reads PFA ($0080) -- shared NMOS code, see B2 issue #570
        CHECK(cpu.cycles()[2].abus == 0x0080);  // PFA (NMOS behaviour)
        CHECK(cpu.cpu().a == 0x42);
    }
}

// =======================================================================
// Category 2: RMW dead cycles (Rows 9-18)
// =======================================================================

TEST_CASE("Row 9: RMW zpg,X dead cycle", "[6502][dead-cycles]") {

    SECTION("NMOS INC zpg,X") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xF6, 0x80});  // INC $80,X
        cpu.plant(0x0090, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        // Dead cycle 3: PFA (unindexed ZP address)
        CHECK(cpu.cycles()[2].abus == 0x0080);
        CHECK(cpu.ram(0x0090) == 0x42);
    }

    SECTION("65C02 INC zpg,X") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xF6, 0x80});
        cpu.plant(0x0090, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        // 65C02 has hand-written CMOS code: correctly reads PBA
        CHECK(cpu.cycles()[2].abus == 0x0401);  // PBA
        CHECK(cpu.cycles()[2].read == UNINTERESTING);
        CHECK(cpu.ram(0x0090) == 0x42);
    }
}

TEST_CASE("Rows 10-11: RMW abs,X NMOS dead cycle", "[6502][dead-cycles]") {

    SECTION("Row 10: no page crossing - reads FFA") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xFE, 0xFE, 0x10});  // INC $10FE,X
        cpu.plant(0x10FF, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x01);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 7);
        // Dead cycle 4: FFA (no page crossing)
        CHECK(cpu.cycles()[3].abus == 0x10FF);
        CHECK(cpu.ram(0x10FF) == 0x42);
    }

    SECTION("Row 11: page crossing - reads PFA") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xFE, 0xFE, 0x10});
        cpu.plant(0x110E, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 7);
        // Dead cycle 4: PFA
        CHECK(cpu.cycles()[3].abus == 0x100E);
        CHECK(cpu.ram(0x110E) == 0x42);
    }
}

TEST_CASE("Rows 12-13: RMW abs,X 65C02 INC/DEC dead cycle", "[6502][dead-cycles]") {

    SECTION("Row 12: no page crossing - reads FFA") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xFE, 0xFE, 0x10});  // INC $10FE,X
        cpu.plant(0x10FF, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x01);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 7);
        CHECK(cpu.cycles()[3].abus == 0x10FF);  // FFA
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
        CHECK(cpu.ram(0x10FF) == 0x42);
    }

    SECTION("Row 13: page crossing - reads PBA") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xFE, 0xFE, 0x10});
        cpu.plant(0x110E, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 7);
        CHECK(cpu.cycles()[3].abus == 0x0402);  // PBA
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
        CHECK(cpu.ram(0x110E) == 0x42);
    }
}

TEST_CASE("Row 14: RMW abs,X 65C02 ASL/LSR/ROL/ROR dead cycle", "[6502][dead-cycles]") {

    SECTION("ASL abs,X page crossing - reads PBA") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0x1E, 0xFE, 0x10});  // ASL $10FE,X
        cpu.plant(0x110E, {0x21});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        // Dead cycle: PBA
        CHECK(cpu.cycles()[3].abus == 0x0402);
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
        CHECK(cpu.ram(0x110E) == 0x42);  // $21 << 1 = $42
    }
}

TEST_CASE("Rows 15-18: RMW modify cycle", "[6502][dead-cycles]") {
    // NMOS: modify cycle WRITES the unmodified value (two writes).
    // 65C02: modify cycle READS with Uninteresting (read then write).

    SECTION("Row 15: zpg NMOS writes unmodified") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xE6, 0x80});  // INC $80
        cpu.plant(0x0080, {0x41});
        cpu.set_pc(0x0400);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        // Cycle 4: modify - NMOS writes unmodified value
        CHECK(cpu.cycles()[3].abus == 0x0080);
        CHECK(cpu.cycles()[3].read == WRITE);
        CHECK(cpu.cycles()[3].dbus == 0x41);  // Unmodified
        // Cycle 5: write modified value
        CHECK(cpu.cycles()[4].abus == 0x0080);
        CHECK(cpu.cycles()[4].read == WRITE);
        CHECK(cpu.ram(0x0080) == 0x42);
    }

    SECTION("Row 15: zpg 65C02 reads during modify") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xE6, 0x80});
        cpu.plant(0x0080, {0x41});
        cpu.set_pc(0x0400);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 5);
        // Cycle 4: modify - 65C02 reads (not writes)
        CHECK(cpu.cycles()[3].abus == 0x0080);
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
        // Cycle 5: write modified value
        CHECK(cpu.cycles()[4].abus == 0x0080);
        CHECK(cpu.cycles()[4].read == WRITE);
        CHECK(cpu.ram(0x0080) == 0x42);
    }

    SECTION("Row 16: abs NMOS writes unmodified") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xEE, 0x80, 0x10});  // INC $1080
        cpu.plant(0x1080, {0x41});
        cpu.set_pc(0x0400);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        // Cycle 5: modify - NMOS writes unmodified
        CHECK(cpu.cycles()[4].abus == 0x1080);
        CHECK(cpu.cycles()[4].read == WRITE);
        CHECK(cpu.cycles()[4].dbus == 0x41);
        CHECK(cpu.ram(0x1080) == 0x42);
    }

    SECTION("Row 16: abs 65C02 reads during modify") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xEE, 0x80, 0x10});
        cpu.plant(0x1080, {0x41});
        cpu.set_pc(0x0400);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        CHECK(cpu.cycles()[4].abus == 0x1080);
        CHECK(cpu.cycles()[4].read == UNINTERESTING);
        CHECK(cpu.ram(0x1080) == 0x42);
    }

    SECTION("Row 17: zpg,X NMOS writes unmodified") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xF6, 0x80});  // INC $80,X
        cpu.plant(0x0090, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        CHECK(cpu.cycles()[4].abus == 0x0090);
        CHECK(cpu.cycles()[4].read == WRITE);
        CHECK(cpu.cycles()[4].dbus == 0x41);
        CHECK(cpu.ram(0x0090) == 0x42);
    }

    SECTION("Row 17: zpg,X 65C02 reads during modify") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xF6, 0x80});
        cpu.plant(0x0090, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x10);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 6);
        CHECK(cpu.cycles()[4].abus == 0x0090);
        CHECK(cpu.cycles()[4].read == UNINTERESTING);
        CHECK(cpu.ram(0x0090) == 0x42);
    }

    SECTION("Row 18: abs,X NMOS writes unmodified") {
        TestCpu cpu(&M6502_nmos6502_config);
        cpu.plant(0x0400, {0xFE, 0xFE, 0x10});  // INC $10FE,X
        cpu.plant(0x10FF, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x01);  // No page cross
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 7);
        CHECK(cpu.cycles()[5].abus == 0x10FF);
        CHECK(cpu.cycles()[5].read == WRITE);
        CHECK(cpu.cycles()[5].dbus == 0x41);
        CHECK(cpu.ram(0x10FF) == 0x42);
    }

    SECTION("Row 18: abs,X 65C02 INC") {
        TestCpu cpu(&M6502_rockwell65c02_config);
        cpu.plant(0x0400, {0xFE, 0xFE, 0x10});
        cpu.plant(0x10FF, {0x41});
        cpu.set_pc(0x0400);
        cpu.set_x(0x01);
        cpu.run_instruction();

        REQUIRE(cpu.num_cycles() == 7);
        // Cycle 3 (index 3): first dead cycle - Uninteresting
        CHECK(cpu.cycles()[3].read == UNINTERESTING);
        // Cycle 4 (index 4): second dead cycle - Uninteresting
        CHECK(cpu.cycles()[4].read == UNINTERESTING);
        // Cycle 5 (index 5): actual data read from FFA
        CHECK(cpu.cycles()[5].abus == 0x10FF);
        CHECK(cpu.cycles()[5].read == DATA);
        // Cycle 6 (index 6): write modified value
        CHECK(cpu.cycles()[6].abus == 0x10FF);
        CHECK(cpu.cycles()[6].read == WRITE);
        CHECK(cpu.ram(0x10FF) == 0x42);
    }
}
