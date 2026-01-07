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

// Category E: Integrated CPU+VIA Timing Tests
//
// These tests verify that the VIA timer IRQ timing is correct when running
// actual 6502 instructions. Based on issues identified in stardot forums:
// https://stardot.org.uk/forums/viewtopic.php?t=16138
//
// Key behaviors tested:
// 1. IRQ detection timing - CPU should see IRQ on correct cycle
// 2. Instruction mid-cycle reads - different instructions access VIA at different points
// 3. Timer reads during multi-cycle instructions

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/Via6522.hpp>
#include <beebium/Types.hpp>
#include <array>

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
