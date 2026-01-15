/**
 * Boot Check Test - verify the machine boots to BASIC prompt correctly.
 */

import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { BeebiumClient, ServerFixture } from '../src/index.js';

describe('Boot Check', () => {
    let fixture: ServerFixture;
    let client: BeebiumClient;

    beforeAll(async () => {
        fixture = new ServerFixture({ timeout: 10000, model: 'B' });
        client = await fixture.start();
    }, 30000);

    afterAll(async () => {
        await fixture.stop();
    });

    it('should boot to BASIC prompt', async () => {
        console.log('\n=== Boot Check ===\n');

        // Check initial state before running
        const beforeState = await client.getState();
        console.log(`Before run(): cycles=${beforeState.cycleCount}, isRunning=${beforeState.isRunning}`);

        // Let it boot without reset (server already ran 7 cycles after reset)
        console.log('Calling run()...');
        await client.run();

        // Check state immediately after run()
        const afterRunState = await client.getState();
        console.log(`After run(): cycles=${afterRunState.cycleCount}, isRunning=${afterRunState.isRunning}`);

        console.log('Polling state every 500ms...');
        for (let i = 0; i < 6; i++) {
            await new Promise(resolve => setTimeout(resolve, 500));
            const pollState = await client.getState();
            console.log(`  ${(i+1)*500}ms: cycles=${pollState.cycleCount}, isRunning=${pollState.isRunning}`);
        }

        // Check state before stop
        const beforeStopState = await client.getState();
        console.log(`Before stop(): cycles=${beforeStopState.cycleCount}, isRunning=${beforeStopState.isRunning}`);

        await client.stop();

        const state = await client.getState();
        const cpu = await client.getCpuState();

        console.log(`State after boot:`);
        console.log(`  Cycles: ${state.cycleCount.toLocaleString()}`);
        console.log(`  PC=$${cpu.pc.toString(16).toUpperCase().padStart(4, '0')}`);

        // Read more of the screen (Mode 7: 25 rows x 40 columns)
        console.log('\nScreen contents (Mode 7):');
        const screenStart = 0x7C00;
        for (let row = 0; row < 10; row++) {
            const rowMem = await client.peekMemory(screenStart + row * 40, 40);
            let rowText = '';
            for (const byte of rowMem) {
                if (byte >= 0x20 && byte < 0x7F) {
                    rowText += String.fromCharCode(byte);
                } else {
                    rowText += ' ';
                }
            }
            console.log(`  ${row}: "${rowText}"`);
        }

        // Check for expected boot message on line 2 (row index 1)
        // Line 0 is blank, Line 1 has "BBC Computer 32K"
        const line2 = await client.peekMemory(0x7C00 + 40, 40);
        let line2Text = '';
        for (const byte of line2) {
            if (byte >= 0x20 && byte < 0x7F) {
                line2Text += String.fromCharCode(byte);
            } else {
                line2Text += ' ';
            }
        }
        console.log(`\nLine 2 check: "${line2Text.substring(0, 20)}"`);

        // The boot message should be "BBC Computer 32K"
        expect(line2Text).toContain('BBC');

        console.log('\nBoot successful!');
    }, 30000);
});
