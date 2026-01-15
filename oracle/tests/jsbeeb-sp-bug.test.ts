/**
 * Minimal reproduction of jsbeeb SP initialization bug.
 *
 * Issue: After reset, SP should be 0xFD (decremented 3 times during
 * the 7-cycle reset sequence), but jsbeeb leaves it at 0x00.
 *
 * References:
 * - https://www.pagetable.com/?p=410 (6502 reset internals)
 * - http://forum.6502.org/viewtopic.php?p=2959 (SP after reset)
 */

import { describe, it, expect, beforeAll } from 'vitest';
import { JsbeebOracle } from '../src/index.js';

describe('jsbeeb SP initialization bug', () => {
    let oracle: JsbeebOracle;

    beforeAll(async () => {
        oracle = new JsbeebOracle();
        await oracle.initialize('B-DFS1.2');
    });

    it('demonstrates incorrect SP after reset', () => {
        // Get CPU state after initialization (which calls reset internally)
        const cpu = oracle.getCpuState();

        console.log('\n=== jsbeeb SP Initialization Bug ===');
        console.log(`SP after reset: 0x${cpu.sp.toString(16).toUpperCase().padStart(2, '0')}`);
        console.log(`Expected SP:    0xFD`);

        // Document the bug - SP is 0x00 when it should be 0xFD
        // The real 6502 reset sequence does 3 "fake" pushes that decrement SP:
        //   0x00 → 0xFF → 0xFE → 0xFD
        //
        // jsbeeb's reset() just sets PC directly without simulating these.

        if (cpu.sp === 0x00) {
            console.log('\nBUG CONFIRMED: SP=0x00 instead of 0xFD');
            console.log('\nThe 6502 reset sequence performs 3 dummy stack reads');
            console.log('that decrement SP, even though no data is written.');
            console.log('jsbeeb skips this, leaving SP at its initial value.');
        }

        // This test documents the current (buggy) behavior
        // When jsbeeb is fixed, change this to expect 0xFD
        expect(cpu.sp).toBe(0x00); // BUG: should be 0xFD
    });

    it('shows stack operations go to wrong addresses', async () => {
        // Reset to known state
        oracle.reset();

        const cpuBefore = oracle.getCpuState();
        console.log(`\nSP before JSR: 0x${cpuBefore.sp.toString(16).toUpperCase().padStart(2, '0')}`);

        // The MOS will do JSR instructions during boot.
        // With SP=0x00, first push goes to $0100, SP wraps to 0xFF
        // With correct SP=0xFD, first push goes to $01FD, SP becomes 0xFC

        // Step a few instructions to see JSR happen
        await oracle.stepInstructions(10);

        const cpuAfter = oracle.getCpuState();
        console.log(`SP after 10 instructions: 0x${cpuAfter.sp.toString(16).toUpperCase().padStart(2, '0')}`);

        // With buggy SP=0x00, after wrapping we'd see values like 0xFF, 0xFE, etc.
        // With correct SP=0xFD, we'd see 0xFC, 0xFB, etc.

        // The actual values depend on what instructions execute,
        // but the key point is the stack data ends up at different
        // memory addresses than it would on real hardware.
    });
});
