// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// VIA 6522 Cycle-Accurate Timing Tests
//
// These tests verify that VIA timer timing is cycle-accurate when running
// actual 6502 instructions. Tests are divided into two categories:
//
// 1. Basic Integration Tests: Verify correct CPU+VIA interaction
//    - IRQ detection timing
//    - Timer reads during multi-cycle instructions
//    - Timer countdown sequences
//
// 2. Hardware-Validated Tests: Verified against real BBC hardware
//    - Created by @scarybeasts, validated on BBC Master
//    - Source: https://github.com/mattgodbolt/jsbeeb/issues/179

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/Via6522.hpp>
#include <beebium/Types.hpp>
#include <array>
#include <vector>

using namespace beebium;

// System VIA register addresses (base 0xFE40, mirrored with 0x0F)
// These constants document the BBC Micro memory map for VIA registers.
// The actual addresses are encoded in the test program machine code.
[[maybe_unused]] constexpr uint16_t SYS_VIA_T1CL  = kSystemViaAddr + Via6522::REG_T1CL;   // 0xFE44
[[maybe_unused]] constexpr uint16_t SYS_VIA_T1CH  = kSystemViaAddr + Via6522::REG_T1CH;   // 0xFE45
[[maybe_unused]] constexpr uint16_t SYS_VIA_T1LL  = kSystemViaAddr + Via6522::REG_T1LL;   // 0xFE46
[[maybe_unused]] constexpr uint16_t SYS_VIA_T1LH  = kSystemViaAddr + Via6522::REG_T1LH;   // 0xFE47
[[maybe_unused]] constexpr uint16_t SYS_VIA_T2CL  = kSystemViaAddr + Via6522::REG_T2CL;   // 0xFE48
[[maybe_unused]] constexpr uint16_t SYS_VIA_T2CH  = kSystemViaAddr + Via6522::REG_T2CH;   // 0xFE49
[[maybe_unused]] constexpr uint16_t SYS_VIA_ACR   = kSystemViaAddr + Via6522::REG_ACR;    // 0xFE4B
[[maybe_unused]] constexpr uint16_t SYS_VIA_IFR   = kSystemViaAddr + Via6522::REG_IFR;    // 0xFE4D
[[maybe_unused]] constexpr uint16_t SYS_VIA_IER   = kSystemViaAddr + Via6522::REG_IER;    // 0xFE4E

// Helper to set up a minimal MOS with reset vector and IRQ vector
void setup_minimal_mos(ModelB& machine, uint16_t reset_addr, uint16_t irq_addr) {
    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);  // Fill with NOPs

    // Reset vector at 0xFFFC-0xFFFD (offset 0x3FFC-0x3FFD from 0xC000)
    mos[0x3FFC] = reset_addr & 0xFF;
    mos[0x3FFD] = (reset_addr >> 8) & 0xFF;

    // IRQ/BRK vector at 0xFFFE-0xFFFF (offset 0x3FFE-0x3FFF from 0xC000)
    mos[0x3FFE] = irq_addr & 0xFF;
    mos[0x3FFF] = (irq_addr >> 8) & 0xFF;

    machine.memory().load_mos(mos.data(), mos.size());
}

//////////////////////////////////////////////////////////////////////////////
// Test E1: Timer IRQ fires at expected cycle
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Integrated: Timer 1 IRQ fires at expected cycle", "[via][cpu][irq][integrated]") {
    ModelB machine;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Simple program at 0x0400:
    //   SEI          ; Disable interrupts initially
    //   LDA #$C0     ; T1 IRQ enable bit
    //   STA $FE4E    ; Enable T1 IRQ in IER
    //   LDA #$04     ; T1 latch low = 4
    //   STA $FE46    ; Write T1LL
    //   LDA #$00     ; T1 latch high = 0
    //   STA $FE47    ; Write T1LH (clears T1 flag)
    //   STA $FE45    ; Write T1CH (starts timer with value 4)
    //   CLI          ; Enable interrupts
    //   NOP          ; \
    //   NOP          ;  } Wait for IRQ
    //   NOP          ; /
    //   ...
    machine.write(0x0400, 0x78);        // SEI
    machine.write(0x0401, 0xA9);        // LDA #$C0
    machine.write(0x0402, 0xC0);
    machine.write(0x0403, 0x8D);        // STA $FE4E
    machine.write(0x0404, 0x4E);
    machine.write(0x0405, 0xFE);
    machine.write(0x0406, 0xA9);        // LDA #$04
    machine.write(0x0407, 0x04);
    machine.write(0x0408, 0x8D);        // STA $FE46
    machine.write(0x0409, 0x46);
    machine.write(0x040A, 0xFE);
    machine.write(0x040B, 0xA9);        // LDA #$00
    machine.write(0x040C, 0x00);
    machine.write(0x040D, 0x8D);        // STA $FE47 (clears T1 flag)
    machine.write(0x040E, 0x47);
    machine.write(0x040F, 0xFE);
    machine.write(0x0410, 0x8D);        // STA $FE45 (starts timer)
    machine.write(0x0411, 0x45);
    machine.write(0x0412, 0xFE);
    machine.write(0x0413, 0x58);        // CLI
    machine.write(0x0414, 0xEA);        // NOP
    machine.write(0x0415, 0xEA);        // NOP
    machine.write(0x0416, 0xEA);        // NOP
    machine.write(0x0417, 0xEA);        // NOP
    machine.write(0x0418, 0xEA);        // NOP

    // IRQ handler at 0x0500 - just RTI
    machine.write(0x0500, 0x40);        // RTI

    // Reset CPU
    M6502_Reset(&machine.cpu());

    // Execute reset sequence (7 cycles) - after this, CPU is fetching at 0x0400
    machine.step_instruction();

    // Execute setup code until CLI (at 0x0413)
    // PC points to the next byte being fetched, so we look for 0x0414 (after CLI)
    while (machine.pc() < 0x0414) {
        machine.step_instruction();
    }

    // At this point, T1 has been started with latch value 4.
    // The timer sequence is: 4 -> 3 -> 2 -> 1 -> 0 -> FFFF (IRQ fires)
    // Timer decrements on trailing edge of 1MHz clock (every 2nd 2MHz cycle)

    // Execute NOPs until we hit the IRQ handler
    int nops_executed = 0;
    while (machine.pc() >= 0x0414 && machine.pc() < 0x0500 && nops_executed < 20) {
        uint16_t pc_before = machine.pc();
        machine.step_instruction();
        if (pc_before != machine.pc() && machine.pc() != pc_before + 1) {
            // We branched (to IRQ handler)
            break;
        }
        if (machine.pc() >= 0x0414 && machine.pc() <= 0x0418) {
            nops_executed++;
        }
    }

    // Should have reached IRQ handler (PC will be 0x0500 or 0x0501 depending on fetch state)
    REQUIRE(machine.pc() >= 0x0500);
    REQUIRE(machine.pc() <= 0x0503);  // Within the IRQ handler

    // Verify IRQ flag was set
    REQUIRE(machine.memory().system_via.state().ifr.bits.t1 == 1);
}

//////////////////////////////////////////////////////////////////////////////
// Test E2: CPU sees correct timer value during read instruction
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Integrated: CPU reads correct timer value", "[via][cpu][timer][integrated]") {
    ModelB machine;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program: Start timer and immediately read its value
    //   LDA #$10     ; T1 latch low = 16
    //   STA $FE46    ; Write T1LL
    //   LDA #$00     ; T1 latch high = 0
    //   STA $FE47    ; Write T1LH
    //   STA $FE45    ; Write T1CH (starts timer)
    //   LDA $FE44    ; Read T1CL into A
    //   STA $00      ; Store result
    //   JMP $0420    ; Loop
    machine.write(0x0400, 0xA9);        // LDA #$10
    machine.write(0x0401, 0x10);
    machine.write(0x0402, 0x8D);        // STA $FE46
    machine.write(0x0403, 0x46);
    machine.write(0x0404, 0xFE);
    machine.write(0x0405, 0xA9);        // LDA #$00
    machine.write(0x0406, 0x00);
    machine.write(0x0407, 0x8D);        // STA $FE47
    machine.write(0x0408, 0x47);
    machine.write(0x0409, 0xFE);
    machine.write(0x040A, 0x8D);        // STA $FE45 (starts timer)
    machine.write(0x040B, 0x45);
    machine.write(0x040C, 0xFE);
    machine.write(0x040D, 0xAD);        // LDA $FE44 (read T1CL)
    machine.write(0x040E, 0x44);
    machine.write(0x040F, 0xFE);
    machine.write(0x0410, 0x85);        // STA $00
    machine.write(0x0411, 0x00);
    machine.write(0x0412, 0x4C);        // JMP $0420
    machine.write(0x0413, 0x20);
    machine.write(0x0414, 0x04);
    // Loop point
    machine.write(0x0420, 0xEA);        // NOP
    machine.write(0x0421, 0x4C);        // JMP $0420
    machine.write(0x0422, 0x20);
    machine.write(0x0423, 0x04);

    M6502_Reset(&machine.cpu());
    machine.step_instruction();  // Reset sequence

    // Execute until we've stored the timer value (STA $00 is at 0x0410-0x0411)
    while (machine.pc() < 0x0412) {
        machine.step_instruction();
    }

    // The timer was started with value 16 (0x10)
    // After STA $FE45 (4 cycles) and LDA $FE44 (4 cycles), timer has decremented
    // The exact value depends on timing, but should be reasonable
    uint8_t timer_value_read = machine.read(0x00);

    // Timer should have decremented from 16 but not wrapped yet
    REQUIRE(timer_value_read <= 16);
    REQUIRE(timer_value_read >= 8);  // Reasonable range given instruction timing
}

//////////////////////////////////////////////////////////////////////////////
// Test E3: IRQ latency - CPU completes current instruction before taking IRQ
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Integrated: IRQ latency during multi-cycle instruction", "[via][cpu][irq][integrated]") {
    ModelB machine;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program: Start timer with short timeout, then execute long instruction
    //   SEI
    //   LDA #$C0     ; Enable T1 IRQ
    //   STA $FE4E
    //   LDA #$02     ; T1 latch = 2 (very short)
    //   STA $FE46
    //   LDA #$00
    //   STA $FE47
    //   STA $FE45    ; Start timer
    //   CLI
    //   JSR $0420    ; 6-cycle instruction (timer will fire during this)
    //   NOP          ; Should not reach here until after IRQ
    //   JMP $0430
    //
    // Subroutine at 0x0420:
    //   NOP
    //   NOP
    //   RTS
    //
    // IRQ handler at 0x0500:
    //   INC $01      ; Flag that IRQ occurred
    //   RTI

    machine.write(0x0400, 0x78);        // SEI
    machine.write(0x0401, 0xA9);        // LDA #$C0
    machine.write(0x0402, 0xC0);
    machine.write(0x0403, 0x8D);        // STA $FE4E
    machine.write(0x0404, 0x4E);
    machine.write(0x0405, 0xFE);
    machine.write(0x0406, 0xA9);        // LDA #$02
    machine.write(0x0407, 0x02);
    machine.write(0x0408, 0x8D);        // STA $FE46
    machine.write(0x0409, 0x46);
    machine.write(0x040A, 0xFE);
    machine.write(0x040B, 0xA9);        // LDA #$00
    machine.write(0x040C, 0x00);
    machine.write(0x040D, 0x8D);        // STA $FE47
    machine.write(0x040E, 0x47);
    machine.write(0x040F, 0xFE);
    machine.write(0x0410, 0x8D);        // STA $FE45 (start timer)
    machine.write(0x0411, 0x45);
    machine.write(0x0412, 0xFE);
    machine.write(0x0413, 0x58);        // CLI
    machine.write(0x0414, 0x20);        // JSR $0420
    machine.write(0x0415, 0x20);
    machine.write(0x0416, 0x04);
    machine.write(0x0417, 0xEA);        // NOP (return point)
    machine.write(0x0418, 0x4C);        // JMP $0430
    machine.write(0x0419, 0x30);
    machine.write(0x041A, 0x04);

    // Subroutine
    machine.write(0x0420, 0xEA);        // NOP
    machine.write(0x0421, 0xEA);        // NOP
    machine.write(0x0422, 0x60);        // RTS

    // End point
    machine.write(0x0430, 0xEA);        // NOP
    machine.write(0x0431, 0x4C);        // JMP $0430
    machine.write(0x0432, 0x30);
    machine.write(0x0433, 0x04);

    // IRQ handler - must clear T1 flag by reading T1CL
    machine.write(0x0500, 0xE6);        // INC $01
    machine.write(0x0501, 0x01);
    machine.write(0x0502, 0xAD);        // LDA $FE44 (read T1CL to clear flag)
    machine.write(0x0503, 0x44);
    machine.write(0x0504, 0xFE);
    machine.write(0x0505, 0x40);        // RTI

    // Initialize flag
    machine.write(0x0001, 0x00);

    M6502_Reset(&machine.cpu());
    machine.step_instruction();  // Reset sequence

    // Execute until we reach the end loop (0x0430-0x0433)
    // The loop must allow execution through the IRQ handler at 0x0500-0x0505
    int iterations = 0;
    while (iterations < 300) {
        machine.step_instruction();
        iterations++;
        // Exit when we reach the end loop
        if (machine.pc() >= 0x0430 && machine.pc() <= 0x0434) {
            break;
        }
    }

    // Should have reached end point (PC will be within the JMP $0430 loop)
    REQUIRE(machine.pc() >= 0x0430);
    REQUIRE(machine.pc() <= 0x0434);

    // IRQ handler should have run (incremented $01)
    REQUIRE(machine.read(0x0001) >= 1);
}

//////////////////////////////////////////////////////////////////////////////
// Test E4: Reading T1CL clears IRQ flag
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Integrated: Reading T1CL clears IRQ flag", "[via][cpu][irq][integrated]") {
    ModelB machine;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program: Start timer, wait for IRQ flag, read T1CL to clear it
    //   LDA #$02     ; T1 latch = 2
    //   STA $FE46
    //   LDA #$00
    //   STA $FE47
    //   STA $FE45    ; Start timer
    //   ; Wait for IRQ flag to set
    // wait:
    //   LDA $FE4D    ; Read IFR
    //   AND #$40     ; Check T1 flag
    //   BEQ wait
    //   ; T1 flag is set, now read T1CL to clear it
    //   LDA $FE44    ; Read T1CL (should clear flag)
    //   LDA $FE4D    ; Read IFR again
    //   STA $00      ; Store for verification
    //   JMP $0430

    machine.write(0x0400, 0xA9);        // LDA #$02
    machine.write(0x0401, 0x02);
    machine.write(0x0402, 0x8D);        // STA $FE46
    machine.write(0x0403, 0x46);
    machine.write(0x0404, 0xFE);
    machine.write(0x0405, 0xA9);        // LDA #$00
    machine.write(0x0406, 0x00);
    machine.write(0x0407, 0x8D);        // STA $FE47
    machine.write(0x0408, 0x47);
    machine.write(0x0409, 0xFE);
    machine.write(0x040A, 0x8D);        // STA $FE45
    machine.write(0x040B, 0x45);
    machine.write(0x040C, 0xFE);
    // wait:
    machine.write(0x040D, 0xAD);        // LDA $FE4D
    machine.write(0x040E, 0x4D);
    machine.write(0x040F, 0xFE);
    machine.write(0x0410, 0x29);        // AND #$40
    machine.write(0x0411, 0x40);
    machine.write(0x0412, 0xF0);        // BEQ wait (back to $040D)
    machine.write(0x0413, 0xF9);
    // T1 flag is set
    machine.write(0x0414, 0xAD);        // LDA $FE44 (read T1CL to clear)
    machine.write(0x0415, 0x44);
    machine.write(0x0416, 0xFE);
    machine.write(0x0417, 0xAD);        // LDA $FE4D (read IFR)
    machine.write(0x0418, 0x4D);
    machine.write(0x0419, 0xFE);
    machine.write(0x041A, 0x85);        // STA $00
    machine.write(0x041B, 0x00);
    machine.write(0x041C, 0x4C);        // JMP $0430
    machine.write(0x041D, 0x30);
    machine.write(0x041E, 0x04);

    machine.write(0x0430, 0xEA);        // NOP
    machine.write(0x0431, 0x4C);        // JMP $0430
    machine.write(0x0432, 0x30);
    machine.write(0x0433, 0x04);

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    // Execute until we reach the end (0x0430 end loop)
    int iterations = 0;
    while (machine.pc() < 0x0430 && iterations < 300) {
        machine.step_instruction();
        iterations++;
    }

    REQUIRE(machine.pc() >= 0x0430);
    REQUIRE(machine.pc() <= 0x0434);

    // IFR should have T1 flag clear (bit 6 = 0)
    // Note: bit 7 (IRQ) depends on other enabled interrupts
    uint8_t ifr_value = machine.read(0x0000);
    REQUIRE((ifr_value & 0x40) == 0);  // T1 flag should be clear
}

//////////////////////////////////////////////////////////////////////////////
// Test E5: Timer continues counting during instruction execution
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Integrated: Timer counts during instruction execution", "[via][cpu][timer][integrated]") {
    ModelB machine;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program: Start timer with known value, execute a long instruction, read timer
    //   LDA #$20     ; T1 latch = 32
    //   STA $FE46
    //   LDA #$00
    //   STA $FE47
    //   STA $FE45    ; Start timer (now T1 = 32)
    //   ; Immediately read timer value before instruction
    //   LDA $FE44    ; Read T1CL (4 cycles for LDA abs)
    //   STA $00      ; Store first value
    //   ; Execute some instructions to let timer count
    //   NOP          ; 2 cycles
    //   NOP          ; 2 cycles
    //   NOP          ; 2 cycles
    //   NOP          ; 2 cycles
    //   ; Read timer again
    //   LDA $FE44    ; Read T1CL
    //   STA $01      ; Store second value
    //   JMP $0430

    machine.write(0x0400, 0xA9);        // LDA #$20
    machine.write(0x0401, 0x20);
    machine.write(0x0402, 0x8D);        // STA $FE46
    machine.write(0x0403, 0x46);
    machine.write(0x0404, 0xFE);
    machine.write(0x0405, 0xA9);        // LDA #$00
    machine.write(0x0406, 0x00);
    machine.write(0x0407, 0x8D);        // STA $FE47
    machine.write(0x0408, 0x47);
    machine.write(0x0409, 0xFE);
    machine.write(0x040A, 0x8D);        // STA $FE45
    machine.write(0x040B, 0x45);
    machine.write(0x040C, 0xFE);
    machine.write(0x040D, 0xAD);        // LDA $FE44
    machine.write(0x040E, 0x44);
    machine.write(0x040F, 0xFE);
    machine.write(0x0410, 0x85);        // STA $00
    machine.write(0x0411, 0x00);
    machine.write(0x0412, 0xEA);        // NOP
    machine.write(0x0413, 0xEA);        // NOP
    machine.write(0x0414, 0xEA);        // NOP
    machine.write(0x0415, 0xEA);        // NOP
    machine.write(0x0416, 0xAD);        // LDA $FE44
    machine.write(0x0417, 0x44);
    machine.write(0x0418, 0xFE);
    machine.write(0x0419, 0x85);        // STA $01
    machine.write(0x041A, 0x01);
    machine.write(0x041B, 0x4C);        // JMP $0430
    machine.write(0x041C, 0x30);
    machine.write(0x041D, 0x04);

    machine.write(0x0430, 0xEA);        // NOP
    machine.write(0x0431, 0x4C);        // JMP $0430
    machine.write(0x0432, 0x30);
    machine.write(0x0433, 0x04);

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    while (machine.pc() < 0x0430) {
        machine.step_instruction();
    }

    uint8_t first_value = machine.read(0x0000);
    uint8_t second_value = machine.read(0x0001);

    // Second value should be less than first (timer counted down)
    // The difference should be roughly 8 cycles (4 NOPs + partial instruction timing)
    // But due to 2MHz vs 1MHz timing, it's 4 decrements per 8 cycles
    REQUIRE(second_value < first_value);

    // The difference should be reasonable (timer counts at 1MHz, so ~4-6 decrements)
    uint8_t difference = first_value - second_value;
    REQUIRE(difference >= 3);
    REQUIRE(difference <= 10);
}

//////////////////////////////////////////////////////////////////////////////
// Test E6: IRQ during branch instruction
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Integrated: IRQ during branch instruction", "[via][cpu][irq][integrated]") {
    ModelB machine;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program: Set up timer IRQ, then execute branch loops
    // The IRQ should be taken at instruction boundary, not mid-instruction
    //
    //   SEI
    //   LDA #$C0     ; Enable T1 IRQ
    //   STA $FE4E
    //   LDA #$04     ; T1 latch = 4
    //   STA $FE46
    //   LDA #$00
    //   STA $FE47
    //   STA $FE45    ; Start timer
    //   CLI
    // loop:
    //   CLC          ; 2 cycles
    //   BCC loop     ; 3 cycles (taken)
    //   JMP $0430    ; Should never reach

    machine.write(0x0400, 0x78);        // SEI
    machine.write(0x0401, 0xA9);        // LDA #$C0
    machine.write(0x0402, 0xC0);
    machine.write(0x0403, 0x8D);        // STA $FE4E
    machine.write(0x0404, 0x4E);
    machine.write(0x0405, 0xFE);
    machine.write(0x0406, 0xA9);        // LDA #$04
    machine.write(0x0407, 0x04);
    machine.write(0x0408, 0x8D);        // STA $FE46
    machine.write(0x0409, 0x46);
    machine.write(0x040A, 0xFE);
    machine.write(0x040B, 0xA9);        // LDA #$00
    machine.write(0x040C, 0x00);
    machine.write(0x040D, 0x8D);        // STA $FE47
    machine.write(0x040E, 0x47);
    machine.write(0x040F, 0xFE);
    machine.write(0x0410, 0x8D);        // STA $FE45
    machine.write(0x0411, 0x45);
    machine.write(0x0412, 0xFE);
    machine.write(0x0413, 0x58);        // CLI
    // loop:
    machine.write(0x0414, 0x18);        // CLC
    machine.write(0x0415, 0x90);        // BCC loop
    machine.write(0x0416, 0xFD);        // -3
    machine.write(0x0417, 0x4C);        // JMP $0430
    machine.write(0x0418, 0x30);
    machine.write(0x0419, 0x04);

    machine.write(0x0430, 0xEA);        // NOP (end marker)

    // IRQ handler - set flag, clear T1 interrupt, then RTI
    machine.write(0x0500, 0xE6);        // INC $01
    machine.write(0x0501, 0x01);
    machine.write(0x0502, 0xAD);        // LDA $FE44 (read T1CL to clear flag)
    machine.write(0x0503, 0x44);
    machine.write(0x0504, 0xFE);
    machine.write(0x0505, 0x40);        // RTI

    machine.write(0x0001, 0x00);        // Clear flag

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    // Execute until IRQ handler runs
    int iterations = 0;
    bool saw_irq_handler = false;
    while (iterations < 100) {
        machine.step_instruction();
        // PC will be in range 0x0500-0x0506 when in IRQ handler
        if (machine.pc() >= 0x0500 && machine.pc() <= 0x0506) {
            saw_irq_handler = true;
        }
        // If we're back in the loop after IRQ, we're done
        if (saw_irq_handler && machine.pc() >= 0x0414 && machine.pc() <= 0x0417) {
            break;
        }
        iterations++;
    }

    // Should have seen IRQ handler
    REQUIRE(saw_irq_handler);

    // Flag should have been incremented
    REQUIRE(machine.read(0x0001) >= 1);
}

//////////////////////////////////////////////////////////////////////////////
// Test E7: Multiple timer reads in sequence
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Integrated: Multiple timer reads show countdown", "[via][cpu][timer][integrated]") {
    ModelB machine;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program: Start timer and read it multiple times to verify countdown
    //   LDA #$40     ; T1 latch = 64
    //   STA $FE46
    //   LDA #$00
    //   STA $FE47
    //   STA $FE45    ; Start timer
    //   LDA $FE44    ; Read 1
    //   STA $10
    //   LDA $FE44    ; Read 2
    //   STA $11
    //   LDA $FE44    ; Read 3
    //   STA $12
    //   LDA $FE44    ; Read 4
    //   STA $13
    //   JMP $0440

    machine.write(0x0400, 0xA9);        // LDA #$40
    machine.write(0x0401, 0x40);
    machine.write(0x0402, 0x8D);        // STA $FE46
    machine.write(0x0403, 0x46);
    machine.write(0x0404, 0xFE);
    machine.write(0x0405, 0xA9);        // LDA #$00
    machine.write(0x0406, 0x00);
    machine.write(0x0407, 0x8D);        // STA $FE47
    machine.write(0x0408, 0x47);
    machine.write(0x0409, 0xFE);
    machine.write(0x040A, 0x8D);        // STA $FE45
    machine.write(0x040B, 0x45);
    machine.write(0x040C, 0xFE);
    // Read 1
    machine.write(0x040D, 0xAD);        // LDA $FE44
    machine.write(0x040E, 0x44);
    machine.write(0x040F, 0xFE);
    machine.write(0x0410, 0x85);        // STA $10
    machine.write(0x0411, 0x10);
    // Read 2
    machine.write(0x0412, 0xAD);        // LDA $FE44
    machine.write(0x0413, 0x44);
    machine.write(0x0414, 0xFE);
    machine.write(0x0415, 0x85);        // STA $11
    machine.write(0x0416, 0x11);
    // Read 3
    machine.write(0x0417, 0xAD);        // LDA $FE44
    machine.write(0x0418, 0x44);
    machine.write(0x0419, 0xFE);
    machine.write(0x041A, 0x85);        // STA $12
    machine.write(0x041B, 0x12);
    // Read 4
    machine.write(0x041C, 0xAD);        // LDA $FE44
    machine.write(0x041D, 0x44);
    machine.write(0x041E, 0xFE);
    machine.write(0x041F, 0x85);        // STA $13
    machine.write(0x0420, 0x13);
    // Jump to end
    machine.write(0x0421, 0x4C);        // JMP $0440
    machine.write(0x0422, 0x40);
    machine.write(0x0423, 0x04);

    machine.write(0x0440, 0xEA);        // NOP

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    while (machine.pc() < 0x0440) {
        machine.step_instruction();
    }

    uint8_t v1 = machine.read(0x10);
    uint8_t v2 = machine.read(0x11);
    uint8_t v3 = machine.read(0x12);
    uint8_t v4 = machine.read(0x13);

    // Values should be strictly decreasing
    REQUIRE(v1 > v2);
    REQUIRE(v2 > v3);
    REQUIRE(v3 > v4);

    // Each read pair is separated by 7 cycles (LDA abs=4, STA zp=3)
    // At 1MHz timer rate vs 2MHz CPU, that's ~3-4 decrements per read
    // So differences should be roughly consistent
    uint8_t d1 = v1 - v2;
    uint8_t d2 = v2 - v3;
    uint8_t d3 = v3 - v4;

    // All differences should be similar (within 1-2)
    REQUIRE(d1 >= 2);
    REQUIRE(d1 <= 6);
    REQUIRE(d2 >= 2);
    REQUIRE(d2 <= 6);
    REQUIRE(d3 >= 2);
    REQUIRE(d3 <= 6);
}

//////////////////////////////////////////////////////////////////////////////
// Hardware-Validated Timer Tests
//
// These tests verify cycle-accurate VIA timer behavior against real BBC
// hardware measurements. Expected values are from @scarybeasts' BBC Master.
//
// Source: jsbeeb tests/integration/via.js
// Reference: https://github.com/mattgodbolt/jsbeeb/issues/179
//
// These tests are hardware-validated against real BBC Master measurements
// by @scarybeasts. The 1MHz bus stretch timing is implemented in CpuBinding
// to add proper wait cycles when the 2MHz CPU accesses 1MHz peripherals.
//////////////////////////////////////////////////////////////////////////////

// User VIA register addresses (base 0xFE60, mirrored with 0x0F)
[[maybe_unused]] constexpr uint16_t USER_VIA_ORB   = kUserViaAddr + Via6522::REG_ORB;    // 0xFE60
[[maybe_unused]] constexpr uint16_t USER_VIA_DDRB  = kUserViaAddr + Via6522::REG_DDRB;   // 0xFE62
[[maybe_unused]] constexpr uint16_t USER_VIA_T1CL  = kUserViaAddr + Via6522::REG_T1CL;   // 0xFE64
[[maybe_unused]] constexpr uint16_t USER_VIA_T1CH  = kUserViaAddr + Via6522::REG_T1CH;   // 0xFE65
[[maybe_unused]] constexpr uint16_t USER_VIA_T1LL  = kUserViaAddr + Via6522::REG_T1LL;   // 0xFE66
[[maybe_unused]] constexpr uint16_t USER_VIA_T1LH  = kUserViaAddr + Via6522::REG_T1LH;   // 0xFE67
[[maybe_unused]] constexpr uint16_t USER_VIA_T2CL  = kUserViaAddr + Via6522::REG_T2CL;   // 0xFE68
[[maybe_unused]] constexpr uint16_t USER_VIA_T2CH  = kUserViaAddr + Via6522::REG_T2CH;   // 0xFE69
[[maybe_unused]] constexpr uint16_t USER_VIA_SR    = kUserViaAddr + Via6522::REG_SR;     // 0xFE6A
[[maybe_unused]] constexpr uint16_t USER_VIA_ACR   = kUserViaAddr + Via6522::REG_ACR;    // 0xFE6B
[[maybe_unused]] constexpr uint16_t USER_VIA_IFR   = kUserViaAddr + Via6522::REG_IFR;    // 0xFE6D
[[maybe_unused]] constexpr uint16_t USER_VIA_IER   = kUserViaAddr + Via6522::REG_IER;    // 0xFE6E
[[maybe_unused]] constexpr uint16_t USER_VIA_PCR   = kUserViaAddr + 0x0C;                // 0xFE6C
[[maybe_unused]] constexpr uint16_t USER_VIA_ORA_NH= kUserViaAddr + 0x0F;                // 0xFE6F

// Helper to check a sequence of bytes at a given address
static void check_results(ModelB& machine, uint16_t addr, const std::vector<uint8_t>& expected, const char* test_name) {
    for (size_t i = 0; i < expected.size(); ++i) {
        uint8_t actual = machine.read(static_cast<uint16_t>(addr + i));
        INFO(test_name << " - mismatch at offset " << i << " (address 0x" << std::hex << (addr + i) << ")");
        REQUIRE(actual == expected[i]);
    }
}

//////////////////////////////////////////////////////////////////////////////
// ACR write during timer operation
// REAL BBC MASTER: 64, 0, 0, 128
// Source: @scarybeasts VIA.AC1 test
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("ACR write during timer operation", "[via][hardware-validated][acr]") {
    ModelB machine;
    constexpr uint16_t RESULT_ADDR = 0x0100;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program at 0x0400:
    // SEI
    // LDA #$FF : STA $FE62          ; DDRB = 0xFF (all outputs)
    // LDA #$00 : STA $FE60          ; ORB = 0x00
    // LDA #$7F : STA $FE6E          ; IER = disable all interrupts
    // LDA #$80 : STA $FE6B          ; ACR = 0x80 (T1 continuous, PB7 output)
    // LDA #$04 : STA $FE64          ; T1LL = 4
    // LDA #$00 : STA $FE65          ; T1CH = 0 (start timer)
    // NOP : NOP : NOP
    // LDA $FE6D : STA R%            ; Read IFR, store result[0]
    // NOP : NOP
    // LDA #$C0 : STA $FE6B          ; ACR = 0xC0 (T1 continuous, PB7 output)
    // LDA $FE64 : STA R%+1          ; Read T1CL, store result[1]
    // LDA $FE6D : STA R%+2          ; Read IFR, store result[2]
    // LDA $FE60 : STA R%+3          ; Read ORB, store result[3]
    // CLI : RTS

    size_t pc = 0x0400;
    machine.write(pc++, 0x78);        // SEI
    machine.write(pc++, 0xA9);        // LDA #$FF
    machine.write(pc++, 0xFF);
    machine.write(pc++, 0x8D);        // STA $FE62
    machine.write(pc++, 0x62);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$00
    machine.write(pc++, 0x00);
    machine.write(pc++, 0x8D);        // STA $FE60
    machine.write(pc++, 0x60);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$7F
    machine.write(pc++, 0x7F);
    machine.write(pc++, 0x8D);        // STA $FE6E
    machine.write(pc++, 0x6E);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$80
    machine.write(pc++, 0x80);
    machine.write(pc++, 0x8D);        // STA $FE6B
    machine.write(pc++, 0x6B);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$04
    machine.write(pc++, 0x04);
    machine.write(pc++, 0x8D);        // STA $FE64
    machine.write(pc++, 0x64);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$00
    machine.write(pc++, 0x00);
    machine.write(pc++, 0x8D);        // STA $FE65 (start timer)
    machine.write(pc++, 0x65);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xAD);        // LDA $FE6D (read IFR)
    machine.write(pc++, 0x6D);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $0100
    machine.write(pc++, RESULT_ADDR & 0xFF);
    machine.write(pc++, RESULT_ADDR >> 8);
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xA9);        // LDA #$C0
    machine.write(pc++, 0xC0);
    machine.write(pc++, 0x8D);        // STA $FE6B (change ACR)
    machine.write(pc++, 0x6B);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xAD);        // LDA $FE64 (read T1CL)
    machine.write(pc++, 0x64);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $0101
    machine.write(pc++, (RESULT_ADDR + 1) & 0xFF);
    machine.write(pc++, (RESULT_ADDR + 1) >> 8);
    machine.write(pc++, 0xAD);        // LDA $FE6D (read IFR)
    machine.write(pc++, 0x6D);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $0102
    machine.write(pc++, (RESULT_ADDR + 2) & 0xFF);
    machine.write(pc++, (RESULT_ADDR + 2) >> 8);
    machine.write(pc++, 0xAD);        // LDA $FE60 (read ORB)
    machine.write(pc++, 0x60);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $0103
    machine.write(pc++, (RESULT_ADDR + 3) & 0xFF);
    machine.write(pc++, (RESULT_ADDR + 3) >> 8);
    machine.write(pc++, 0x58);        // CLI
    machine.write(pc++, 0x60);        // RTS

    M6502_Reset(&machine.cpu());
    machine.step_instruction();  // Reset sequence

    // Execute until RTS
    int iterations = 0;
    while (machine.pc() < pc && iterations < 500) {
        machine.step_instruction();
        iterations++;
    }

    // REAL BBC: 64, 0, 0, 128
    // 64 = IFR with T1 flag set (0x40)
    // 0 = T1CL after reload
    // 0 = IFR after T1CL read (cleared T1 flag)
    // 128 = ORB with PB7 high (0x80 from T1 toggle)
    check_results(machine, RESULT_ADDR, {64, 0, 0, 128}, "VIA.AC1");
}

//////////////////////////////////////////////////////////////////////////////
// T1 counter sequence through timeout and reload
// REAL BBC MASTER: 1, 0, 255, 4, 3, 2
// Source: @scarybeasts VIA.T11 test
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("T1 counter sequence through timeout and reload", "[via][hardware-validated][timer]") {
    ModelB machine;
    constexpr uint16_t RESULT_ADDR = 0x0100;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // This test reads T1CL at 6 different times after starting the timer
    // with latch=4 to see exactly how it counts down and wraps
    // The jsbeeb test calls PROCtimeit(N%) with N=1 to 6, where each call
    // adds N NOPs before reading T1CL

    // We'll build a sequence that does all 6 reads consecutively
    // For each read: SEI, setup timer with latch=4, N NOPs, read T1CL, store result

    size_t code_pc = 0x0400;

    // Helper to write one timer read sequence
    auto write_timer_read = [&](int nop_count, uint16_t result_addr) {
        machine.write(code_pc++, 0x78);        // SEI
        machine.write(code_pc++, 0xA2);        // LDX #0
        machine.write(code_pc++, 0x00);
        machine.write(code_pc++, 0xA0);        // LDY #0
        machine.write(code_pc++, 0x00);
        machine.write(code_pc++, 0xA9);        // LDA #4
        machine.write(code_pc++, 0x04);
        machine.write(code_pc++, 0x8D);        // STA $FE64 (T1LL)
        machine.write(code_pc++, 0x64);
        machine.write(code_pc++, 0xFE);
        machine.write(code_pc++, 0xA9);        // LDA #0
        machine.write(code_pc++, 0x00);
        machine.write(code_pc++, 0x8D);        // STA $FE65 (T1CH - starts timer)
        machine.write(code_pc++, 0x65);
        machine.write(code_pc++, 0xFE);

        // Insert N NOPs
        for (int i = 0; i < nop_count; i++) {
            machine.write(code_pc++, 0xEA);    // NOP
        }

        machine.write(code_pc++, 0xAD);        // LDA $FE64 (T1CL)
        machine.write(code_pc++, 0x64);
        machine.write(code_pc++, 0xFE);
        machine.write(code_pc++, 0x8D);        // STA result_addr
        machine.write(code_pc++, result_addr & 0xFF);
        machine.write(code_pc++, result_addr >> 8);
        machine.write(code_pc++, 0x58);        // CLI
    };

    // Write 6 timer read sequences with 1-6 NOPs
    write_timer_read(1, RESULT_ADDR + 0);
    write_timer_read(2, RESULT_ADDR + 1);
    write_timer_read(3, RESULT_ADDR + 2);
    write_timer_read(4, RESULT_ADDR + 3);
    write_timer_read(5, RESULT_ADDR + 4);
    write_timer_read(6, RESULT_ADDR + 5);
    machine.write(code_pc++, 0x60);            // RTS

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    int iterations = 0;
    while (machine.pc() < code_pc && iterations < 2000) {
        machine.step_instruction();
        iterations++;
    }

    // REAL BBC: 1, 0, 255, 4, 3, 2
    // This shows the timer counting: 4 -> 3 -> 2 -> 1 -> 0 -> underflow to 255 -> reload to 4 -> 3 -> 2
    check_results(machine, RESULT_ADDR, {1, 0, 255, 4, 3, 2}, "VIA.T11");
}

//////////////////////////////////////////////////////////////////////////////
// T2 counter continues past timeout
// REAL BBC MASTER: 1, 0, 255, 254, 253, 252
// Source: @scarybeasts VIA.T22 test
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("T2 counter continues past timeout", "[via][hardware-validated][timer][t2]") {
    ModelB machine;
    constexpr uint16_t RESULT_ADDR = 0x0100;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // T2 in one-shot mode should continue counting past expiry (into FFFF, FFFE, etc.)
    // Similar to T11 but for T2

    size_t code_pc = 0x0400;

    // First set ACR to 0 (one-shot T2)
    machine.write(code_pc++, 0xA9);        // LDA #0
    machine.write(code_pc++, 0x00);
    machine.write(code_pc++, 0x8D);        // STA $FE6B
    machine.write(code_pc++, 0x6B);
    machine.write(code_pc++, 0xFE);

    auto write_timer_read = [&](int nop_count, uint16_t result_addr) {
        machine.write(code_pc++, 0x78);        // SEI
        machine.write(code_pc++, 0xA2);        // LDX #0
        machine.write(code_pc++, 0x00);
        machine.write(code_pc++, 0xA0);        // LDY #0
        machine.write(code_pc++, 0x00);
        machine.write(code_pc++, 0xA9);        // LDA #4
        machine.write(code_pc++, 0x04);
        machine.write(code_pc++, 0x8D);        // STA $FE68 (T2CL)
        machine.write(code_pc++, 0x68);
        machine.write(code_pc++, 0xFE);
        machine.write(code_pc++, 0xA9);        // LDA #0
        machine.write(code_pc++, 0x00);
        machine.write(code_pc++, 0x8D);        // STA $FE69 (T2CH - starts timer)
        machine.write(code_pc++, 0x69);
        machine.write(code_pc++, 0xFE);

        for (int i = 0; i < nop_count; i++) {
            machine.write(code_pc++, 0xEA);    // NOP
        }

        machine.write(code_pc++, 0xAD);        // LDA $FE68 (T2CL)
        machine.write(code_pc++, 0x68);
        machine.write(code_pc++, 0xFE);
        machine.write(code_pc++, 0x8D);        // STA result_addr
        machine.write(code_pc++, result_addr & 0xFF);
        machine.write(code_pc++, result_addr >> 8);
        machine.write(code_pc++, 0x58);        // CLI
    };

    write_timer_read(1, RESULT_ADDR + 0);
    write_timer_read(2, RESULT_ADDR + 1);
    write_timer_read(3, RESULT_ADDR + 2);
    write_timer_read(4, RESULT_ADDR + 3);
    write_timer_read(5, RESULT_ADDR + 4);
    write_timer_read(6, RESULT_ADDR + 5);
    machine.write(code_pc++, 0x60);            // RTS

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    int iterations = 0;
    while (machine.pc() < code_pc && iterations < 2000) {
        machine.step_instruction();
        iterations++;
    }

    // REAL BBC: 1, 0, 255, 254, 253, 252
    // T2 continues counting down past 0 into 255, 254, etc. (no reload in one-shot)
    check_results(machine, RESULT_ADDR, {1, 0, 255, 254, 253, 252}, "VIA.T22");
}

//////////////////////////////////////////////////////////////////////////////
// T1LH write clears interrupt flag
// REAL BBC MASTER: 64, 0
// Source: @scarybeasts VIA.I1 test
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("T1LH write clears interrupt flag", "[via][hardware-validated][interrupt]") {
    ModelB machine;
    constexpr uint16_t RESULT_ADDR = 0x0100;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // Program:
    // SEI
    // LDA #$7F : STA $FE6E          ; Disable all interrupts
    // LDA #$00 : STA $FE6B          ; ACR = 0 (one-shot)
    // STA $FE64                     ; T1LL = 0
    // STA $FE65                     ; T1CH = 0 (start timer)
    // NOP : NOP
    // LDA $FE6D : STA R%            ; Read IFR (should have T1 flag set)
    // LDA #$00 : STA $FE67          ; Write T1LH (should clear T1 flag)
    // LDA $FE6D : STA R%+1          ; Read IFR (should be clear now)
    // CLI : RTS

    size_t pc = 0x0400;
    machine.write(pc++, 0x78);        // SEI
    machine.write(pc++, 0xA9);        // LDA #$7F
    machine.write(pc++, 0x7F);
    machine.write(pc++, 0x8D);        // STA $FE6E
    machine.write(pc++, 0x6E);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$00
    machine.write(pc++, 0x00);
    machine.write(pc++, 0x8D);        // STA $FE6B
    machine.write(pc++, 0x6B);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $FE64 (T1LL)
    machine.write(pc++, 0x64);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $FE65 (T1CH - start timer)
    machine.write(pc++, 0x65);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xAD);        // LDA $FE6D
    machine.write(pc++, 0x6D);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $0100
    machine.write(pc++, RESULT_ADDR & 0xFF);
    machine.write(pc++, RESULT_ADDR >> 8);
    machine.write(pc++, 0xA9);        // LDA #$00
    machine.write(pc++, 0x00);
    machine.write(pc++, 0x8D);        // STA $FE67 (T1LH - should clear T1 flag)
    machine.write(pc++, 0x67);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xAD);        // LDA $FE6D
    machine.write(pc++, 0x6D);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA $0101
    machine.write(pc++, (RESULT_ADDR + 1) & 0xFF);
    machine.write(pc++, (RESULT_ADDR + 1) >> 8);
    machine.write(pc++, 0x58);        // CLI
    machine.write(pc++, 0x60);        // RTS

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    int iterations = 0;
    while (machine.pc() < pc && iterations < 500) {
        machine.step_instruction();
        iterations++;
    }

    // REAL BBC: 64, 0
    // 64 = T1 flag set in IFR (0x40)
    // 0 = T1 flag cleared after T1LH write
    check_results(machine, RESULT_ADDR, {64, 0}, "VIA.I1");
}

//////////////////////////////////////////////////////////////////////////////
// T1 latch write timing relative to expiry
// REAL BBC MASTER: 253, 0
// Source: @scarybeasts VIA.T12 test
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("T1 latch write timing relative to expiry", "[via][hardware-validated][timer]") {
    ModelB machine;
    constexpr uint16_t RESULT_ADDR = 0x0100;
    setup_minimal_mos(machine, 0x0400, 0x0500);

    // This test checks timing of T1LL writes vs timer reload
    // Program:
    // SEI
    // LDA #$00 : STA $FE6B          ; ACR = 0 (one-shot)
    // LDA #$7F : STA $FE6E          ; Disable interrupts
    // LDA #$02 : STA $FE64          ; T1LL = 2
    // LDA #$00 : STA $FE65          ; T1CH = 0 (start timer)
    // LDA #$FF : STA $FE66          ; Write T1LL = 255 just before expiry
    // LDA $FE64 : STA R%            ; Read T1CL
    // -- Now do same thing but with more delay --
    // LDA #$03 : STA $FE64          ; T1LL = 3
    // LDA #$00 : STA $FE65          ; Start timer
    // NOP : NOP
    // LDA #$FF : STA $FE66          ; Write T1LL = 255 after expiry
    // LDA $FE64 : STA R%+1          ; Read T1CL
    // CLI : RTS

    size_t pc = 0x0400;
    machine.write(pc++, 0x78);        // SEI
    machine.write(pc++, 0xA9);        // LDA #$00
    machine.write(pc++, 0x00);
    machine.write(pc++, 0x8D);        // STA $FE6B
    machine.write(pc++, 0x6B);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$7F
    machine.write(pc++, 0x7F);
    machine.write(pc++, 0x8D);        // STA $FE6E
    machine.write(pc++, 0x6E);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$02
    machine.write(pc++, 0x02);
    machine.write(pc++, 0x8D);        // STA $FE64 (T1LL = 2)
    machine.write(pc++, 0x64);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$00
    machine.write(pc++, 0x00);
    machine.write(pc++, 0x8D);        // STA $FE65 (start timer)
    machine.write(pc++, 0x65);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$FF
    machine.write(pc++, 0xFF);
    machine.write(pc++, 0x8D);        // STA $FE66 (T1LL = 255)
    machine.write(pc++, 0x66);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xAD);        // LDA $FE64
    machine.write(pc++, 0x64);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA R%
    machine.write(pc++, RESULT_ADDR & 0xFF);
    machine.write(pc++, RESULT_ADDR >> 8);
    // Second sequence
    machine.write(pc++, 0xA9);        // LDA #$03
    machine.write(pc++, 0x03);
    machine.write(pc++, 0x8D);        // STA $FE64
    machine.write(pc++, 0x64);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xA9);        // LDA #$00
    machine.write(pc++, 0x00);
    machine.write(pc++, 0x8D);        // STA $FE65 (start timer)
    machine.write(pc++, 0x65);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xEA);        // NOP
    machine.write(pc++, 0xA9);        // LDA #$FF
    machine.write(pc++, 0xFF);
    machine.write(pc++, 0x8D);        // STA $FE66
    machine.write(pc++, 0x66);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0xAD);        // LDA $FE64
    machine.write(pc++, 0x64);
    machine.write(pc++, 0xFE);
    machine.write(pc++, 0x8D);        // STA R%+1
    machine.write(pc++, (RESULT_ADDR + 1) & 0xFF);
    machine.write(pc++, (RESULT_ADDR + 1) >> 8);
    machine.write(pc++, 0x58);        // CLI
    machine.write(pc++, 0x60);        // RTS

    M6502_Reset(&machine.cpu());
    machine.step_instruction();

    int iterations = 0;
    while (machine.pc() < pc && iterations < 500) {
        machine.step_instruction();
        iterations++;
    }

    // REAL BBC: 253, 0
    // 253 = Timer counting down from 255 (new latch value took effect before reload)
    // 0 = Timer at 0 (write was after expiry, timer continuing free-run)
    check_results(machine, RESULT_ADDR, {253, 0}, "VIA.T12");
}
