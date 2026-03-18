/**
 * Dump jsbeeb's parasite instruction trace from $0819 (decompressor entry).
 * Writes a binary file that can be compared against Beebium's trace.
 *
 * Format: 8 bytes per entry (PC:u16le, A:u8, X:u8, Y:u8, SP:u8, P:u8, opcode:u8)
 */

import { describe, it } from 'vitest';
import { join } from 'node:path';
import { writeFileSync } from 'node:fs';
import { JsbeebOracle } from '../src/index.js';

const REPO_ROOT = join(import.meta.dirname, '..', '..');
const DISC_FILEPATH_REL = '../tests/assets/discs/chuckieEgg2023.ssd';
const OUTPUT_FILEPATH = join(REPO_ROOT, 'tests', 'assets', 'ce2023_jsbeeb_trace.bin');

const DECOMP_ENTRY = 0x0819;
const FIRST_R4_ACK = 0x0810;
const P_MASK = 0xCF;  // mask bits 4-5

describe('CE2023 jsbeeb trace dump', () => {
    it('dumps instruction trace from $0819', async () => {
        const oracle = new JsbeebOracle();
        await oracle.initialize('B1770', { tube: true });
        await oracle.loadDisc(0, DISC_FILEPATH_REL);
        oracle.reset();

        // Boot to BASIC prompt
        await oracle.runCycles(16_000_000);
        await oracle.type("*EXEC !BOOT\r");

        // Run until decompressor entry
        await oracle.runUntilParasiteAddress(FIRST_R4_ACK, 30);
        console.log('Reached $0810');

        // Run until $0819 (past the R4 ack JSR)
        await oracle.runUntilParasiteAddress(DECOMP_ENTRY, 10);
        console.log('Reached $0819');

        const parasiteCpu = (oracle as any).parasiteProcessor;
        console.log(`CPU state at $0819: A=$${parasiteCpu.a.toString(16)} X=$${parasiteCpu.x.toString(16)} Y=$${parasiteCpu.y.toString(16)} SP=$${parasiteCpu.s.toString(16)} P=$${(parasiteCpu.p.asByte() & P_MASK).toString(16)}`);

        // Collect trace: 8 bytes per entry
        const MAX_INSTRUCTIONS = 500_000;
        const buffer = Buffer.alloc(MAX_INSTRUCTIONS * 8);
        let count = 0;

        parasiteCpu._debugInstruction = (pc: number) => {
            if (count >= MAX_INSTRUCTIONS) return true;

            const offset = count * 8;
            buffer.writeUInt16LE(pc, offset);
            buffer.writeUInt8(parasiteCpu.a, offset + 2);
            buffer.writeUInt8(parasiteCpu.x, offset + 3);
            buffer.writeUInt8(parasiteCpu.y, offset + 4);
            buffer.writeUInt8(parasiteCpu.s, offset + 5);
            buffer.writeUInt8(parasiteCpu.p.asByte() & P_MASK, offset + 6);
            // Read opcode from memory
            buffer.writeUInt8(parasiteCpu.memory[pc], offset + 7);
            count++;

            return count >= MAX_INSTRUCTIONS;
        };

        // Run host to drive parasite through the decompressor
        (oracle as any).processor.execute(200_000_000);
        parasiteCpu._debugInstruction = null;

        console.log(`Collected ${count} instructions`);
        if (count > 0) {
            const first = { pc: buffer.readUInt16LE(0), a: buffer.readUInt8(2) };
            const last_off = (count - 1) * 8;
            const last = { pc: buffer.readUInt16LE(last_off), a: buffer.readUInt8(last_off + 2) };
            console.log(`  First: PC=$${first.pc.toString(16).toUpperCase()} A=$${first.a.toString(16).toUpperCase()}`);
            console.log(`  Last:  PC=$${last.pc.toString(16).toUpperCase()} A=$${last.a.toString(16).toUpperCase()}`);
        }

        // Check table value
        const val_0CE6 = parasiteCpu.memory[0x0CE6];
        console.log(`Value at $0CE6: $${val_0CE6.toString(16).toUpperCase()} (expected $38)`);

        // Output bytes
        const output = Array.from({ length: 20 }, (_, i) => parasiteCpu.memory[0xFC00 + i]);
        console.log(`Output ($FC00+): ${output.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join(' ')}`);

        // Write trace file (truncated to actual count)
        writeFileSync(OUTPUT_FILEPATH, buffer.subarray(0, count * 8));
        console.log(`Trace written to: ${OUTPUT_FILEPATH}`);

    }, 120_000);
});
