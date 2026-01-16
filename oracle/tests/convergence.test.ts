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
        // - jsbeeb B1770: WD1770 controller, DFS 2.26, ADFS 1.30, sideways RAM in slots 0-7
        // - Beebium B-RomRam: 16-slot ROM/RAM board with identical layout
        //
        // B-RomRam model provides JsbeebStyle default layout:
        //   Slots 0-7: Sideways RAM
        //   Slots 8-12: Empty
        //   Slot 13: ADFS ROM
        //   Slot 14: DFS ROM
        //   Slot 15: BASIC ROM
        oracle = new JsbeebOracle();
        await oracle.initialize('B1770');

        // Use absolute paths to ROMs to avoid path resolution issues
        const romDir = '/Users/rjs/Code/beebium/roms';
        fixture = new ServerFixture({
            timeout: 10000,
            model: 'B-RomRam',
            fdc: 'acorn-1770',
            sideways: [
                // Default JsbeebStyle already provides slots 0-7 as RAM
                // Override slot 13 and 14 with our ROMs (same as jsbeeb)
                { slot: 13, type: 'rom', filepath: `${romDir}/acorn-adfs_1_30.rom` },
                { slot: 14, type: 'rom', filepath: `${romDir}/acorn-dfs_2_26.rom` },
                // Slot 15 has BASIC by default
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

    describe('Cycle-by-cycle bisection', () => {
        it('finds where cycle difference accumulates (using reusable tool)', async () => {
            // Reset both emulators
            oracle.reset();
            await client.reset();

            console.log('\n=== Cycle Difference Accumulation Analysis ===\n');
            console.log('Using DiffRunner.findCycleDifferenceChanges() to track cycle drift...\n');

            // Use the reusable bisection tool
            const changes = await runner.findCycleDifferenceChanges(
                600_000,
                (instruction, cycleDiff) => {
                    console.log(`  ${instruction.toLocaleString()} instructions, cycle diff: ${cycleDiff}`);
                }
            );

            // Report findings
            const jsbeebFinal = oracle.getState();
            const beebiumFinal = await client.getMachineState();

            console.log(`\nFinal state:`);
            console.log(`  jsbeeb: PC=$${jsbeebFinal.cpu.pc.toString(16).toUpperCase()}, cycles=${jsbeebFinal.cycles}`);
            console.log(`  beebium: PC=$${beebiumFinal.cpu.pc.toString(16).toUpperCase()}, cycles=${beebiumFinal.cycles}`);
            console.log(`  Total cycle difference: ${beebiumFinal.cycles - jsbeebFinal.cycles}`);

            if (changes.length > 0) {
                console.log(`\n${changes.length} point(s) where cycle difference changed:`);
                for (const change of changes) {
                    console.log(`  At ~${change.approximateInstruction.toLocaleString()}: ${change.previousDiff} -> ${change.newDiff} (delta: ${change.delta})`);
                    console.log(`    jsbeeb PC=$${change.jsbeebPc.toString(16).toUpperCase()}, beebium PC=$${change.beebiumPc.toString(16).toUpperCase()}`);
                }
            } else {
                console.log('\nCycle difference was constant throughout boot!');
                console.log('This means the difference comes from initial state, not execution divergence.');
            }

            expect(true).toBe(true);
        }, 300000);

        it('finds first PC divergence (using reusable tool)', async () => {
            // Reset both emulators
            oracle.reset();
            await client.reset();

            console.log('\n=== Fine-Grained PC Divergence Search ===\n');
            console.log('Using DiffRunner.findFirstPcDivergence() to find where PCs first differ...\n');

            // Use the reusable bisection tool
            const divergence = await runner.findFirstPcDivergence(
                50_000,
                (instruction, pc, cycleDiff) => {
                    console.log(`  ${instruction.toLocaleString()} instructions, PC=$${pc.toString(16).toUpperCase()}, cycle diff=${cycleDiff}`);
                }
            );

            if (divergence) {
                console.log(`\nPC DIVERGENCE at instruction ${divergence.instruction}:`);
                console.log(`  Before step:`);
                console.log(`    jsbeeb: PC=$${divergence.jsbeebBefore.pc.toString(16).toUpperCase().padStart(4,'0')} A=$${divergence.jsbeebBefore.a.toString(16).padStart(2,'0')} X=$${divergence.jsbeebBefore.x.toString(16).padStart(2,'0')} Y=$${divergence.jsbeebBefore.y.toString(16).padStart(2,'0')} P=$${divergence.jsbeebBefore.p.toString(16).padStart(2,'0')}`);
                console.log(`    beebium: PC=$${divergence.beebiumBefore.pc.toString(16).toUpperCase().padStart(4,'0')} A=$${divergence.beebiumBefore.a.toString(16).padStart(2,'0')} X=$${divergence.beebiumBefore.x.toString(16).padStart(2,'0')} Y=$${divergence.beebiumBefore.y.toString(16).padStart(2,'0')} P=$${divergence.beebiumBefore.p.toString(16).padStart(2,'0')}`);
                console.log(`  After step:`);
                console.log(`    jsbeeb: PC=$${divergence.jsbeebAfter.pc.toString(16).toUpperCase().padStart(4,'0')} A=$${divergence.jsbeebAfter.a.toString(16).padStart(2,'0')} X=$${divergence.jsbeebAfter.x.toString(16).padStart(2,'0')} Y=$${divergence.jsbeebAfter.y.toString(16).padStart(2,'0')} P=$${divergence.jsbeebAfter.p.toString(16).padStart(2,'0')}`);
                console.log(`    beebium: PC=$${divergence.beebiumAfter.pc.toString(16).toUpperCase().padStart(4,'0')} A=$${divergence.beebiumAfter.a.toString(16).padStart(2,'0')} X=$${divergence.beebiumAfter.x.toString(16).padStart(2,'0')} Y=$${divergence.beebiumAfter.y.toString(16).padStart(2,'0')} P=$${divergence.beebiumAfter.p.toString(16).padStart(2,'0')}`);

                // Show memory at divergence point
                console.log(`  Memory at PC=$${divergence.jsbeebBefore.pc.toString(16).toUpperCase()}: ${Array.from(divergence.memoryAtPc).map(b => b.toString(16).padStart(2,'0')).join(' ')}`);

                // Decode flags
                const jsN = (divergence.jsbeebAfter.p & 0x80) ? 1 : 0;
                const jsV = (divergence.jsbeebAfter.p & 0x40) ? 1 : 0;
                const jsZ = (divergence.jsbeebAfter.p & 0x02) ? 1 : 0;
                const jsC = (divergence.jsbeebAfter.p & 0x01) ? 1 : 0;
                const beeN = (divergence.beebiumAfter.p & 0x80) ? 1 : 0;
                const beeV = (divergence.beebiumAfter.p & 0x40) ? 1 : 0;
                const beeZ = (divergence.beebiumAfter.p & 0x02) ? 1 : 0;
                const beeC = (divergence.beebiumAfter.p & 0x01) ? 1 : 0;
                console.log(`  Flags: jsbeeb: N=${jsN} V=${jsV} Z=${jsZ} C=${jsC}`);
                console.log(`         beebium: N=${beeN} V=${beeV} Z=${beeZ} C=${beeC}`);
                console.log(`  Cycle difference: ${divergence.cycleDiff}`);
            } else {
                console.log(`\nNo PC divergence found in first 50,000 instructions.`);
                console.log('Both emulators execute the same code path.');
                console.log('Cycle difference must come from cycle counting, not branching.');
            }

            expect(true).toBe(true);
        }, 600000);  // 10 minute timeout for instruction-by-instruction stepping

        it('finds divergence after synchronizing initial state', async () => {
            // Reset both emulators
            oracle.reset();
            await client.reset();

            console.log('\n=== PC Divergence Search After Synchronization ===\n');
            console.log('Using DiffRunner.synchronizeAfterReset() to align initial state...\n');

            // Synchronize both emulators past initialization differences
            let initialCycleOffset = 0;
            try {
                const sync = await runner.synchronizeAfterReset();
                console.log(`Synchronized at PC=$${sync.syncPc.toString(16).toUpperCase()}`);
                console.log(`  jsbeeb cycles: ${sync.jsbeebCycles}`);
                console.log(`  beebium cycles: ${sync.beebiumCycles}`);
                initialCycleOffset = sync.beebiumCycles - sync.jsbeebCycles;
                console.log(`  Initial cycle offset: ${initialCycleOffset}\n`);
            } catch (e) {
                console.log(`Synchronization failed: ${e}`);
                console.log('Continuing with unsynchronized state...\n');
            }

            // Search for PC divergence through the full boot sequence (700K instructions)
            const maxInstructions = 700_000;
            const divergence = await runner.findFirstPcDivergence(
                maxInstructions,
                (instruction, pc, cycleDiff) => {
                    if (instruction % 50000 === 0) {
                        const adjustedDiff = cycleDiff - initialCycleOffset;
                        console.log(`  ${instruction.toLocaleString()} instructions, PC=$${pc.toString(16).toUpperCase()}, cycle diff=${cycleDiff} (adjusted: ${adjustedDiff})`);
                    }
                }
            );

            if (divergence) {
                console.log(`\nPC DIVERGENCE at instruction ${divergence.instruction}:`);
                console.log(`  Before: jsbeeb PC=$${divergence.jsbeebBefore.pc.toString(16).toUpperCase()} beebium PC=$${divergence.beebiumBefore.pc.toString(16).toUpperCase()}`);
                console.log(`  After:  jsbeeb PC=$${divergence.jsbeebAfter.pc.toString(16).toUpperCase()} beebium PC=$${divergence.beebiumAfter.pc.toString(16).toUpperCase()}`);
                console.log(`  Memory: ${Array.from(divergence.memoryAtPc).map(b => b.toString(16).padStart(2,'0')).join(' ')}`);
                console.log(`  Cycle diff: ${divergence.cycleDiff} (adjusted: ${divergence.cycleDiff - initialCycleOffset})`);
            } else {
                const jsbeebFinal = oracle.getState();
                const beebiumFinal = await client.getMachineState();
                const finalDiff = beebiumFinal.cycles - jsbeebFinal.cycles;
                const adjustedDiff = finalDiff - initialCycleOffset;

                console.log(`\nNo PC divergence found in ${maxInstructions.toLocaleString()} instructions after sync.`);
                console.log('Both emulators execute identical code paths!');
                console.log(`\nFinal state:`);
                console.log(`  jsbeeb: PC=$${jsbeebFinal.cpu.pc.toString(16).toUpperCase()}, cycles=${jsbeebFinal.cycles}`);
                console.log(`  beebium: PC=$${beebiumFinal.cpu.pc.toString(16).toUpperCase()}, cycles=${beebiumFinal.cycles}`);
                console.log(`  Total cycle difference: ${finalDiff} (adjusted for reset: ${adjustedDiff})`);
            }

            expect(true).toBe(true);
        }, 1200000);  // 20 minute timeout for full boot

        it('captures VIA state at divergence point ($DD13)', async () => {
            // Reset both emulators
            oracle.reset();
            await client.reset();

            console.log('\n=== VIA State at Divergence Point ===\n');

            // Synchronize first
            const sync = await runner.synchronizeAfterReset();
            console.log(`Synchronized at PC=$${sync.syncPc.toString(16).toUpperCase()}`);

            // Step to just before the divergence (177,441 instructions after sync)
            // The divergence is at instruction 177,442
            const targetInstruction = 177440;
            console.log(`Stepping to instruction ${targetInstruction}...`);

            await runner.stepBoth(targetInstruction);

            const jsbeebState = oracle.getState();
            const beebiumState = await client.getMachineState();

            console.log(`\nAt instruction ${targetInstruction}:`);
            console.log(`  jsbeeb PC=$${jsbeebState.cpu.pc.toString(16).toUpperCase()}`);
            console.log(`  beebium PC=$${beebiumState.cpu.pc.toString(16).toUpperCase()}`);

            // Step one more instruction to get to $DD13
            await runner.stepBoth(1);
            const js1 = oracle.getState();
            const bee1 = await client.getMachineState();
            console.log(`\nAfter step 1: jsbeeb PC=$${js1.cpu.pc.toString(16).toUpperCase()}, beebium PC=$${bee1.cpu.pc.toString(16).toUpperCase()}`);

            // If we're at $DD13, capture VIA state before executing the BCC
            if (js1.cpu.pc === 0xDD13 || bee1.cpu.pc === 0xDD13) {
                console.log('\n=== VIA State Just Before BCC at $DD13 ===\n');

                // Read VIA registers directly
                // System VIA IFR at $FE4D
                const jsIFR = oracle.readMemory(0xFE4D, 1)[0];
                const beeIFR = (await client.peekMemory(0xFE4D, 1))[0];
                console.log(`System VIA IFR ($FE4D):`);
                console.log(`  jsbeeb:  0x${jsIFR.toString(16).padStart(2, '0')} = ${jsIFR.toString(2).padStart(8, '0')}`);
                console.log(`  beebium: 0x${beeIFR.toString(16).padStart(2, '0')} = ${beeIFR.toString(2).padStart(8, '0')}`);

                // System VIA IER at $FE4E
                const jsIER = oracle.readMemory(0xFE4E, 1)[0];
                const beeIER = (await client.peekMemory(0xFE4E, 1))[0];
                console.log(`\nSystem VIA IER ($FE4E):`);
                console.log(`  jsbeeb:  0x${jsIER.toString(16).padStart(2, '0')} = ${jsIER.toString(2).padStart(8, '0')}`);
                console.log(`  beebium: 0x${beeIER.toString(16).padStart(2, '0')} = ${beeIER.toString(2).padStart(8, '0')}`);

                // MOS workspace at $0279 (used in the AND mask)
                const jsMask = oracle.readMemory(0x0279, 1)[0];
                const beeMask = (await client.peekMemory(0x0279, 1))[0];
                console.log(`\nMOS mask ($0279):`);
                console.log(`  jsbeeb:  0x${jsMask.toString(16).padStart(2, '0')}`);
                console.log(`  beebium: 0x${beeMask.toString(16).padStart(2, '0')}`);

                // Calculate what the code computes
                const jsMasked = jsIFR & jsMask & jsIER;
                const beeMasked = beeIFR & beeMask & beeIER;
                console.log(`\nMasked result (IFR & $0279 & IER):`);
                console.log(`  jsbeeb:  0x${jsMasked.toString(16).padStart(2, '0')} = ${jsMasked.toString(2).padStart(8, '0')}`);
                console.log(`  beebium: 0x${beeMasked.toString(16).padStart(2, '0')} = ${beeMasked.toString(2).padStart(8, '0')}`);

                // After two RORs, bit 1 of masked result ends up in Carry
                const jsCarry = (jsMasked >> 1) & 1;
                const beeCarry = (beeMasked >> 1) & 1;
                console.log(`\nAfter 2x ROR, Carry would be (bit 1 of masked):`);
                console.log(`  jsbeeb:  ${jsCarry} (BCC ${jsCarry ? 'not taken' : 'TAKEN'})`);
                console.log(`  beebium: ${beeCarry} (BCC ${beeCarry ? 'not taken' : 'TAKEN'})`);

                // Decode IFR bits
                // BBC Micro System VIA control line assignments:
                //   CA1 = vertical sync (active low)
                //   CA2 = keyboard (directly from keyboard matrix)
                //   CB1 = A/D conversion complete
                //   CB2 = light pen strobe
                console.log('\n=== IFR Bit Meanings ===');
                const ifrBits = [
                    'CA2 interrupt (keyboard)',
                    'CA1 interrupt (vertical sync)',
                    'Shift register',
                    'CB2 interrupt (light pen)',
                    'CB1 interrupt (A/D complete)',
                    'Timer 2',
                    'Timer 1',
                    'IRQ active'
                ];
                for (let i = 0; i < 8; i++) {
                    const jsb = (jsIFR >> i) & 1;
                    const beeb = (beeIFR >> i) & 1;
                    const match = jsb === beeb ? '' : ' <-- DIFFERS!';
                    console.log(`  Bit ${i} (${ifrBits[i]}): jsbeeb=${jsb} beebium=${beeb}${match}`);
                }
            } else {
                console.log('\nNot at $DD13 - may need to adjust instruction count');
                console.log('Stepping a few more to find divergence...');

                // Step a few more
                for (let i = 0; i < 5; i++) {
                    const jsBefore = oracle.getCpuState();
                    const beeBefore = await client.getCpuState();
                    await runner.stepBoth(1);
                    const jsAfter = oracle.getCpuState();
                    const beeAfter = await client.getCpuState();

                    console.log(`  Step ${i + 1}: jsbeeb $${jsBefore.pc.toString(16).toUpperCase()}->${jsAfter.pc.toString(16).toUpperCase()}, beebium $${beeBefore.pc.toString(16).toUpperCase()}->${beeAfter.pc.toString(16).toUpperCase()}`);

                    if (jsAfter.pc !== beeAfter.pc) {
                        console.log(`  ^ PC DIVERGENCE detected!`);
                        break;
                    }
                }
            }

            expect(true).toBe(true);
        }, 600000);
    });
});
