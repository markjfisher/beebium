/**
 * Convergence tests - verify jsbeeb and Beebium reach identical states at key sync points.
 *
 * These tests validate that both emulators execute the same code paths and arrive
 * at the same functional state, despite minor timing implementation differences.
 *
 * See CYCLE_DIFFERENCE_INVESTIGATION.md for details on expected timing differences.
 */

import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { JsbeebOracle, BeebiumClient, DiffRunner, ServerFixture } from '../src/index.js';

describe('State Convergence', () => {
    let fixture: ServerFixture;
    let oracle: JsbeebOracle;
    let client: BeebiumClient;
    let runner: DiffRunner;

    beforeAll(async () => {
        // Both configured with WD1770 disc controller + DFS + ADFS:
        // - jsbeeb B1770: WD1770 controller, DFS 2.26, ADFS 1.30, sideways RAM in slots 0-7
        // - Beebium B-RomRam: 16-slot ROM/RAM board with identical layout
        oracle = new JsbeebOracle();
        await oracle.initialize('B1770');

        const romDir = '/Users/rjs/Code/beebium/roms';
        fixture = new ServerFixture({
            timeout: 10000,
            model: 'B-RomRam',
            fdc: 'acorn-1770',
            sideways: [
                { slot: 0, type: 'ram' },
                { slot: 1, type: 'ram' },
                { slot: 2, type: 'ram' },
                { slot: 3, type: 'ram' },
                { slot: 4, type: 'ram' },
                { slot: 5, type: 'ram' },
                { slot: 6, type: 'ram' },
                { slot: 7, type: 'ram' },
                { slot: 13, type: 'rom', filepath: `${romDir}/acorn-adfs_1_30.rom` },
                { slot: 14, type: 'rom', filepath: `${romDir}/acorn-dfs_2_26.rom` },
            ],
        });
        client = await fixture.start();

        runner = new DiffRunner(oracle, client);
    }, 30000);

    afterAll(async () => {
        await fixture.stop();
    });

    it('should have identical CPU and memory state at BASIC entry ($8000)', async () => {
        // Reset both emulators
        oracle.reset();
        await client.reset();

        // Run both to BASIC entry point
        const result = await runner.runUntilAddress(0x8000);

        // Get states for reporting
        const jsbeebState = oracle.getState();
        const beebiumState = await client.getMachineState();

        // Filter divergences by component
        const cpuDivs = result.divergences.filter(d => d.component === 'cpu');

        // Compare zero page
        const jsbeebZP = oracle.readMemory(0, 256);
        const beebiumZP = await client.peekMemory(0, 256);
        let zpDiffs = 0;
        for (let i = 0; i < 256; i++) {
            if (jsbeebZP[i] !== beebiumZP[i]) {
                zpDiffs++;
            }
        }

        // Log results for visibility
        console.log(`jsbeeb: PC=$${jsbeebState.cpu.pc.toString(16).toUpperCase()}, cycles=${jsbeebState.cycles}`);
        console.log(`Beebium: PC=$${beebiumState.cpu.pc.toString(16).toUpperCase()}, cycles=${beebiumState.cycles}`);
        console.log(`Cycle difference: ${Math.abs(jsbeebState.cycles - beebiumState.cycles)} (expected ~40)`);
        console.log(`CPU divergences: ${cpuDivs.length}`);
        console.log(`Zero page differences: ${zpDiffs}/256 bytes`);

        // Assertions - CPU state and memory must match
        expect(cpuDivs.length).toBe(0);
        expect(zpDiffs).toBe(0);
    }, 60000);
});
