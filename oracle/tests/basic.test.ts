/**
 * Basic differential tests between jsbeeb and Beebium.
 */

import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { JsbeebOracle, BeebiumClient, DiffRunner, ServerFixture } from '../src/index.js';

describe('JsbeebOracle', () => {
    let oracle: JsbeebOracle;

    beforeAll(async () => {
        oracle = new JsbeebOracle();
        await oracle.initialize('B-DFS1.2');
    });

    it('should initialize and get CPU state', () => {
        const cpu = oracle.getCpuState();
        expect(cpu).toBeDefined();
        expect(typeof cpu.a).toBe('number');
        expect(typeof cpu.x).toBe('number');
        expect(typeof cpu.y).toBe('number');
        expect(typeof cpu.sp).toBe('number');
        expect(typeof cpu.pc).toBe('number');
        expect(typeof cpu.p).toBe('number');
    });

    it('should get VIA state', () => {
        const systemVia = oracle.getSystemViaState();
        expect(systemVia).toBeDefined();
        expect(typeof systemVia.ora).toBe('number');
        expect(typeof systemVia.ifr).toBe('number');

        const userVia = oracle.getUserViaState();
        expect(userVia).toBeDefined();
    });

    it('should get CRTC state', () => {
        const crtc = oracle.getCrtcState();
        expect(crtc).toBeDefined();
        expect(Array.isArray(crtc.registers)).toBe(true);
        expect(crtc.registers.length).toBe(18);
    });

    it('should get Video ULA state', () => {
        const ula = oracle.getVideoUlaState();
        expect(ula).toBeDefined();
        expect(typeof ula.control).toBe('number');
        expect(Array.isArray(ula.palette)).toBe(true);
        expect(ula.palette.length).toBe(16);
    });

    it('should read memory', () => {
        const mem = oracle.readMemory(0x8000, 16);
        expect(mem).toBeInstanceOf(Uint8Array);
        expect(mem.length).toBe(16);
    });

    it('should step instructions', async () => {
        const startCpu = oracle.getCpuState();
        const cycles = await oracle.stepInstructions(10);
        const endCpu = oracle.getCpuState();

        expect(cycles).toBeGreaterThan(0);
        // PC should have changed after stepping
        expect(endCpu.pc).not.toBe(startCpu.pc);
    });
});

describe('BeebiumClient with ServerFixture', () => {
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
        try {
            client = await fixture.start();
        } catch (err) {
            console.log('Failed to start Beebium server:', err);
            throw err;
        }
    }, 15000);

    afterAll(async () => {
        await fixture.stop();
    });

    it('should get execution state', async () => {
        const state = await client.getState();
        expect(state).toBeDefined();
        expect(typeof state.isRunning).toBe('boolean');
    });

    it('should get CPU state', async () => {
        const cpu = await client.getCpuState();
        expect(cpu).toBeDefined();
        expect(typeof cpu.a).toBe('number');
        expect(typeof cpu.x).toBe('number');
        expect(typeof cpu.y).toBe('number');
        expect(typeof cpu.sp).toBe('number');
        expect(typeof cpu.pc).toBe('number');
        expect(typeof cpu.p).toBe('number');
    });

    it('should get VIA state', async () => {
        const systemVia = await client.getSystemViaState();
        expect(systemVia).toBeDefined();
        expect(typeof systemVia.ora).toBe('number');
        expect(typeof systemVia.ifr).toBe('number');
    });

    it('should get CRTC state', async () => {
        const crtc = await client.getCrtcState();
        expect(crtc).toBeDefined();
        expect(Array.isArray(crtc.registers)).toBe(true);
    });

    it('should step instruction', async () => {
        const result = await client.stepInstruction(10);

        // Verify step succeeded and executed the requested instructions
        expect(result.success).toBe(true);
        expect(result.instructionsExecuted).toBe(10);
        expect(result.cyclesExecuted).toBeGreaterThan(0);
    });

    it('should read memory', async () => {
        const mem = await client.peekMemory(0x8000, 16);
        expect(mem).toBeInstanceOf(Uint8Array);
        expect(mem.length).toBe(16);
    });
});

describe('DiffRunner with ServerFixture', () => {
    let fixture: ServerFixture;
    let oracle: JsbeebOracle;
    let client: BeebiumClient;
    let runner: DiffRunner;

    beforeAll(async () => {
        // Initialize jsbeeb oracle
        oracle = new JsbeebOracle();
        await oracle.initialize('B-DFS1.2');

        // Start Beebium server with matching ROM config
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

        runner = new DiffRunner(oracle, client);
    }, 15000);

    afterAll(async () => {
        await fixture.stop();
    });

    it('should compare initial state', async () => {
        // Reset both emulators
        // Note: jsbeeb sets PC directly to reset vector (0xD9CD), cycles=0
        // Beebium runs 7-cycle reset sequence, PC=0xD9CE (after opcode fetch), cycles=7
        // This is an expected divergence due to different reset implementations.
        oracle.reset();
        await client.reset();

        const result = await runner.compareState();
        expect(result).toBeDefined();
        expect(Array.isArray(result.divergences)).toBe(true);

        if (!result.match) {
            console.log('Initial state divergences:');
            console.log(DiffRunner.formatResult(result));
        }
    });

    it('should step both emulators', async () => {
        // Reset both emulators
        oracle.reset();
        await client.reset();

        // Step a few instructions
        const result = await runner.stepBoth(10);
        expect(result).toBeDefined();

        if (!result.match) {
            console.log('Post-step divergences:');
            console.log(DiffRunner.formatResult(result));
        }
    });
});
