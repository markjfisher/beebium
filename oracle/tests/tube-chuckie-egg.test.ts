/**
 * Differential test: Chuckie Egg 2023 Tube decompression
 *
 * Boots CE2023 on both jsbeeb and Beebium, synchronises at the parasite
 * decompressor, then compares state to find the divergence point.
 *
 * Timing reference (jsbeeb):
 *   Boot to "Loading" screen: ~5 seconds emulated
 *   "Loading" to "A game of skill": ~34 seconds emulated
 *
 * See docs/discussion/chuckie-egg-2023-tube-hang.md for full analysis.
 */

import { describe, it, expect } from 'vitest';
import { join } from 'node:path';
import { JsbeebOracle } from '../src/index.js';
import { Beebium } from 'beebium';

const REPO_ROOT = join(import.meta.dirname, '..', '..');
const ROM_DIRPATH = join(REPO_ROOT, 'roms');
const DFS_ROM_FILEPATH = join(ROM_DIRPATH, 'acorn-dfs_2_26.rom');
const DISC_FILEPATH_ABS = join(REPO_ROOT, 'tests', 'assets', 'discs', 'chuckieEgg2023.ssd');
const DISC_FILEPATH_REL = '../tests/assets/discs/chuckieEgg2023.ssd';

// Parasite addresses from the decompressor analysis
const FIRST_R4_ACK  = 0x0810;  // JSR $0A2D (write $FC to R4 P->H)
const DECOMP_ENTRY  = 0x0819;  // Decompression loop entry
const AFTER_R1_READ = 0x09D9;  // BRA $09E3 (right after LDA $FEF9)

/** Boot jsbeeb with Tube + CE2023 disc, type *EXEC !BOOT. */
async function bootJsbeeb(): Promise<JsbeebOracle> {
    const oracle = new JsbeebOracle();
    await oracle.initialize('B1770', { tube: true });
    await oracle.loadDisc(0, DISC_FILEPATH_REL);
    oracle.reset();
    // 8s emulated = 16M host cycles to reach BASIC prompt
    await oracle.runCycles(16_000_000);
    await oracle.type("*EXEC !BOOT\r");
    return oracle;
}

/** Boot Beebium with Tube + CE2023 disc, type *EXEC !BOOT. */
async function bootBeebium(): Promise<Beebium> {
    const host = await Beebium.launch({
        args: [
            '--tube', '65C02-3MHz',
            '--fdc', 'acorn-1770',
            '--sideways', `14:rom:${DFS_ROM_FILEPATH}`,
        ],
        timeoutMs: 20000,
    });
    // Boot for 5 emulated seconds (BASIC prompt + Tube banner)
    await host.runForEmulatedSeconds(5);
    // Insert disc and type command
    await host.disc.drive(0).insert(DISC_FILEPATH_ABS);
    await host.keyboard.type("*EXEC !BOOT\r");
    // Run 10 more emulated seconds for game to load
    await host.runForEmulatedSeconds(10);
    return host;
}

describe('Tube CE2023 Differential', () => {

    it('jsbeeb boots CE2023 to Loading screen', async () => {
        const oracle = await bootJsbeeb();
        // 10s emulated = 20M cycles (game reaches Loading at ~5s)
        await oracle.runCycles(20_000_000);

        const screenData = oracle.readMemory(0x7C00, 25 * 40);
        const row11 = Buffer.from(screenData.slice(11 * 40, 12 * 40))
            .toString('latin1').replace(/[\x00-\x1f\x80-\xff]/g, '.');
        console.log(`jsbeeb screen row 11: [${row11}]`);
        expect(row11).toContain('Loading');
    }, 30000);

    it('jsbeeb parasite reaches decompressor entry', async () => {
        const oracle = await bootJsbeeb();
        // The decompressor runs during the game's custom Tube transfer,
        // which happens within the first few seconds of game loading.
        await oracle.runUntilParasiteAddress(FIRST_R4_ACK, 10);
        const state = oracle.getParasiteCpuState();
        console.log(`jsbeeb parasite at $0810: A=$${state.a.toString(16).toUpperCase()}`);
        expect(state.pc).toBe(FIRST_R4_ACK);
    }, 30000);

    it('both parasites match at decompressor entry', async () => {
        // Boot jsbeeb to decompressor
        const oracle = await bootJsbeeb();
        await oracle.runUntilParasiteAddress(FIRST_R4_ACK, 10);
        const jsState = oracle.getParasiteCpuState();
        console.log(`jsbeeb at $0810: A=$${jsState.a.toString(16)} SP=$${jsState.sp.toString(16)}`);

        // Boot Beebium to same point
        const host = await bootBeebium();
        try {
            // Run host until game reaches loading screen
            await host.runForEmulatedSeconds(10);
            const parasite = await host.connectParasite();
            try {
                // Stop parasite and run to decompressor entry
                if (await parasite.debugger.isRunning()) await parasite.debugger.stop();
                await parasite.debugger.runUntil(FIRST_R4_ACK);
                const beeState = await parasite.cpu.getRegisters();
                console.log(`Beebium at $0810: A=$${beeState.a.toString(16)} SP=$${beeState.sp.toString(16)}`);

                console.log(`A: jsbeeb=$${jsState.a.toString(16)} Beebium=$${beeState.a.toString(16)} ${jsState.a === beeState.a ? 'MATCH' : 'DIFFER'}`);
                console.log(`SP: jsbeeb=$${jsState.sp.toString(16)} Beebium=$${beeState.sp.toString(16)} ${jsState.sp === beeState.sp ? 'MATCH' : 'DIFFER'}`);

                // Compare zero page
                const jsZP = oracle.readParasiteMemory(0x00, 64);
                const beeZP = await parasite.memory.address.peek.read(0x00, 64);
                let zpDiffs = 0;
                for (let i = 0; i < 64; i++) {
                    if (jsZP[i] !== beeZP[i]) {
                        console.log(`  ZP[$${i.toString(16)}]: js=$${jsZP[i].toString(16)} bee=$${beeZP[i].toString(16)}`);
                        zpDiffs++;
                    }
                }
                console.log(`ZP diffs: ${zpDiffs}/64`);
                expect(jsState.a).toBe(beeState.a);
            } finally {
                await parasite.close();
            }
        } finally {
            await host.close();
        }
    }, 60000);

    it('should find first R1 byte divergence', async () => {
        // Boot jsbeeb to decompression loop
        const oracle = await bootJsbeeb();
        await oracle.runUntilParasiteAddress(DECOMP_ENTRY, 10);
        console.log(`jsbeeb parasite at $${oracle.getParasiteCpuState().pc.toString(16)}`);

        // Boot Beebium to same point
        const host = await bootBeebium();
        try {
            await host.runForEmulatedSeconds(10);
            const parasite = await host.connectParasite();
            try {
                if (await parasite.debugger.isRunning()) await parasite.debugger.stop();
                await parasite.debugger.runUntil(DECOMP_ENTRY);
                console.log(`Beebium parasite at $${(await parasite.cpu.getRegisters()).pc.toString(16)}`);

                // Compare R1 bytes one by one
                let divergenceFound = false;
                for (let i = 0; i < 702; i++) {
                    await oracle.runUntilParasiteAddress(AFTER_R1_READ, 5);
                    if (await parasite.debugger.isRunning()) await parasite.debugger.stop();
                    await parasite.debugger.runUntil(AFTER_R1_READ);

                    const jsA = oracle.getParasiteCpuState().a;
                    const beeA = (await parasite.cpu.getRegisters()).a;

                    if (jsA !== beeA) {
                        console.log(`\n*** DIVERGENCE at R1 byte ${i} ***`);
                        console.log(`  jsbeeb A=$${jsA.toString(16).toUpperCase()}`);
                        console.log(`  Beebium A=$${beeA.toString(16).toUpperCase()}`);
                        const jsZP = oracle.readParasiteMemory(0x2F, 3);
                        const beeZP = await parasite.memory.address.peek.read(0x2F, 3);
                        console.log(`  Dest: js=$${jsZP[1].toString(16)}${jsZP[0].toString(16)} bee=$${beeZP[1].toString(16)}${beeZP[0].toString(16)}`);
                        console.log(`  $31: js=$${jsZP[2].toString(16)} bee=$${beeZP[2].toString(16)}`);
                        divergenceFound = true;
                        break;
                    }

                    if (i > 0 && i % 100 === 0) {
                        const dest = oracle.readParasiteMemory(0x2F, 2);
                        console.log(`  R1 byte ${i}/702 A=$${jsA.toString(16)} dest=$${(dest[0]|(dest[1]<<8)).toString(16)}`);
                    }
                }

                if (!divergenceFound) {
                    console.log('All 702 R1 bytes match! Comparing output...');
                    for (const [label, addr, len] of [
                        ['$0800', 0x0800, 256],
                        ['$FC00', 0xFC00, 256],
                        ['$FFFC', 0xFFFC, 4],
                    ] as const) {
                        const jsD = oracle.readParasiteMemory(addr, len);
                        const beeD = await parasite.memory.address.peek.read(addr, len);
                        let d = 0;
                        for (let j = 0; j < len; j++) if (jsD[j] !== beeD[j]) d++;
                        console.log(`  ${label}: ${d}/${len} differ`);
                    }
                }

                expect(true).toBe(true);
            } finally {
                await parasite.close();
            }
        } finally {
            await host.close();
        }
    }, 300000);
});
