/**
 * Convergence test - checks if emulator states converge after MOS initialization.
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
        // - jsbeeb B1770: WD1770 controller, DFS 2.26, ADFS 1.30 (ROMs loaded top-down from slot 15)
        // - Beebium Model B: Acorn 1770 controller, DFS in slot 1, ADFS in slot 4
        //
        // Model B has ROM sockets at slots 0, 1, 4, 15 only.
        // jsbeeb loads ROMs into different slots, but both machines boot into DFS.
        // The same DFS ROM (verified by checksum) should produce identical behavior.
        oracle = new JsbeebOracle();
        await oracle.initialize('B1770');

        // Use absolute paths to ROMs to avoid path resolution issues
        const romDir = '/Users/rjs/Code/beebium/roms';
        fixture = new ServerFixture({
            timeout: 10000,
            model: 'B',
            fdc: 'acorn-1770',
            roms: [
                { slot: 1, filepath: `${romDir}/acorn-dfs_2_26.rom` },
                { slot: 4, filepath: `${romDir}/acorn-adfs_1_30.rom` },
            ],
        });
        client = await fixture.start();

        runner = new DiffRunner(oracle, client);
    }, 30000);

    afterAll(async () => {
        await fixture.stop();
    });

    it('should show divergence patterns over time', async () => {
        // Reset both
        oracle.reset();
        await client.reset();

        // Track divergences at checkpoints
        const checkpoints = [
            { instructions: 0, label: 'After reset' },
            { instructions: 100, label: 'After 100 instructions' },
            { instructions: 1000, label: 'After 1000 instructions' },
            { instructions: 5000, label: 'After 5000 instructions' },
        ];

        console.log('\n=== State Convergence Analysis ===\n');

        let totalInstructions = 0;
        for (const checkpoint of checkpoints) {
            const toStep = checkpoint.instructions - totalInstructions;
            if (toStep > 0) {
                await runner.stepBoth(toStep);
                totalInstructions = checkpoint.instructions;
            }

            const result = await runner.compareState();
            const jsbeebState = oracle.getState();
            const beebiumState = await client.getMachineState();

            console.log(`--- ${checkpoint.label} ---`);
            console.log(`Cycles: jsbeeb=${jsbeebState.cycles} beebium=${beebiumState.cycles}`);
            console.log(`PC: jsbeeb=$${jsbeebState.cpu.pc.toString(16).toUpperCase()} beebium=$${beebiumState.cpu.pc.toString(16).toUpperCase()}`);

            // Count divergences by category
            const cpuDivs = result.divergences.filter(d => d.component === 'cpu');
            const sysViaDivs = result.divergences.filter(d => d.component === 'system_via');
            const userViaDivs = result.divergences.filter(d => d.component === 'user_via');
            const crtcDivs = result.divergences.filter(d => d.component === 'crtc');

            console.log(`Divergences: CPU=${cpuDivs.length}, SysVIA=${sysViaDivs.length}, UserVIA=${userViaDivs.length}, CRTC=${crtcDivs.length}`);

            // Show CPU details
            if (cpuDivs.length > 0) {
                for (const d of cpuDivs) {
                    const jsVal = typeof d.jsbeebValue === 'number' ? `0x${d.jsbeebValue.toString(16).toUpperCase()}` : d.jsbeebValue;
                    const beeVal = typeof d.beebiumValue === 'number' ? `0x${d.beebiumValue.toString(16).toUpperCase()}` : d.beebiumValue;
                    console.log(`  CPU.${d.field}: jsbeeb=${jsVal} beebium=${beeVal}`);
                }
            } else {
                console.log('  CPU: MATCH!');
            }

            // Show VIA details for first checkpoint only
            if (checkpoint.instructions === 0) {
                console.log('  System VIA divergences:');
                for (const d of sysViaDivs) {
                    const jsVal = typeof d.jsbeebValue === 'number' ? `0x${d.jsbeebValue.toString(16).toUpperCase()}` : d.jsbeebValue;
                    const beeVal = typeof d.beebiumValue === 'number' ? `0x${d.beebiumValue.toString(16).toUpperCase()}` : d.beebiumValue;
                    console.log(`    ${d.field}: jsbeeb=${jsVal} beebium=${beeVal}`);
                }
                console.log('  User VIA divergences:');
                for (const d of userViaDivs) {
                    const jsVal = typeof d.jsbeebValue === 'number' ? `0x${d.jsbeebValue.toString(16).toUpperCase()}` : d.jsbeebValue;
                    const beeVal = typeof d.beebiumValue === 'number' ? `0x${d.beebiumValue.toString(16).toUpperCase()}` : d.beebiumValue;
                    console.log(`    ${d.field}: jsbeeb=${jsVal} beebium=${beeVal}`);
                }
            }

            console.log('');
        }

        // Test passes - we're just gathering information
        expect(true).toBe(true);
    }, 60000);

    it('should compare memory at key locations', async () => {
        // Reset both
        oracle.reset();
        await client.reset();

        // Run enough to get past initial MOS setup
        await runner.stepBoth(10000);

        console.log('\n=== Memory Comparison at Key Locations ===\n');

        // Compare zero page (used heavily by MOS)
        const jsbeebZP = oracle.readMemory(0x0000, 256);
        const beebiumZP = await client.peekMemory(0x0000, 256);

        let zpDiffs = 0;
        for (let i = 0; i < 256; i++) {
            if (jsbeebZP[i] !== beebiumZP[i]) {
                zpDiffs++;
            }
        }
        console.log(`Zero page differences: ${zpDiffs}/256 bytes`);

        // Compare stack page
        const jsbeebStack = oracle.readMemory(0x0100, 256);
        const beebiumStack = await client.peekMemory(0x0100, 256);

        let stackDiffs = 0;
        for (let i = 0; i < 256; i++) {
            if (jsbeebStack[i] !== beebiumStack[i]) {
                stackDiffs++;
            }
        }
        console.log(`Stack page differences: ${stackDiffs}/256 bytes`);

        // Compare screen memory start (Mode 7 at 0x7C00)
        const jsbeebScreen = oracle.readMemory(0x7C00, 256);
        const beebiumScreen = await client.peekMemory(0x7C00, 256);

        let screenDiffs = 0;
        for (let i = 0; i < 256; i++) {
            if (jsbeebScreen[i] !== beebiumScreen[i]) {
                screenDiffs++;
            }
        }
        console.log(`Screen memory (0x7C00) differences: ${screenDiffs}/256 bytes`);

        expect(true).toBe(true);
    }, 60000);

    it('should compare state after boot to BASIC prompt', async () => {
        // Reset both emulators
        oracle.reset();
        await client.reset();

        console.log('\n=== Comparison After Boot to BASIC Prompt ===\n');

        // Run jsbeeb until it hits $8000 (language ROM entry)
        console.log('Running jsbeeb until $8000...');
        await oracle.runUntilAddress(0x8000, 30);
        const jsbeebState = oracle.getState();
        console.log(`jsbeeb at PC=$${jsbeebState.cpu.pc.toString(16).toUpperCase()}, cycles=${jsbeebState.cycles}`);

        // For Beebium, use Run() to boot quickly (stepping via gRPC is too slow)
        // Boot takes ~6M cycles at 2MHz = 3 seconds
        console.log('Running Beebium for 3.5 seconds to complete boot...');
        await client.run();
        await new Promise(resolve => setTimeout(resolve, 3500));
        await client.stop();

        const beebiumState = await client.getMachineState();
        console.log(`Beebium at PC=$${beebiumState.cpu.pc.toString(16).toUpperCase()}, cycles=${beebiumState.cycles}`);

        // Verify Beebium booted by checking screen
        const screenLine = await client.peekMemory(0x7C00 + 40, 16);
        const screenText = String.fromCharCode(...screenLine);
        console.log(`Beebium screen: "${screenText}"`);
        expect(screenText).toContain('BBC');

        // Note: States won't match exactly because:
        // 1. jsbeeb stopped at exact $8000 entry
        // 2. Beebium ran past boot and is now in keyboard polling loop
        // 3. Cycle counts will differ significantly

        // Compare states
        const result = await runner.compareState();

        console.log(`\nTotal divergences: ${result.divergences.length}`);

        // Group by component
        const byComponent: Record<string, typeof result.divergences> = {};
        for (const d of result.divergences) {
            if (!byComponent[d.component]) byComponent[d.component] = [];
            byComponent[d.component].push(d);
        }

        for (const [component, divs] of Object.entries(byComponent)) {
            console.log(`\n${component.toUpperCase()} (${divs.length} divergences):`);
            for (const d of divs) {
                const jsVal = typeof d.jsbeebValue === 'number'
                    ? `0x${d.jsbeebValue.toString(16).toUpperCase().padStart(2, '0')}`
                    : String(d.jsbeebValue);
                const beeVal = typeof d.beebiumValue === 'number'
                    ? `0x${d.beebiumValue.toString(16).toUpperCase().padStart(2, '0')}`
                    : String(d.beebiumValue);
                console.log(`  ${d.field}: jsbeeb=${jsVal} beebium=${beeVal}`);
            }
        }

        // Compare key zero page locations
        console.log('\n--- Key Zero Page Locations ---');
        const zpLocations = [
            { addr: 0x00, name: 'Language ROM' },
            { addr: 0x28, name: 'Current ROM' },
            { addr: 0xF4, name: 'ROM select copy' },
            { addr: 0xFE, name: 'BASIC text pointer low' },
            { addr: 0xFF, name: 'BASIC text pointer high' },
        ];

        for (const loc of zpLocations) {
            const jsVal = oracle.readMemory(loc.addr, 1)[0];
            const beeVal = (await client.peekMemory(loc.addr, 1))[0];
            const match = jsVal === beeVal ? '✓' : '✗';
            console.log(`  $${loc.addr.toString(16).toUpperCase().padStart(2, '0')} ${loc.name}: jsbeeb=0x${jsVal.toString(16).toUpperCase().padStart(2, '0')} beebium=0x${beeVal.toString(16).toUpperCase().padStart(2, '0')} ${match}`);
        }

        // Memory comparison
        console.log('\n--- Memory Comparison ---');
        const jsbeebZP = oracle.readMemory(0x0000, 256);
        const beebiumZP = await client.peekMemory(0x0000, 256);
        let zpDiffs = 0;
        for (let i = 0; i < 256; i++) {
            if (jsbeebZP[i] !== beebiumZP[i]) zpDiffs++;
        }
        console.log(`Zero page: ${zpDiffs}/256 bytes differ`);

        expect(true).toBe(true);
    }, 180000);  // 3 minutes - stepping is slow

    it('should synchronize at BASIC entry ($8000)', async () => {
        // Reset both emulators
        oracle.reset();
        await client.reset();

        console.log('\n=== Synchronized Comparison at BASIC Entry ($8000) ===\n');

        // Use DiffRunner to sync both at $8000
        console.log('Running both emulators to PC=$8000...');
        const result = await runner.runUntilAddress(0x8000);

        // Get states for reporting
        const jsbeebState = oracle.getState();
        const beebiumState = await client.getMachineState();

        console.log(`jsbeeb: PC=$${jsbeebState.cpu.pc.toString(16).toUpperCase()}, cycles=${jsbeebState.cycles}`);
        console.log(`Beebium: PC=$${beebiumState.cpu.pc.toString(16).toUpperCase()}, cycles=${beebiumState.cycles}`);
        console.log(`Cycle difference: ${Math.abs(jsbeebState.cycles - beebiumState.cycles)}`);

        // Report divergences
        const cpuDivs = result.divergences.filter(d => d.component === 'cpu');
        const viaDivs = result.divergences.filter(d => d.component.includes('via'));
        const otherDivs = result.divergences.filter(d => !d.component.includes('via') && d.component !== 'cpu');

        console.log(`\nDivergences: CPU=${cpuDivs.length}, VIA=${viaDivs.length}, Other=${otherDivs.length}`);

        if (cpuDivs.length === 0) {
            console.log('  CPU: MATCH!');
        } else {
            console.log('  CPU divergences:');
            for (const d of cpuDivs) {
                const jsVal = typeof d.jsbeebValue === 'number'
                    ? `0x${d.jsbeebValue.toString(16).toUpperCase().padStart(2, '0')}`
                    : String(d.jsbeebValue);
                const beeVal = typeof d.beebiumValue === 'number'
                    ? `0x${d.beebiumValue.toString(16).toUpperCase().padStart(2, '0')}`
                    : String(d.beebiumValue);
                console.log(`    ${d.field}: jsbeeb=${jsVal} beebium=${beeVal}`);
            }
        }

        // Compare zero page
        const jsbeebZP = oracle.readMemory(0, 256);
        const beebiumZP = await client.peekMemory(0, 256);
        let zpDiffs = 0;
        const zpDiffList: string[] = [];
        for (let i = 0; i < 256; i++) {
            if (jsbeebZP[i] !== beebiumZP[i]) {
                zpDiffs++;
                if (zpDiffList.length < 10) {
                    zpDiffList.push(`$${i.toString(16).toUpperCase().padStart(2, '0')}: jsbeeb=${jsbeebZP[i].toString(16).padStart(2, '0')} beebium=${beebiumZP[i].toString(16).padStart(2, '0')}`);
                }
            }
        }
        console.log(`\nZero page: ${zpDiffs}/256 bytes differ`);
        if (zpDiffs > 0 && zpDiffs <= 10) {
            for (const diff of zpDiffList) {
                console.log(`  ${diff}`);
            }
        } else if (zpDiffs > 10) {
            console.log(`  First 10: ${zpDiffList.join(', ')}`);
        }

        // This test documents current state - CPU should match at this sync point
        expect(cpuDivs.length).toBe(0);
    }, 60000);

    it('should compare ROM slot contents to diagnose cycle difference', async () => {
        // Reset both emulators
        oracle.reset();
        await client.reset();

        console.log('\n=== ROM Slot Header Comparison (Diagnosing Cycle Difference) ===\n');
        console.log('Checking what each emulator returns for unpopulated/empty ROM slots.');
        console.log('MOS enumeration behavior depends on ROM header bytes at $8006-$8008.\n');

        // For each slot, we need to select it and then read the header
        // We'll do this by directly reading memory at $8006-$8008 after selecting each bank
        //
        // In jsbeeb, we can access the raw ROM memory through the processor
        // In Beebium, we need to use the memory region API or select bank and read

        console.log('Slot | jsbeeb $8006-8008 | Beebium $8006-8008 | Notes');
        console.log('-----|-------------------|--------------------|-----------');

        // Read raw ROM banks from jsbeeb
        const jsbeebProcessor = (oracle as any).processor;

        for (let slot = 15; slot >= 0; slot--) {
            // Read jsbeeb ROM bank memory directly
            // jsbeeb stores ROMs at romOffset + slot * 16384
            const romOffset = jsbeebProcessor.romOffset;
            const bankBase = romOffset + slot * 16384;
            const js8006 = jsbeebProcessor.ramRomOs[bankBase + 0x06];
            const js8007 = jsbeebProcessor.ramRomOs[bankBase + 0x07];
            const js8008 = jsbeebProcessor.ramRomOs[bankBase + 0x08];

            // Read Beebium ROM bank memory via region API
            const regionName = `bank_${slot}`;
            const bee8006 = (await client.peekRegion(regionName, 0x8006))[0];
            const bee8007 = (await client.peekRegion(regionName, 0x8007))[0];
            const bee8008 = (await client.peekRegion(regionName, 0x8008))[0];

            // Determine if slot is populated
            const jsPopulated = js8006 !== 0 || js8007 !== 0 || js8008 !== 0;
            const beePopulated = bee8006 !== 0xFF || bee8007 !== 0xFF || bee8008 !== 0xFF;

            // Note about sideways RAM in jsbeeb
            const jsSwram = (jsbeebProcessor.model?.swram?.[slot]) ? 'swram' : '';

            const note = jsSwram ? 'jsbeeb:swram' :
                        (jsPopulated && beePopulated) ? 'ROM' :
                        (!jsPopulated && !beePopulated) ? 'empty' :
                        'MISMATCH';

            console.log(`  ${slot.toString().padStart(2)} | ${js8006.toString(16).padStart(2,'0')} ${js8007.toString(16).padStart(2,'0')} ${js8008.toString(16).padStart(2,'0')}           | ${bee8006.toString(16).padStart(2,'0')} ${bee8007.toString(16).padStart(2,'0')} ${bee8008.toString(16).padStart(2,'0')}              | ${note}`);
        }

        console.log('\nKey: $8006=ROM type, $8007=copyright offset, $8008=version');
        console.log('jsbeeb empty slots return 0x00 (Uint8Array default)');
        console.log('Beebium empty slots return 0xFF (open bus / empty socket)');
        console.log('\nThis difference affects MOS ROM enumeration cycle count.');

        expect(true).toBe(true);
    }, 30000);
});
