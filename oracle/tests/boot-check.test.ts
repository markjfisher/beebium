/**
 * Boot Check Test - verify the machine boots to BASIC prompt correctly.
 */

import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { BeebiumClient, ServerFixture } from '../src/index.js';

describe('Boot Check', () => {
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

    it('should boot to BASIC prompt', async () => {
        // Let it boot
        await client.run();
        await new Promise(resolve => setTimeout(resolve, 3000));
        await client.stop();

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

        expect(line2Text).toContain('BBC');
    }, 30000);
});
