/**
 * jsbeeb Oracle Wrapper
 *
 * Wraps jsbeeb's TestMachine to provide a clean interface for state extraction
 * and synchronized execution during differential testing.
 */

import type { CpuState, ViaState, CrtcState, VideoUlaState, MachineState, VideoTimingState } from './types.js';
import { P_STATUS_MASK } from './types.js';

// jsbeeb imports - using relative path to sibling repo (jsbeeb as sibling of beebium)
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import { TestMachine } from '../../../jsbeeb/tests/test-machine.js';
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import * as fdc from '../../../jsbeeb/src/fdc.js';
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import { Video } from '../../../jsbeeb/src/video.js';
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import { fake6502 } from '../../../jsbeeb/src/fake6502.js';
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import { findModel } from '../../../jsbeeb/src/models.js';
// @ts-expect-error - jsbeeb doesn't have TypeScript definitions
import * as utils from '../../../jsbeeb/src/utils.js';

export interface JsbeebOracleOptions {
    /** Use real Video class instead of FakeVideo for accurate vsync timing */
    useRealVideo?: boolean;
    /** Enable 65C02 Tube second processor */
    tube?: boolean;
}

export class JsbeebOracle {
    private machine: TestMachine | null = null;
    private cycleCount: number = 0;
    private realVideo: boolean = false;
    private tubeEnabled: boolean = false;

    /**
     * Initialize the oracle with the specified BBC model.
     * @param model - Model name (default: "B-DFS1.2")
     * @param options - Configuration options, or boolean for backwards compat (useRealVideo)
     */
    async initialize(model: string = "B-DFS1.2", options: JsbeebOracleOptions | boolean = {}): Promise<void> {
        // Backwards compatibility: second arg was previously a boolean (useRealVideo)
        if (typeof options === 'boolean') {
            options = { useRealVideo: options };
        }
        const useRealVideo = options.useRealVideo ?? true;
        this.tubeEnabled = options.tube ?? false;
        this.realVideo = useRealVideo;

        if (useRealVideo) {
            // Create real Video instance with dummy framebuffer for headless operation
            // Video needs: isMaster, fb32 (framebuffer), paint_ext (optional callback)
            const modelInfo = findModel(model);
            const isMaster = modelInfo.isMaster || false;

            // Create a dummy 1024x625 framebuffer (standard BBC output size)
            const fb32 = new Uint32Array(1024 * 625);

            // Create Video with no-op paint callback (headless mode)
            const video = new Video(isMaster, fb32, () => {});

            // Create processor with real video
            // Use fake6502 with video option to create a custom Cpu6502
            const processor = fake6502(modelInfo, { video, tube: this.tubeEnabled });

            // Create a TestMachine-like wrapper
            this.machine = {
                processor,
                async initialise() {
                    await processor.initialise();
                },
                async runFor(cycles: number) {
                    let left = cycles;
                    while (left > 0) {
                        const todo = Math.min(left, 100000);
                        processor.execute(todo);
                        left -= todo;
                    }
                },
                async runUntilAddress(targetAddr: number, secs: number = 120) {
                    let hit = false;
                    const hook = processor.debugInstruction.add((addr: number) => {
                        if (addr === targetAddr) {
                            hit = true;
                            return true;
                        }
                    });
                    // Run for up to secs seconds (2MHz = 2M cycles/sec)
                    let left = secs * 2 * 1000 * 1000;
                    while (left > 0 && !hit) {
                        const todo = Math.min(left, 100000);
                        processor.execute(todo);
                        left -= todo;
                    }
                    hook.remove();
                    if (!hit) {
                        throw new Error(`did not hit address $${targetAddr.toString(16).toUpperCase()} in ${secs} seconds`);
                    }
                },
            } as TestMachine;

            await this.machine.initialise();
        } else {
            // Use standard TestMachine with FakeVideo
            if (this.tubeEnabled) {
                throw new Error("Tube requires useRealVideo: true (TestMachine does not support Tube)");
            }
            this.machine = new TestMachine(model);
            await this.machine.initialise();
        }

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
     *
     * Note: jsbeeb's reset() has a bug - it doesn't simulate the 6502's 7-cycle reset
     * sequence which performs three "fake" stack pushes (reads instead of writes) that
     * decrement SP. On real hardware and in Beebium, SP ends up at 0xFD after reset
     * (0x00 → 0xFF → 0xFE → 0xFD). jsbeeb leaves SP at 0x00.
     *
     * We correct SP here to ensure stack operations use the same memory addresses in
     * both emulators, enabling meaningful memory comparison during differential testing.
     *
     * See: https://www.pagetable.com/?p=410 for details on the 6502 reset sequence.
     */
    reset(): void {
        if (!this.machine) throw new Error("Machine not initialized");
        this.processor.reset(true);

        // Fix jsbeeb's incorrect SP initialization (see comment above)
        this.processor.s = 0xFD;

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
        const startCycles = this.processor.currentCycles;
        await this.machine.runUntilAddress(address, timeoutSeconds);
        const endCycles = this.processor.currentCycles;
        this.cycleCount += endCycles - startCycles;
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
     *
     * Values are normalized for comparison with Beebium:
     * - P has bits 4-5 masked out (these don't represent real CPU state)
     */
    getCpuState(): CpuState {
        const p = this.processor;
        return {
            a: p.a,
            x: p.x,
            y: p.y,
            sp: p.s,
            pc: p.pc,
            p: p.p.asByte() & P_STATUS_MASK,  // Normalize: exclude bits 4-5
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
     *
     * Timer value normalization:
     *
     * jsbeeb stores timers in a "doubled" internal format (via.js:71 initializes
     * to 0x1FFFE). The internal counter decrements by 2 per 1MHz tick. When the
     * CPU reads the timer, jsbeeb converts via: ((t1c + 1) >>> 1) & 0xFF.
     *
     * Beebium stores timers as 16-bit values and reports the "effective" value
     * via effective_t1(), which is (t1 - 1) - predicting what the counter will
     * be after the current cycle's decrement at PHI2 trailing edge.
     *
     * To normalize for comparison, we extract jsbeeb's internal value masked to
     * 16 bits, then subtract 1 to match Beebium's effective value semantic:
     *
     *   jsbeeb internal 0x1FFFE → masked 0xFFFE → effective 0xFFFD
     *   Beebium internal 0xFFFE → effective 0xFFFD
     *
     * This ensures both report the same value for the same underlying timer state.
     *
     * Latch value normalization:
     *
     * Timer 1 has a 16-bit latch, Timer 2 only has an 8-bit latch (the 6522
     * doesn't latch T2CH). jsbeeb stores both in doubled format. We normalize:
     *   - t1l: mask to 16 bits and convert from doubled format
     *   - t2l: mask to 8 bits (only low byte is meaningful)
     */
    private extractViaState(via: any): ViaState {
        // Extract and normalize timer counters to match Beebium's effective value
        // (what the CPU would read, including the pending decrement)
        const t1c = ((via.t1c & 0xFFFF) - 1) & 0xFFFF;
        const t2c = ((via.t2c & 0xFFFF) - 1) & 0xFFFF;

        // Normalize latches:
        // - t1l: convert from doubled format to 16-bit
        // - t2l: only low 8 bits are meaningful (6522 doesn't latch T2CH)
        const t1l = via.t1l & 0xFFFF;
        const t2l = via.t2l & 0xFF;  // Only low byte - T2 has 8-bit latch

        return {
            ora: via.ora,
            orb: via.orb,
            ira: via.ira,
            irb: via.irb,
            ddra: via.ddra,
            ddrb: via.ddrb,
            t1c,
            t1l,
            t2c,
            t2l,
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
     * Get detailed video timing state for debugging vsync issues.
     * This exposes internal jsbeeb video state not visible through registers.
     */
    getVideoTimingState(): VideoTimingState {
        const video = this.processor.video;
        return {
            inVsync: video.inVSync ?? false,
            vpulseCounter: video.vpulseCounter ?? 0,
            vpulseWidth: (video.regs[3] ?? 0) >>> 4,
            horizCounter: video.horizCounter ?? 0,
            vertCounter: video.vertCounter ?? 0,
            doEvenFrameLogic: video.doEvenFrameLogic ?? false,
            frameCount: video.frameCount ?? 0,
            hadVsyncThisRow: video.hadVSyncThisRow ?? false,
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

    // =========================================================================
    // Tube second processor support
    // =========================================================================

    /**
     * Whether the Tube is enabled and has a real parasite CPU.
     */
    hasTube(): boolean {
        if (!this.machine) return false;
        // FakeTube has no 'memory' property; real Tube6502 does
        return this.tubeEnabled && this.processor.tube?.memory != null;
    }

    /**
     * Get the parasite (Tube6502) object for direct access.
     * @throws if Tube is not enabled
     */
    private get parasiteProcessor(): any {
        if (!this.hasTube()) throw new Error("Tube not enabled");
        return this.processor.tube;
    }

    /**
     * Get parasite CPU register state.
     */
    getParasiteCpuState(): CpuState {
        const p = this.parasiteProcessor;
        return {
            a: p.a,
            x: p.x,
            y: p.y,
            sp: p.s,
            pc: p.pc,
            p: p.p.asByte() & P_STATUS_MASK,
        };
    }

    /**
     * Read parasite memory (side-effect-free, direct RAM access).
     * Does NOT route through Tube registers at $FEF8-$FEFF.
     */
    readParasiteMemory(address: number, length: number): Uint8Array {
        const mem = this.parasiteProcessor.memory;
        return new Uint8Array(mem.buffer, address, length);
    }

    /**
     * Run the host (and thus the parasite) until the parasite PC reaches
     * the given address. Since jsbeeb has no parasite-level debug hook, we
     * run the host in small cycle batches and check the parasite PC after each.
     *
     * @param address - Target parasite PC address
     * @param timeoutSeconds - Timeout in seconds (default: 30)
     */
    async runUntilParasiteAddress(address: number, timeoutSeconds: number = 30): Promise<void> {
        if (!this.hasTube()) throw new Error("Tube not enabled");
        const parasiteCpu = this.parasiteProcessor;

        // Use the parasite's debug instruction hook to stop at the exact
        // address. The hook fires at each parasite instruction boundary.
        // Tube6502.execute() checks _debugInstruction when it's set.
        let hit = false;
        parasiteCpu._debugInstruction = (pc: number) => {
            if (pc === address) {
                hit = true;
                return true;  // stop parasite execution
            }
            return false;
        };

        let remaining = timeoutSeconds * 2_000_000;
        try {
            while (remaining > 0 && !hit) {
                const batch = Math.min(remaining, 100_000);
                this.processor.execute(batch);
                this.cycleCount += batch;
                remaining -= batch;
            }
        } finally {
            parasiteCpu._debugInstruction = null;
        }

        if (!hit) {
            throw new Error(
                `Parasite did not reach $${address.toString(16).toUpperCase()} in ${timeoutSeconds}s ` +
                `(parasite PC=$${parasiteCpu.pc.toString(16)})`
            );
        }
    }

    /**
     * Type text into the emulator via the system VIA keyboard matrix.
     * Simulates keypresses with short cycle runs between them, following
     * jsbeeb's TestMachine.type() pattern.
     *
     * @param text - Text to type (supports a-z, A-Z, 0-9, common symbols, \\r for RETURN)
     */
    async type(text: string): Promise<void> {
        if (!this.machine) throw new Error("Machine not initialized");
        const sysvia = this.processor.sysvia;
        const cyclesPerKey = 40_000;

        const kd = async (code: number) => {
            sysvia.keyDown(code);
            await this.runCycles(cyclesPerKey);
            sysvia.keyUp(code);
            await this.runCycles(cyclesPerKey);
        };

        for (const ch of text) {
            let code: number;
            let shift = false;

            // Use jsbeeb key codes (DOM key codes), matching TestMachine.type()
            switch (ch) {
                case '\r':
                    code = 13;
                    break;
                case ' ':
                    code = 32;
                    break;
                case '*':
                    code = utils.keyCodes.APOSTROPHE;
                    shift = true;
                    break;
                case '!':
                    code = utils.keyCodes.K1;
                    shift = true;
                    break;
                case '"':
                    code = 50;  // '2' key
                    shift = true;
                    break;
                case '.':
                    code = utils.keyCodes.PERIOD;
                    break;
                case ',':
                    code = utils.keyCodes.COMMA;
                    break;
                case ';':
                    code = utils.keyCodes.SEMICOLON;
                    break;
                case ':':
                    code = utils.keyCodes.APOSTROPHE;
                    break;
                case '&':
                    code = utils.keyCodes.K6;
                    shift = true;
                    break;
                default:
                    if (ch >= 'A' && ch <= 'Z') {
                        code = ch.charCodeAt(0);
                        shift = true;
                    } else {
                        code = ch.toUpperCase().charCodeAt(0);
                    }
                    break;
            }

            if (shift) {
                sysvia.keyDown(16);  // SHIFT
                await this.runCycles(cyclesPerKey);
                await kd(code);
                sysvia.keyUp(16);
                await this.runCycles(cyclesPerKey);
            } else {
                await kd(code);
            }
        }
    }
}
