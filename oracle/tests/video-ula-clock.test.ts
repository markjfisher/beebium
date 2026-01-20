/**
 * Video ULA Clock Rate Diagnostic Test
 *
 * This test quantifies the impact of the Video ULA clock rate difference
 * between jsbeeb (2MHz at reset) and beebium (1MHz at reset).
 *
 * The test measures when the first CA1 interrupt (vsync end) occurs in
 * both emulators, with and without clock rate alignment.
 *
 * See CYCLE_DIFFERENCE_INVESTIGATION.md for full analysis.
 */

import { describe, it, expect, beforeAll, afterAll, beforeEach } from 'vitest';
import { JsbeebOracle, BeebiumClient, ServerFixture } from '../src/index.js';

/**
 * Find the first instruction where CA1 interrupt flag is set in the IFR.
 * CA1 on the System VIA signals the end of vsync.
 *
 * @param stepFn - Function to step one instruction, returns cycles executed
 * @param getViaStateFn - Function to get System VIA state
 * @param maxInstructions - Maximum instructions to search
 * @returns Object with instruction number and cycle count where CA1 was found, or null
 */
async function findFirstCa1Interrupt(
    stepFn: () => Promise<number>,
    getViaStateFn: () => Promise<{ ifr: number; ca1: boolean }>,
    maxInstructions: number = 25000
): Promise<{ instruction: number; cycles: number } | null> {
    let totalCycles = 0;

    for (let i = 0; i < maxInstructions; i++) {
        const cycles = await stepFn();
        totalCycles += cycles;

        const viaState = await getViaStateFn();
        // CA1 interrupt flag is bit 1 of IFR
        if (viaState.ifr & 0x02) {
            return { instruction: i + 1, cycles: totalCycles };
        }
    }

    return null;
}

describe('Video ULA Clock Rate Diagnostic', () => {
    let fixture: ServerFixture;
    let client: BeebiumClient;
    let oracle: JsbeebOracle;

    beforeAll(async () => {
        // Initialize jsbeeb oracle with real video (2MHz clock by default)
        oracle = new JsbeebOracle();
        await oracle.initialize('B-DFS1.2', true);

        // Start beebium server
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

    beforeEach(async () => {
        // Reset both emulators before each test
        oracle.reset();
        await client.reset();
    });

    it('should report jsbeeb Video ULA state at reset (expect 2MHz)', () => {
        const videoUlaState = oracle.getVideoUlaState();
        const timingState = oracle.getVideoTimingState();

        console.log('\n=== jsbeeb Video State at Reset ===');
        console.log(`  ULA control: 0x${videoUlaState.control.toString(16).padStart(2, '0')}`);
        console.log(`  Bit 4 (fast clock): ${(videoUlaState.control & 0x10) ? '1 (2MHz)' : '0 (1MHz)'}`);

        // jsbeeb initializes halfClock = false in constructor, meaning 2MHz
        // But this is controlled by the video object directly, not ulactrl at init
        // Check the actual video timing to verify clock rate
        console.log(`  inVsync: ${timingState.inVsync}`);
        console.log(`  vpulseWidth: ${timingState.vpulseWidth}`);
    });

    it('should report beebium Video ULA state at reset (expect 1MHz)', async () => {
        const videoUlaState = await client.getVideoUlaState();

        console.log('\n=== Beebium Video State at Reset ===');
        console.log(`  ULA control: 0x${videoUlaState.control.toString(16).padStart(2, '0')}`);
        console.log(`  Bit 4 (fast clock): ${(videoUlaState.control & 0x10) ? '1 (2MHz)' : '0 (1MHz)'}`);

        // Verify beebium starts at 1MHz (control = 0)
        expect(videoUlaState.control & 0x10).toBe(0);
    });

    it('should measure first CA1 interrupt in jsbeeb (default 2MHz)', async () => {
        console.log('\n=== jsbeeb CA1 Detection (2MHz clock) ===');

        const result = await findFirstCa1Interrupt(
            () => oracle.stepInstructions(1),
            async () => oracle.getSystemViaState(),
            25000
        );

        if (result) {
            console.log(`  First CA1 interrupt at instruction ${result.instruction}, cycle ${result.cycles}`);
        } else {
            console.log('  No CA1 interrupt detected in 25000 instructions');
        }

        // Store result for comparison
        (globalThis as any).jsbeeb2MHzResult = result;
    });

    it('should measure first CA1 interrupt in jsbeeb (aligned to 1MHz)', async () => {
        console.log('\n=== jsbeeb CA1 Detection (1MHz clock - aligned) ===');

        // Access the video object and set halfClock = true for 1MHz mode
        const processor = (oracle as any).processor;
        if (processor?.video) {
            processor.video.halfClock = true;
            processor.video.pixelsPerChar = 16;
            console.log('  Set jsbeeb to 1MHz mode (halfClock = true)');
        } else {
            console.log('  Warning: Could not access video object to set clock rate');
        }

        const result = await findFirstCa1Interrupt(
            () => oracle.stepInstructions(1),
            async () => oracle.getSystemViaState(),
            25000
        );

        if (result) {
            console.log(`  First CA1 interrupt at instruction ${result.instruction}, cycle ${result.cycles}`);
        } else {
            console.log('  No CA1 interrupt detected in 25000 instructions');
        }

        // Store result for comparison
        (globalThis as any).jsbeeb1MHzResult = result;
    });

    it('should measure first CA1 interrupt in beebium (default 1MHz)', async () => {
        console.log('\n=== Beebium CA1 Detection (1MHz clock) ===');

        const result = await findFirstCa1Interrupt(
            async () => {
                const stepResult = await client.stepInstruction(1);
                return stepResult.cyclesExecuted;
            },
            async () => client.getSystemViaState(),
            25000
        );

        if (result) {
            console.log(`  First CA1 interrupt at instruction ${result.instruction}, cycle ${result.cycles}`);
        } else {
            console.log('  No CA1 interrupt detected in 25000 instructions');
        }

        // Store result for comparison
        (globalThis as any).beebiumResult = result;
    });

    it('should summarize timing comparison', async () => {
        const jsbeeb2MHz = (globalThis as any).jsbeeb2MHzResult;
        const jsbeeb1MHz = (globalThis as any).jsbeeb1MHzResult;
        const beebium = (globalThis as any).beebiumResult;

        console.log('\n=== Summary ===');
        console.log('First CA1 Interrupt (vsync end) detection:');
        console.log('');

        if (jsbeeb2MHz) {
            console.log(`  jsbeeb (2MHz): instruction ${jsbeeb2MHz.instruction}, cycle ${jsbeeb2MHz.cycles}`);
        } else {
            console.log('  jsbeeb (2MHz): Not detected');
        }

        if (jsbeeb1MHz) {
            console.log(`  jsbeeb (1MHz): instruction ${jsbeeb1MHz.instruction}, cycle ${jsbeeb1MHz.cycles}`);
        } else {
            console.log('  jsbeeb (1MHz): Not detected');
        }

        if (beebium) {
            console.log(`  beebium (1MHz): instruction ${beebium.instruction}, cycle ${beebium.cycles}`);
        } else {
            console.log('  beebium (1MHz): Not detected');
        }

        console.log('');

        // Analysis
        if (jsbeeb2MHz && beebium) {
            const diff = Math.abs(jsbeeb2MHz.cycles - beebium.cycles);
            console.log(`  jsbeeb(2MHz) vs beebium: ${diff} cycle difference`);
        }

        if (jsbeeb1MHz && beebium) {
            const diff = Math.abs(jsbeeb1MHz.cycles - beebium.cycles);
            console.log(`  jsbeeb(1MHz) vs beebium: ${diff} cycle difference`);
            if (diff < 50) {
                console.log('  -> 1MHz alignment significantly reduces timing difference');
            }
        }

        if (jsbeeb2MHz && jsbeeb1MHz) {
            const diff = Math.abs(jsbeeb2MHz.cycles - jsbeeb1MHz.cycles);
            console.log(`  jsbeeb 2MHz vs 1MHz: ${diff} cycle difference`);
        }

        console.log('');
        console.log('See CYCLE_DIFFERENCE_INVESTIGATION.md for alignment options.');
    });
});
