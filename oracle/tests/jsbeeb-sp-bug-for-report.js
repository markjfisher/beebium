/**
 * Minimal reproduction for jsbeeb issue: SP incorrect after reset
 *
 * To run in jsbeeb repo:
 *   1. Save this file as tests/sp-reset-bug.js
 *   2. Run: npm test -- --grep "SP after reset"
 *
 * Or add to an existing test file.
 */

// For jsbeeb's test framework (Mocha-style):
describe("6502 reset sequence", function () {
    it("should set SP to 0xFD after reset", async function () {
        // TestMachine initializes and resets the CPU
        const testMachine = new TestMachine("B");
        await testMachine.initialise();

        const sp = testMachine.processor.s;

        // Real 6502 reset does 3 dummy stack reads, decrementing SP each time:
        // 0x00 → 0xFF → 0xFE → 0xFD
        // See: https://www.pagetable.com/?p=410

        expect(sp).to.equal(0xFD);  // Currently fails: sp === 0x00
    });
});

/**
 * EXPECTED BEHAVIOR:
 *
 * The 6502 reset sequence reuses interrupt logic but with R/W set to "read":
 *
 * Cycle 1-3: Dummy reads from stack area, SP decrements each time
 * Cycle 4-5: Read reset vector from $FFFC/$FFFD
 * Cycle 6-7: Jump to reset vector
 *
 * After reset: SP = initial_value - 3
 *
 * If SP starts at 0x00 (common power-on state):
 *   0x00 → 0xFF → 0xFE → 0xFD
 *
 * CURRENT BEHAVIOR:
 *
 * jsbeeb's reset() in src/6502.js just sets PC directly:
 *   this.pc = this.readmem(0xfffc) | (this.readmem(0xfffd) << 8);
 *
 * SP is never decremented, staying at 0x00.
 *
 * IMPACT:
 *
 * Stack operations (JSR, PHA, interrupts) write to wrong addresses:
 * - With SP=0x00: First push goes to $0100, SP wraps to 0xFF
 * - With SP=0xFD: First push goes to $01FD, SP becomes 0xFC
 *
 * This causes the stack page ($0100-$01FF) to have data at
 * different offsets than real hardware.
 *
 * SUGGESTED FIX:
 *
 * In src/6502.js reset() method, after setting PC, add:
 *   this.s = (this.s - 3) & 0xff;  // Simulate 3 dummy pushes
 *
 * Or for full accuracy, simulate the 7-cycle reset sequence.
 */
