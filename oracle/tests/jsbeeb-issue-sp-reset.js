/**
 * Test for SP initialization after reset.
 *
 * To use: Copy this file to jsbeeb/tests/unit/test-sp-reset.js
 * Run: npm test -- tests/unit/test-sp-reset.js
 *
 * Bug: After reset, SP should be 0xFD but is 0x00.
 *
 * The real 6502 reset sequence performs 3 dummy stack reads that
 * decrement SP, even though no data is written (R/W held in read mode).
 *
 * References:
 * - https://www.pagetable.com/?p=410
 * - http://forum.6502.org/viewtopic.php?p=2959
 */

import { describe, it, expect } from "vitest";
import { fake6502, fake65C12 } from "../../src/fake6502.js";

describe("6502 reset SP behavior", () => {
    it("should decrement SP by 3 during reset (NMOS)", async () => {
        const cpu = fake6502();
        await cpu.initialise();

        // After reset, SP should be 0xFD
        // Real 6502 does 3 dummy pushes: 0x00 → 0xFF → 0xFE → 0xFD
        expect(cpu.s).toBe(0xfd);
    });

    it("should decrement SP by 3 during reset (CMOS)", async () => {
        const cpu = fake65C12();
        await cpu.initialise();

        // Same behavior expected for 65C12
        expect(cpu.s).toBe(0xfd);
    });

    it("should have SP=0xFD after explicit reset call", async () => {
        const cpu = fake6502();
        await cpu.initialise();

        // Set SP to a known value
        cpu.s = 0x80;

        // Reset should decrement by 3: 0x80 → 0x7F → 0x7E → 0x7D
        cpu.reset(true);

        expect(cpu.s).toBe(0x7d);
    });
});

/**
 * EXPLANATION:
 *
 * The 6502 reset sequence reuses the interrupt/BRK logic internally.
 * During BRK/IRQ/NMI, the CPU pushes PCH, PCL, and P to the stack.
 *
 * During RESET, the same cycles occur but the R/W pin is held in
 * "read" mode, so no actual writes happen. However, SP still
 * decrements 3 times as if pushes occurred.
 *
 * This was a clever hardware optimization - it avoided corrupting
 * memory at power-on when SP contains random garbage.
 *
 * From pagetable.com:
 * "The internal state of the CPU now shows that SP is 0xFD,
 *  because it got decremented 3 times for the three fake push
 *  operations."
 *
 * SUGGESTED FIX in src/6502.js reset() method:
 *
 *   reset(hard) {
 *       // ... existing code ...
 *       this.pc = this.readmem(0xfffc) | (this.readmem(0xfffd) << 8);
 *       this.p.i = true;
 *
 *       // Simulate the 3 dummy stack pushes that decrement SP
 *       this.s = (this.s - 3) & 0xff;
 *   }
 */
