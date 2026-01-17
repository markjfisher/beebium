/**
 * Debug test for breakpoint functionality
 */
import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { ServerFixture, BeebiumClient } from '../src/index.js';

describe('Breakpoint Debug', () => {
    let fixture: ServerFixture;
    let client: BeebiumClient;

    beforeAll(async () => {
        const romDir = '/Users/rjs/Code/beebium/roms';
        fixture = new ServerFixture({
            timeout: 10000,
            model: 'B-RomRam',
            fdc: 'acorn-1770',
            sideways: [
                { slot: 14, type: 'rom', filepath: `${romDir}/acorn-dfs_2_26.rom` },
            ],
        });
        client = await fixture.start();
    }, 30000);

    afterAll(async () => {
        await fixture.stop();
    });

    it('should hit a breakpoint near reset vector', async () => {
        // Initial state - machine is paused at first instruction
        let state = await client.getState();
        console.log('Initial: paused=' + !state.isRunning + ' cycles=' + state.cycleCount);

        const cpu = await client.getCpuState();
        console.log('Initial PC=$' + cpu.pc.toString(16).toUpperCase());

        // Set breakpoint a few instructions ahead (D9CD + ~10 = ~D9D7)
        // The exact address doesn't matter as long as it's in the boot path
        const targetPC = cpu.pc + 10;
        const bpId = await client.addBreakpoint(targetPC);
        console.log('Set breakpoint at $' + targetPC.toString(16).toUpperCase() + ' id=' + bpId);

        // Run
        await client.run();
        console.log('Called run(), waiting...');

        // Wait for breakpoint to be hit
        await new Promise(r => setTimeout(r, 1000));

        // Check state
        state = await client.getState();
        const cpuAfter = await client.getCpuState();
        console.log('After 1s: isRunning=' + state.isRunning + ' cycles=' + state.cycleCount + ' PC=$' + cpuAfter.pc.toString(16).toUpperCase());

        // Should have stopped at the breakpoint
        // addBreakpoint() and getCpuState() both handle PC normalization internally,
        // so the reported PC should match the requested breakpoint address
        expect(state.isRunning).toBe(false);
        expect(cpuAfter.pc).toBe(targetPC);

        // Cleanup
        await client.removeBreakpoint(bpId);
    }, 30000);
});
