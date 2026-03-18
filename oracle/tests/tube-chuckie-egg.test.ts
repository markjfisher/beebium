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
import { Beebium, screenContains } from 'beebium';

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

/** Read function for screenContains -- reads from host memory. */
function hostReadFn(host: Beebium) {
    return async (addr: number, len: number) =>
        host.memory.address.peek.read(addr, len);
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

    it('memory comparison after decompression', async () => {
        // Compare parasite memory after decompression completes on both.
        //
        // jsbeeb: decompression succeeds, stop at $0816 (JMP ($FFFC)).
        // Beebium: decompression hangs at R1 poll ($09C8-$09D9) after
        //   consuming all 702 bytes and writing 64K. Both have completed
        //   the same decompression work; the difference is jsbeeb's
        //   output is correct (decompressor terminates) while Beebium's
        //   is wrong (decompressor asks for byte 703).

        // jsbeeb: boot and stop at decompressor exit
        const oracle = await bootJsbeeb();
        await oracle.runUntilParasiteAddress(0x0816, 30);
        const jsState = oracle.getParasiteCpuState();
        console.log(`jsbeeb parasite at $${jsState.pc.toString(16).toUpperCase()}`);
        expect(jsState.pc).toBe(0x0816);

        // Beebium: boot, load game, wait for decompressor hang
        const host = await Beebium.launch({
            args: [
                '--tube', '65C02-3MHz',
                '--fdc', 'acorn-1770',
                '--sideways', `14:rom:${DFS_ROM_FILEPATH}`,
            ],
            timeoutMs: 20000,
        });

        try {
            // Boot to Tube banner
            await host.runUntilOrTimeout(
                () => screenContains(hostReadFn(host), "Acorn TUBE"),
                10,
            );

            // Insert disc and type command
            await host.disc.drive(0).insert(DISC_FILEPATH_ABS);
            await host.keyboard.type("*EXEC !BOOT\r");

            // Run coupled, wait for parasite to reach the decompressor
            // R1 poll hang ($09C8-$09D9 range).
            const parasite = await host.connectParasite();
            const reachedHang = await host.runUntilOrTimeout(
                async () => {
                    if (await parasite.debugger.isRunning()) await parasite.debugger.stop();
                    const regs = await parasite.cpu.getRegisters();
                    const inR1Poll = regs.pc >= 0x09C8 && regs.pc <= 0x09D9;
                    if (!inR1Poll) {
                        try { await parasite.debugger.run(); } catch { /* */ }
                    }
                    return inR1Poll;
                },
                30,
                { coupled: true },
            );

            if (await host.debugger.isRunning()) await host.debugger.stop();
            if (await parasite.debugger.isRunning()) await parasite.debugger.stop();

            const beeRegs = await parasite.cpu.getRegisters();
            console.log(`Beebium parasite at $${beeRegs.pc.toString(16).toUpperCase()}`);
            expect(reachedHang).toBe(true);

            // Compare full 64K parasite memory in 256-byte pages.
            // Skip $FE00-$FEFF (Tube I/O registers -- peek returns
            // live register state, not decompressed data).
            let firstDiffAddr = -1;
            let firstDiffJs = 0;
            let firstDiffBee = 0;
            let totalDiffs = 0;
            const diffPages: number[] = [];

            for (let page = 0; page < 256; page++) {
                if (page === 0xFE) continue;  // Skip Tube I/O page
                const addr = page * 256;
                const jsData = oracle.readParasiteMemory(addr, 256);
                const beeData = await parasite.memory.address.peek.read(addr, 256);
                let pageDiffs = 0;
                for (let i = 0; i < 256; i++) {
                    if (jsData[i] !== beeData[i]) {
                        if (firstDiffAddr < 0) {
                            firstDiffAddr = addr + i;
                            firstDiffJs = jsData[i];
                            firstDiffBee = beeData[i];
                        }
                        pageDiffs++;
                        totalDiffs++;
                    }
                }
                if (pageDiffs > 0) {
                    diffPages.push(page);
                }
            }

            console.log(`\nTotal differing bytes: ${totalDiffs}/65280 (excl I/O page)`);
            if (diffPages.length > 0) {
                console.log(`Differing pages: ${diffPages.map(p => '$' + (p * 256).toString(16).toUpperCase().padStart(4, '0')).join(', ')}`);
            }

            if (firstDiffAddr >= 0) {
                console.log(`\nFirst divergence at $${firstDiffAddr.toString(16).toUpperCase().padStart(4, '0')}:`);
                console.log(`  jsbeeb=$${firstDiffJs.toString(16).toUpperCase().padStart(2, '0')}`);
                console.log(`  Beebium=$${firstDiffBee.toString(16).toUpperCase().padStart(2, '0')}`);

                // Dump context around the first few divergences
                const contextStart = Math.max(0, firstDiffAddr - 16) & ~0xF;
                const contextLen = 64;
                const jsContext = oracle.readParasiteMemory(contextStart, contextLen);
                const beeContext = await parasite.memory.address.peek.read(contextStart, contextLen);
                console.log(`\nContext around $${firstDiffAddr.toString(16).toUpperCase()}:`);
                for (let row = 0; row < contextLen; row += 16) {
                    const rowAddr = contextStart + row;
                    const jsHex = Array.from(jsContext.slice(row, row + 16))
                        .map(b => b.toString(16).padStart(2, '0')).join(' ');
                    const beeHex = Array.from(beeContext.slice(row, row + 16))
                        .map(b => b.toString(16).padStart(2, '0')).join(' ');
                    const markers = Array.from({ length: 16 }, (_, i) =>
                        jsContext[row + i] !== beeContext[row + i] ? '^^' : '  '
                    ).join(' ');
                    console.log(`  $${rowAddr.toString(16).toUpperCase().padStart(4, '0')}: js  ${jsHex}`);
                    console.log(`  $${rowAddr.toString(16).toUpperCase().padStart(4, '0')}: bee ${beeHex}`);
                    if (markers.trim()) {
                        console.log(`         ${markers}`);
                    }
                }
                // For each divergent page, show the first differing offset
                console.log('\nPer-page first divergence:');
                for (const page of diffPages) {
                    const addr = page * 256;
                    const jsData = oracle.readParasiteMemory(addr, 256);
                    const beeData = await parasite.memory.address.peek.read(addr, 256);
                    for (let i = 0; i < 256; i++) {
                        if (jsData[i] !== beeData[i]) {
                            const a = addr + i;
                            console.log(`  $${a.toString(16).toUpperCase().padStart(4, '0')}: js=$${jsData[i].toString(16).toUpperCase().padStart(2, '0')} bee=$${beeData[i].toString(16).toUpperCase().padStart(2, '0')}`);
                            break;
                        }
                    }
                }
            } else {
                console.log('\nAll bytes match!');
            }

            await parasite.close();
        } finally {
            await host.close();
        }
    }, 120000);

    it('pre-decompression memory comparison', async () => {
        // Compare FULL parasite memory at $0810 (first R4 ack) on both
        // emulators, BEFORE the decompressor writes any output. If the
        // memory differs here, the LZ back-references will diverge even
        // with identical R1 input data.
        //
        // Both emulators are stopped at the exact same parasite PC ($0810).

        // jsbeeb: boot and stop at first R4 ack
        console.log('[1] Booting jsbeeb...');
        const oracle = await bootJsbeeb();
        console.log('[2] jsbeeb booted, running to $0810...');
        await oracle.runUntilParasiteAddress(FIRST_R4_ACK, 10);
        console.log('[3] jsbeeb parasite at $0810');
        expect(oracle.getParasiteCpuState().pc).toBe(FIRST_R4_ACK);

        // Beebium: boot, load game, stop parasite at $0810 via breakpoint
        console.log('[4] Launching Beebium...');
        const host = await Beebium.launch({
            args: [
                '--tube', '65C02-3MHz',
                '--fdc', 'acorn-1770',
                '--sideways', `14:rom:${DFS_ROM_FILEPATH}`,
            ],
            timeoutMs: 20000,
        });

        try {
            // Start the host running (it may be paused in WaitMode after launch)
            try { await host.debugger.run(); } catch { /* already running */ }
            console.log('[5] Booting to Tube banner...');
            const bannerFound = await host.runUntilOrTimeout(
                () => screenContains(hostReadFn(host), "Acorn TUBE"),
                10,
            );
            console.log(`[6] Tube banner: ${bannerFound ? 'visible' : 'NOT found'}`);
            if (!bannerFound) throw new Error("Tube banner not found");

            // Connect to parasite
            const parasite = await host.connectParasite();
            const pState = await parasite.debugger.getState();
            console.log(`[6a] Parasite connected: running=${pState.isRunning}, cycles=${pState.cycleCount}`);

            // Queue disc insert and keyboard command (while both stopped)
            await host.disc.drive(0).insert(DISC_FILEPATH_ABS);
            await host.keyboard.type("*EXEC !BOOT\r");
            console.log('[7] Disc inserted, command typed');

            // Start host running freely, then use parasite.runUntil($0810)
            // to wait for the decompressor entry point. The host must be
            // running for the Tube protocol to function.
            await host.debugger.run();
            console.log('[8] Host running, waiting for parasite to reach $0810...');
            const stopEvent = await parasite.debugger.runUntil(FIRST_R4_ACK, 60000);
            console.log(`[9] Parasite stopped: reason=${stopEvent.reason}, message="${stopEvent.message}", cycles=${stopEvent.state.cycleCount}`);

            // Stop host too
            if (await host.debugger.isRunning()) await host.debugger.stop();

            // Verify parasite stopped at expected address
            const beeRegs = await parasite.cpu.getRegisters();
            console.log(`[10] Beebium parasite at $${beeRegs.pc.toString(16).toUpperCase().padStart(4, '0')}`);
            expect(beeRegs.pc).toBe(FIRST_R4_ACK);

            // Compare full 64K parasite memory page by page.
            // Skip $FE page (Tube I/O registers).
            let firstDiffAddr = -1;
            let firstDiffJs = 0;
            let firstDiffBee = 0;
            let totalDiffs = 0;
            const diffPages: number[] = [];

            for (let page = 0; page < 256; page++) {
                if (page === 0xFE) continue;
                const addr = page * 256;
                const jsData = oracle.readParasiteMemory(addr, 256);
                const beeData = await parasite.memory.address.peek.read(addr, 256);
                let pageDiffs = 0;
                for (let i = 0; i < 256; i++) {
                    if (jsData[i] !== beeData[i]) {
                        if (firstDiffAddr < 0) {
                            firstDiffAddr = addr + i;
                            firstDiffJs = jsData[i];
                            firstDiffBee = beeData[i];
                        }
                        pageDiffs++;
                        totalDiffs++;
                    }
                }
                if (pageDiffs > 0) {
                    diffPages.push(page);
                }
            }

            console.log(`\nTotal differing bytes: ${totalDiffs}/65280 (excl I/O page)`);

            if (totalDiffs === 0) {
                console.log('All parasite memory matches at $0810.');
                console.log('Pre-decompression state is identical -- the divergence');
                console.log('must be in the decompressor execution itself.');
            } else {
                console.log(`Differing pages: ${diffPages.map(p =>
                    '$' + (p * 256).toString(16).toUpperCase().padStart(4, '0')
                ).join(', ')}`);

                console.log(`\nFirst divergence at $${firstDiffAddr.toString(16).toUpperCase().padStart(4, '0')}:`);
                console.log(`  jsbeeb=$${firstDiffJs.toString(16).toUpperCase().padStart(2, '0')}`);
                console.log(`  Beebium=$${firstDiffBee.toString(16).toUpperCase().padStart(2, '0')}`);

                // Dump context around each divergent page's first difference
                console.log('\nPer-page first divergence:');
                for (const page of diffPages) {
                    const addr = page * 256;
                    const jsData = oracle.readParasiteMemory(addr, 256);
                    const beeData = await parasite.memory.address.peek.read(addr, 256);
                    for (let i = 0; i < 256; i++) {
                        if (jsData[i] !== beeData[i]) {
                            const a = addr + i;
                            console.log(`  $${a.toString(16).toUpperCase().padStart(4, '0')}: ` +
                                `js=$${jsData[i].toString(16).toUpperCase().padStart(2, '0')} ` +
                                `bee=$${beeData[i].toString(16).toUpperCase().padStart(2, '0')}`);
                            break;
                        }
                    }
                }

                // Hex dump of $FC00-$FC3F on both (first 64 bytes of decompressor output area)
                console.log('\n$FC00-$FC3F hex dump:');
                const jsFC = oracle.readParasiteMemory(0xFC00, 64);
                const beeFC = await parasite.memory.address.peek.read(0xFC00, 64);
                for (let row = 0; row < 4; row++) {
                    const off = row * 16;
                    const a = 0xFC00 + off;
                    const jsHex = Array.from(jsFC.slice(off, off + 16))
                        .map(b => b.toString(16).padStart(2, '0')).join(' ');
                    const beeHex = Array.from(beeFC.slice(off, off + 16))
                        .map(b => b.toString(16).padStart(2, '0')).join(' ');
                    const markers = Array.from({ length: 16 }, (_, i) =>
                        jsFC[off + i] !== beeFC[off + i] ? '^^' : '  '
                    ).join(' ');
                    console.log(`  $${a.toString(16).toUpperCase().padStart(4, '0')}: js  ${jsHex}`);
                    console.log(`  $${a.toString(16).toUpperCase().padStart(4, '0')}: bee ${beeHex}`);
                    if (markers.trim()) console.log(`         ${markers}`);
                }
            }

            // Also dump CPU state comparison
            const jsState = oracle.getParasiteCpuState();
            console.log(`\nCPU state at $0810:`);
            console.log(`  jsbeeb:  A=$${jsState.a.toString(16).toUpperCase().padStart(2, '0')} X=$${jsState.x.toString(16).toUpperCase().padStart(2, '0')} Y=$${jsState.y.toString(16).toUpperCase().padStart(2, '0')} SP=$${jsState.sp.toString(16).toUpperCase().padStart(2, '0')} P=$${jsState.p.toString(16).toUpperCase().padStart(2, '0')}`);
            console.log(`  Beebium: A=$${beeRegs.a.toString(16).toUpperCase().padStart(2, '0')} X=$${beeRegs.x.toString(16).toUpperCase().padStart(2, '0')} Y=$${beeRegs.y.toString(16).toUpperCase().padStart(2, '0')} SP=$${beeRegs.sp.toString(16).toUpperCase().padStart(2, '0')} P=$${beeRegs.p.toString(16).toUpperCase().padStart(2, '0')}`);

            await parasite.close();
        } finally {
            await host.close();
        }
    }, 120000);
});
