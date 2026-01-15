/**
 * jsbeeb Oracle Wrapper
 *
 * Wraps jsbeeb's TestMachine to provide a clean interface for state extraction
 * and synchronized execution during differential testing.
 */

import type { CpuState, ViaState, CrtcState, VideoUlaState, MachineState } from './types.js';

// jsbeeb imports - using relative path to sibling repo
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import { TestMachine } from '../../jsbeeb/tests/test-machine.js';
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import * as fdc from '../../jsbeeb/src/fdc.js';

export class JsbeebOracle {
    private machine: TestMachine | null = null;
    private cycleCount: number = 0;

    /**
     * Initialize the oracle with the specified BBC model.
     * @param model - Model name (default: "B-DFS1.2")
     */
    async initialize(model: string = "B-DFS1.2"): Promise<void> {
        this.machine = new TestMachine(model);
        await this.machine.initialise();
        this.cycleCount = 0;
    }

    /**
     * Load a disc image into the specified drive.
     * @param drive - Drive number (0 or 1)
     * @param path - Path to disc image file
     */
    async loadDisc(drive: number, path: string): Promise<void> {
        if (!this.machine) throw new Error("Machine not initialized");

        const data = await fdc.load(path);
        const disc = fdc.discFor(this.processor.fdc, "", data);
        this.processor.fdc.loadDisc(drive, disc);
    }

    /**
     * Reset the machine to initial state.
     */
    reset(): void {
        if (!this.machine) throw new Error("Machine not initialized");
        this.processor.reset(true);
        this.cycleCount = 0;
    }

    /**
     * Execute a number of CPU instructions.
     * Uses jsbeeb's debug hook to stop after exactly N instructions.
     * @param count - Number of instructions to execute (default: 1)
     * @returns Number of cycles executed
     */
    async stepInstructions(count: number = 1): Promise<number> {
        if (!this.machine) throw new Error("Machine not initialized");

        const startCycles = this.processor.currentCycles;
        let instructionsExecuted = 0;

        // Use jsbeeb's debug instruction hook to stop after N instructions
        const hook = this.processor.debugInstruction.add(() => {
            instructionsExecuted++;
            return instructionsExecuted >= count; // true = stop
        });

        try {
            // Run for enough cycles to execute the instructions
            // Each instruction takes at least 2 cycles, budget 20 per instruction
            await this.machine.runFor(count * 20);
        } finally {
            hook.remove();
        }

        const endCycles = this.processor.currentCycles;
        const executed = endCycles - startCycles;
        this.cycleCount += executed;
        return executed;
    }

    /**
     * Run for a specified number of cycles.
     * @param cycles - Number of cycles to execute
     */
    async runCycles(cycles: number): Promise<void> {
        if (!this.machine) throw new Error("Machine not initialized");
        await this.machine.runFor(cycles);
        this.cycleCount += cycles;
    }

    /**
     * Run until reaching a specific address.
     * @param address - Target address
     * @param timeoutSeconds - Timeout in seconds (default: 10)
     */
    async runUntilAddress(address: number, timeoutSeconds: number = 10): Promise<void> {
        if (!this.machine) throw new Error("Machine not initialized");
        await this.machine.runUntilAddress(address, timeoutSeconds);
    }

    /**
     * Get the underlying processor object for direct access.
     */
    private get processor(): any {
        if (!this.machine) throw new Error("Machine not initialized");
        return (this.machine as any).processor;
    }

    /**
     * Get current CPU register state.
     */
    getCpuState(): CpuState {
        const p = this.processor;
        return {
            a: p.a,
            x: p.x,
            y: p.y,
            sp: p.s,
            pc: p.pc,
            p: p.p.asByte(),
        };
    }

    /**
     * Get System VIA state.
     */
    getSystemViaState(): ViaState {
        return this.extractViaState(this.processor.sysvia);
    }

    /**
     * Get User VIA state.
     */
    getUserViaState(): ViaState {
        return this.extractViaState(this.processor.uservia);
    }

    /**
     * Extract VIA state from a jsbeeb VIA object.
     */
    private extractViaState(via: any): ViaState {
        return {
            ora: via.ora,
            orb: via.orb,
            ira: via.ira,
            irb: via.irb,
            ddra: via.ddra,
            ddrb: via.ddrb,
            t1c: via.t1c & 0xFFFF,
            t1l: via.t1l & 0xFFFF,
            t2c: via.t2c & 0xFFFF,
            t2l: via.t2l & 0xFFFF,
            acr: via.acr,
            pcr: via.pcr,
            ifr: via.ifr,
            ier: via.ier,
            sr: via.sr,
            ca1: via.ca1,
            ca2: via.ca2,
            cb1: via.cb1,
            cb2: via.cb2,
        };
    }

    /**
     * Get CRTC state.
     */
    getCrtcState(): CrtcState {
        const video = this.processor.video;
        return {
            registers: Array.from({ length: 18 }, (_, i) => video.regs[i] ?? 0),
            addressRegister: video.crtc?.curReg ?? 0,
        };
    }

    /**
     * Get Video ULA state.
     * Note: FakeVideo (used in headless mode) doesn't have full ULA state.
     */
    getVideoUlaState(): VideoUlaState {
        const video = this.processor.video;
        return {
            control: video.ulactrl ?? 0,
            // actualPal only exists on real Video, not FakeVideo
            palette: video.actualPal
                ? Array.from({ length: 16 }, (_, i) => video.actualPal[i] ?? 0)
                : new Array(16).fill(0),
        };
    }

    /**
     * Read memory without side effects.
     * @param address - Start address
     * @param length - Number of bytes to read
     */
    readMemory(address: number, length: number): Uint8Array {
        const data = new Uint8Array(length);
        for (let i = 0; i < length; i++) {
            data[i] = this.processor.readmem(address + i);
        }
        return data;
    }

    /**
     * Write to memory.
     * @param address - Start address
     * @param data - Data to write
     */
    writeMemory(address: number, data: Uint8Array): void {
        for (let i = 0; i < data.length; i++) {
            this.processor.writemem(address + i, data[i]);
        }
    }

    /**
     * Get complete machine state snapshot.
     */
    getState(): MachineState {
        return {
            cpu: this.getCpuState(),
            systemVia: this.getSystemViaState(),
            userVia: this.getUserViaState(),
            crtc: this.getCrtcState(),
            videoUla: this.getVideoUlaState(),
            cycles: this.cycleCount,
        };
    }

    /**
     * Get current cycle count.
     */
    getCycles(): number {
        return this.cycleCount;
    }
}
