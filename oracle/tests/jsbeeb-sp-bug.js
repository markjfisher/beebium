/**
 * Minimal reproduction of jsbeeb SP initialization bug.
 *
 * Issue: After reset, SP should be 0xFD (decremented 3 times from 0x00),
 * but jsbeeb leaves it at 0x00.
 *
 * Run with: node --experimental-vm-modules jsbeeb-sp-bug.js
 * (from the oracle/tests directory, with jsbeeb as sibling to beebium)
 */

import { TestMachine } from '../../../jsbeeb/tests/test-machine.js';

async function main() {
    console.log('=== jsbeeb SP Initialization Bug Reproduction ===\n');

    // Initialize jsbeeb with Model B + DFS
    const machine = new TestMachine('B-DFS1.2');
    await machine.initialise();

    // Get the 6502 processor
    const cpu = machine.processor;

    // Check SP after initialization (before any reset)
    console.log(`SP after initialise(): 0x${cpu.s.toString(16).toUpperCase().padStart(2, '0')}`);

    // Perform a hard reset
    cpu.reset(true);

    // Check SP after reset
    const spAfterReset = cpu.s;
    console.log(`SP after reset():      0x${spAfterReset.toString(16).toUpperCase().padStart(2, '0')}`);

    // What it should be according to 6502 hardware
    const expectedSP = 0xFD;
    console.log(`Expected SP:           0x${expectedSP.toString(16).toUpperCase().padStart(2, '0')}`);

    console.log('\n--- Analysis ---');
    if (spAfterReset === 0x00) {
        console.log('BUG CONFIRMED: SP is 0x00 after reset.');
        console.log('\nThe real 6502 reset sequence performs 3 "fake" stack pushes');
        console.log('(reads instead of writes) that decrement SP:');
        console.log('  0x00 → 0xFF → 0xFE → 0xFD');
        console.log('\njsbeeb\'s reset() sets PC directly without simulating these cycles.');
    } else if (spAfterReset === expectedSP) {
        console.log('SP is correct! Bug may have been fixed.');
    } else {
        console.log(`Unexpected SP value: 0x${spAfterReset.toString(16).toUpperCase()}`);
    }

    // Demonstrate the practical impact
    console.log('\n--- Practical Impact ---');
    console.log('With SP=0x00, first JSR will push return address to:');
    console.log('  PCH → $0100, then SP wraps to 0xFF');
    console.log('  PCL → $01FF, then SP becomes 0xFE');
    console.log('\nWith correct SP=0xFD, first JSR pushes to:');
    console.log('  PCH → $01FD, then SP becomes 0xFC');
    console.log('  PCL → $01FC, then SP becomes 0xFB');
    console.log('\nThis causes stack data to be at different memory addresses,');
    console.log('which can cause issues in differential testing and edge cases');
    console.log('where code relies on stack position.');

    // Reference
    console.log('\n--- References ---');
    console.log('• https://www.pagetable.com/?p=410');
    console.log('  "Internals of BRK/IRQ/NMI/RESET on a MOS 6502"');
    console.log('• http://forum.6502.org/viewtopic.php?p=2959');
    console.log('  "Stack Pointer and Register Status After RESET"');
}

main().catch(console.error);
