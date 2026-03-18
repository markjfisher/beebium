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

    it('find first parasite instruction divergence', async () => {
        // Collect PC traces from both parasites starting at $0813
        // (the JSR $0819 decompression entry) and find where they diverge.

        const P_MASK = 0xCF; // mask out bits 4-5 (unused/break)
        const MAX_INSTRUCTIONS = 5000;

        // jsbeeb: boot to $0813 and collect PC trace
        const oracle = await bootJsbeeb();
        await oracle.runUntilParasiteAddress(FIRST_R4_ACK, 10);
        // Step past $0810 (JSR $0A2D) to $0813 (JSR $0819)
        const parasiteCpu = (oracle as any).parasiteProcessor;

        // Collect jsbeeb trace by running with debug hook
        const jsTrace: Array<{ pc: number; a: number; x: number; y: number; sp: number; p: number }> = [];
        parasiteCpu._debugInstruction = (pc: number) => {
            jsTrace.push({
                pc,
                a: parasiteCpu.a,
                x: parasiteCpu.x,
                y: parasiteCpu.y,
                sp: parasiteCpu.s,
                p: parasiteCpu.p.asByte() & P_MASK,
            });
            return jsTrace.length >= MAX_INSTRUCTIONS; // stop after N instructions
        };
        (oracle as any).processor.execute(50_000_000); // run enough host cycles
        parasiteCpu._debugInstruction = null;

        console.log(`jsbeeb trace: ${jsTrace.length} instructions collected`);
        if (jsTrace.length > 0) {
            console.log(`  first: PC=$${jsTrace[0].pc.toString(16).toUpperCase()}`);
            console.log(`  last:  PC=$${jsTrace[jsTrace.length - 1].pc.toString(16).toUpperCase()}`);
        }

        // Beebium: boot to $0810 and step parasite
        const host = await Beebium.launch({
            args: [
                '--tube', '65C02-3MHz',
                '--fdc', 'acorn-1770',
                '--sideways', `14:rom:${DFS_ROM_FILEPATH}`,
            ],
            timeoutMs: 20000,
        });

        try {
            await host.runUntilOrTimeout(
                () => screenContains(hostReadFn(host), "Acorn TUBE"),
                10,
            );
            const parasite = await host.connectParasite();

            // Insert disc, type command, run until $0810
            await host.disc.drive(0).insert(DISC_FILEPATH_ABS);
            await host.keyboard.type("*EXEC !BOOT\r");
            await host.debugger.run();
            await parasite.debugger.runUntil(FIRST_R4_ACK, 60000);
            if (await host.debugger.isRunning()) await host.debugger.stop();

            // Both parasites are now at $0810. Step through and compare.
            // Start the host running (needed for Tube I/O during decompression)
            await host.debugger.run();

            const limit = Math.min(jsTrace.length, MAX_INSTRUCTIONS);
            let divergeAt = -1;

            for (let i = 0; i < limit; i++) {
                // Read state BEFORE stepping (pre-instruction, matches jsbeeb hook)
                const regs = await parasite.cpu.getRegisters();
                const beeState = {
                    pc: regs.pc,
                    a: regs.a,
                    x: regs.x,
                    y: regs.y,
                    sp: regs.sp,
                    p: regs.p & P_MASK,
                };
                await parasite.debugger.step(1);

                const jsState = jsTrace[i];
                if (beeState.pc !== jsState.pc ||
                    beeState.a !== jsState.a ||
                    beeState.x !== jsState.x ||
                    beeState.y !== jsState.y ||
                    beeState.sp !== jsState.sp ||
                    beeState.p !== jsState.p) {

                    divergeAt = i;
                    console.log(`\nDivergence at instruction #${i}:`);
                    console.log(`  jsbeeb:  PC=$${jsState.pc.toString(16).toUpperCase().padStart(4, '0')} A=$${jsState.a.toString(16).toUpperCase().padStart(2, '0')} X=$${jsState.x.toString(16).toUpperCase().padStart(2, '0')} Y=$${jsState.y.toString(16).toUpperCase().padStart(2, '0')} SP=$${jsState.sp.toString(16).toUpperCase().padStart(2, '0')} P=$${jsState.p.toString(16).toUpperCase().padStart(2, '0')}`);
                    console.log(`  Beebium: PC=$${beeState.pc.toString(16).toUpperCase().padStart(4, '0')} A=$${beeState.a.toString(16).toUpperCase().padStart(2, '0')} X=$${beeState.x.toString(16).toUpperCase().padStart(2, '0')} Y=$${beeState.y.toString(16).toUpperCase().padStart(2, '0')} SP=$${beeState.sp.toString(16).toUpperCase().padStart(2, '0')} P=$${beeState.p.toString(16).toUpperCase().padStart(2, '0')}`);

                    // Show context: previous 5 instructions
                    if (i >= 5) {
                        console.log(`\nPreceding instructions (jsbeeb):`);
                        for (let j = i - 5; j < i; j++) {
                            const s = jsTrace[j];
                            console.log(`  #${j}: PC=$${s.pc.toString(16).toUpperCase().padStart(4, '0')} A=$${s.a.toString(16).toUpperCase().padStart(2, '0')} X=$${s.x.toString(16).toUpperCase().padStart(2, '0')} Y=$${s.y.toString(16).toUpperCase().padStart(2, '0')} P=$${s.p.toString(16).toUpperCase().padStart(2, '0')}`);
                        }
                    }

                    // Disassemble around the divergence point
                    const disAddr = Math.max(0, jsState.pc - 8);
                    const jsCode = oracle.readParasiteMemory(disAddr, 32);
                    console.log(`\njsbeeb code around $${jsState.pc.toString(16).toUpperCase()}:`);
                    console.log(`  ${Array.from(jsCode).map(b => b.toString(16).padStart(2, '0')).join(' ')}`);

                    break;
                }

                // Progress every 10000 instructions
                if (i > 0 && i % 10000 === 0) {
                    console.log(`  ... ${i} instructions match (PC=$${beeState.pc.toString(16).toUpperCase()})`);
                }
            }

            if (divergeAt < 0) {
                console.log(`\nAll ${limit} instructions match!`);
            }

            if (await host.debugger.isRunning()) await host.debugger.stop();
            await parasite.close();
        } finally {
            await host.close();
        }
    }, 600000);  // 10 minute timeout for stepping

    it('compare R1 data bytes received by decompressor', async () => {
        // Compare the actual data bytes read from R1 by the decompressor on
        // both emulators. The bit-serial reader at $09C8 reads each R1 byte
        // via LDA $FEF9 at $09D6. We capture A register right after that
        // instruction (at $09D9, the BRA $09E3) on both sides.
        //
        // jsbeeb: use _debugInstruction hook to capture A at $09D9
        // Beebium: use breakpoint at $09D9 with hit counting

        const R1_READ_DONE = 0x09D9;  // BRA $09E3, right after LDA $FEF9
        const EXPECTED_BYTES = 702;

        // jsbeeb: boot to $0810, then collect A register at each $09D9 hit
        const oracle = await bootJsbeeb();
        await oracle.runUntilParasiteAddress(FIRST_R4_ACK, 10);
        expect(oracle.getParasiteCpuState().pc).toBe(FIRST_R4_ACK);

        const parasiteCpu = (oracle as any).parasiteProcessor;
        const jsR1Bytes: number[] = [];
        parasiteCpu._debugInstruction = (pc: number) => {
            if (pc === R1_READ_DONE) {
                jsR1Bytes.push(parasiteCpu.a);
            }
            // Stop when we have enough bytes or hit $0816 (decompressor exit)
            return jsR1Bytes.length >= EXPECTED_BYTES + 10 || pc === 0x0816;
        };
        (oracle as any).processor.execute(100_000_000);
        parasiteCpu._debugInstruction = null;

        console.log(`jsbeeb: ${jsR1Bytes.length} R1 bytes captured`);
        console.log(`  first 16: ${jsR1Bytes.slice(0, 16).map(b => b.toString(16).padStart(2, '0')).join(' ')}`);

        // Beebium: boot to $0810, then capture A at each $09D9 hit
        const host = await Beebium.launch({
            args: [
                '--tube', '65C02-3MHz',
                '--fdc', 'acorn-1770',
                '--sideways', `14:rom:${DFS_ROM_FILEPATH}`,
            ],
            timeoutMs: 20000,
        });

        try {
            await host.runUntilOrTimeout(
                () => screenContains(hostReadFn(host), "Acorn TUBE"),
                10,
            );
            const parasite = await host.connectParasite();
            await host.disc.drive(0).insert(DISC_FILEPATH_ABS);
            await host.keyboard.type("*EXEC !BOOT\r");
            await host.debugger.run();
            await parasite.debugger.runUntil(FIRST_R4_ACK, 60000);
            if (await host.debugger.isRunning()) await host.debugger.stop();

            // Set breakpoint at $09D9 (after LDA $FEF9)
            const bpId = await parasite.debugger.addBreakpoint(R1_READ_DONE);

            // Collect A register at each hit
            const beeR1Bytes: number[] = [];
            await host.debugger.run();

            for (let i = 0; i < EXPECTED_BYTES + 10; i++) {
                await parasite.debugger.runUntil(R1_READ_DONE, 30000);
                const regs = await parasite.cpu.getRegisters();
                beeR1Bytes.push(regs.a);
                if (regs.pc !== R1_READ_DONE) {
                    console.log(`Beebium: unexpected PC=$${regs.pc.toString(16).toUpperCase()} at byte ${i}`);
                    break;
                }
            }

            if (await host.debugger.isRunning()) await host.debugger.stop();

            console.log(`Beebium: ${beeR1Bytes.length} R1 bytes captured`);
            console.log(`  first 16: ${beeR1Bytes.slice(0, 16).map(b => b.toString(16).padStart(2, '0')).join(' ')}`);

            // Compare
            const limit = Math.min(jsR1Bytes.length, beeR1Bytes.length, EXPECTED_BYTES);
            let firstDiff = -1;
            for (let i = 0; i < limit; i++) {
                if (jsR1Bytes[i] !== beeR1Bytes[i]) {
                    firstDiff = i;
                    break;
                }
            }

            if (firstDiff < 0) {
                console.log(`\nAll ${limit} R1 data bytes match!`);
                console.log('The R1 input stream is identical -- divergence must be in');
                console.log('the LZ control flow or the bit-serial reader state.');
            } else {
                console.log(`\nFirst R1 byte difference at byte #${firstDiff}:`);
                console.log(`  jsbeeb=$${jsR1Bytes[firstDiff].toString(16).toUpperCase().padStart(2, '0')}`);
                console.log(`  Beebium=$${beeR1Bytes[firstDiff].toString(16).toUpperCase().padStart(2, '0')}`);

                // Show context
                const start = Math.max(0, firstDiff - 4);
                const end = Math.min(limit, firstDiff + 5);
                console.log(`  context: bytes ${start}-${end - 1}:`);
                for (let i = start; i < end; i++) {
                    const match = jsR1Bytes[i] === beeR1Bytes[i] ? '  ' : '<<';
                    console.log(`    #${i}: js=$${jsR1Bytes[i].toString(16).padStart(2, '0')} bee=$${beeR1Bytes[i].toString(16).padStart(2, '0')} ${match}`);
                }
            }

            await parasite.debugger.removeBreakpoint(bpId);
            await parasite.close();
        } finally {
            await host.close();
        }
    }, 600000);
});
