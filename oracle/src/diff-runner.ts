/**
 * Differential Test Runner
 *
 * Coordinates jsbeeb and Beebium emulators, stepping them in lockstep
 * and comparing state to detect divergences.
 */

import type {
    CpuState,
    ViaState,
    CrtcState,
    VideoUlaState,
    MachineState,
    ComparisonResult,
    Divergence,
    AddressRange,
    CycleDiffChange,
    PcDivergence,
} from './types.js';
import { JsbeebOracle } from './jsbeeb-oracle.js';
import { BeebiumClient } from './beebium-client.js';

export interface DiffRunnerOptions {
    /** Compare VIA state (default: true) */
    compareVia?: boolean;
    /** Compare CRTC state (default: true) */
    compareCrtc?: boolean;
    /** Compare Video ULA state (default: true) */
    compareVideoUla?: boolean;
    /** Memory ranges to compare */
    memoryRanges?: AddressRange[];
    /** Stop on first divergence (default: true) */
    stopOnDivergence?: boolean;
}

const defaultOptions: DiffRunnerOptions = {
    compareVia: true,
    compareCrtc: true,
    compareVideoUla: true,
    memoryRanges: [],
    stopOnDivergence: true,
};

export class DiffRunner {
    private jsbeeb: JsbeebOracle;
    private beebium: BeebiumClient;
    private options: DiffRunnerOptions;

    constructor(
        jsbeeb: JsbeebOracle,
        beebium: BeebiumClient,
        options: DiffRunnerOptions = {}
    ) {
        this.jsbeeb = jsbeeb;
        this.beebium = beebium;
        this.options = { ...defaultOptions, ...options };
    }

    /**
     * Step both emulators by the specified number of instructions.
     * @param count - Number of instructions to step
     * @returns Comparison result after stepping
     */
    async stepBoth(count: number = 1): Promise<ComparisonResult> {
        // Step jsbeeb
        await this.jsbeeb.stepInstructions(count);

        // Step Beebium
        await this.beebium.stepInstruction(count);

        // Compare state
        return this.compareState();
    }

    /**
     * Run both emulators until a specific address is reached.
     * @param address - Target address
     * @param maxInstructions - Maximum instructions before timeout
     */
    async runUntilAddress(
        address: number,
        maxInstructions: number = 1_000_000
    ): Promise<ComparisonResult> {
        // Add breakpoint to Beebium (client handles PC offset internally)
        const bpId = await this.beebium.addBreakpoint(address);

        try {
            // Run jsbeeb until address
            await this.jsbeeb.runUntilAddress(address);

            // Run Beebium until breakpoint
            await this.beebium.run();

            // Wait for Beebium to stop (hit breakpoint)
            // Boot to BASIC takes ~6M cycles = 3 seconds at 2MHz, give plenty of margin
            const timeoutMs = 30000;
            let state = await this.beebium.getState();
            const startTime = Date.now();
            while (state.isRunning && Date.now() - startTime < timeoutMs) {
                await new Promise(r => setTimeout(r, 50));
                state = await this.beebium.getState();
            }

            if (state.isRunning) {
                const cpu = await this.beebium.getCpuState();
                await this.beebium.stop();
                throw new Error(`Beebium did not hit breakpoint at $${address.toString(16).toUpperCase()} in ${timeoutMs}ms (currently at PC=$${cpu.pc.toString(16).toUpperCase()})`);
            }

            return this.compareState();
        } finally {
            await this.beebium.removeBreakpoint(bpId);
        }
    }

    /**
     * Compare current state of both emulators.
     */
    async compareState(): Promise<ComparisonResult> {
        const jsbeebState = this.jsbeeb.getState();
        const beebiumState = await this.beebium.getMachineState();

        const divergences: Divergence[] = [];

        // Compare CPU
        divergences.push(...this.compareCpu(jsbeebState.cpu, beebiumState.cpu));

        // Compare VIAs
        if (this.options.compareVia) {
            divergences.push(
                ...this.compareVia('system_via', jsbeebState.systemVia, beebiumState.systemVia)
            );
            divergences.push(
                ...this.compareVia('user_via', jsbeebState.userVia, beebiumState.userVia)
            );
        }

        // Compare CRTC
        if (this.options.compareCrtc) {
            divergences.push(...this.compareCrtcState(jsbeebState.crtc, beebiumState.crtc));
        }

        // Compare Video ULA
        if (this.options.compareVideoUla) {
            divergences.push(
                ...this.compareVideoUlaState(jsbeebState.videoUla, beebiumState.videoUla)
            );
        }

        // Compare memory ranges
        for (const range of this.options.memoryRanges || []) {
            divergences.push(...await this.compareMemoryRange(range));
        }

        return {
            match: divergences.length === 0,
            jsbeebCycles: jsbeebState.cycles,
            beebiumCycles: beebiumState.cycles,
            divergences,
        };
    }

    /**
     * Compare CPU state.
     */
    private compareCpu(jsbeeb: CpuState, beebium: CpuState): Divergence[] {
        const divergences: Divergence[] = [];
        const fields: (keyof CpuState)[] = ['a', 'x', 'y', 'sp', 'pc', 'p'];

        for (const field of fields) {
            if (jsbeeb[field] !== beebium[field]) {
                divergences.push({
                    component: 'cpu',
                    field,
                    jsbeebValue: jsbeeb[field],
                    beebiumValue: beebium[field],
                });
            }
        }

        return divergences;
    }

    /**
     * Compare VIA state.
     */
    private compareVia(
        component: 'system_via' | 'user_via',
        jsbeeb: ViaState,
        beebium: ViaState
    ): Divergence[] {
        const divergences: Divergence[] = [];
        const fields: (keyof ViaState)[] = [
            'ora', 'orb', 'ira', 'irb', 'ddra', 'ddrb',
            't1c', 't1l', 't2c', 't2l',
            'acr', 'pcr', 'ifr', 'ier', 'sr',
            'ca1', 'ca2', 'cb1', 'cb2',
        ];

        for (const field of fields) {
            if (jsbeeb[field] !== beebium[field]) {
                divergences.push({
                    component,
                    field,
                    jsbeebValue: jsbeeb[field],
                    beebiumValue: beebium[field],
                });
            }
        }

        return divergences;
    }

    /**
     * Compare CRTC state.
     */
    private compareCrtcState(jsbeeb: CrtcState, beebium: CrtcState): Divergence[] {
        const divergences: Divergence[] = [];

        // Compare registers
        for (let i = 0; i < 18; i++) {
            const jsVal = jsbeeb.registers[i] ?? 0;
            const beebVal = beebium.registers[i] ?? 0;
            if (jsVal !== beebVal) {
                divergences.push({
                    component: 'crtc',
                    field: `R${i}`,
                    jsbeebValue: jsVal,
                    beebiumValue: beebVal,
                });
            }
        }

        // Compare address register
        if (jsbeeb.addressRegister !== beebium.addressRegister) {
            divergences.push({
                component: 'crtc',
                field: 'addressRegister',
                jsbeebValue: jsbeeb.addressRegister,
                beebiumValue: beebium.addressRegister,
            });
        }

        return divergences;
    }

    /**
     * Compare Video ULA state.
     */
    private compareVideoUlaState(
        jsbeeb: VideoUlaState,
        beebium: VideoUlaState
    ): Divergence[] {
        const divergences: Divergence[] = [];

        // Compare control register
        if (jsbeeb.control !== beebium.control) {
            divergences.push({
                component: 'video_ula',
                field: 'control',
                jsbeebValue: jsbeeb.control,
                beebiumValue: beebium.control,
            });
        }

        // Compare palette
        for (let i = 0; i < 16; i++) {
            const jsVal = jsbeeb.palette[i] ?? 0;
            const beebVal = beebium.palette[i] ?? 0;
            if (jsVal !== beebVal) {
                divergences.push({
                    component: 'video_ula',
                    field: `palette[${i}]`,
                    jsbeebValue: jsVal,
                    beebiumValue: beebVal,
                });
            }
        }

        return divergences;
    }

    /**
     * Compare a memory range.
     */
    private async compareMemoryRange(range: AddressRange): Promise<Divergence[]> {
        const divergences: Divergence[] = [];

        const jsbeebMem = this.jsbeeb.readMemory(range.start, range.end - range.start);
        const beebiumMem = await this.beebium.peekMemory(range.start, range.end - range.start);

        for (let i = 0; i < jsbeebMem.length; i++) {
            if (jsbeebMem[i] !== beebiumMem[i]) {
                divergences.push({
                    component: 'memory',
                    field: `$${(range.start + i).toString(16).toUpperCase().padStart(4, '0')}`,
                    jsbeebValue: jsbeebMem[i],
                    beebiumValue: beebiumMem[i],
                });
            }
        }

        return divergences;
    }

    /**
     * Bisect to find where cycle difference changes.
     * Uses exponential probing followed by binary search.
     *
     * @param maxInstructions - Maximum instructions to search
     * @param onProgress - Callback for progress updates
     * @returns Array of points where cycle difference changed
     */
    async findCycleDifferenceChanges(
        maxInstructions: number = 700_000,
        onProgress?: (instruction: number, cycleDiff: number) => void
    ): Promise<CycleDiffChange[]> {
        const changes: CycleDiffChange[] = [];
        let lastCycleDiff = 0;
        let position = 0;

        // Exponential probing to find regions of interest
        const checkpoints = [
            100, 500, 1000, 2000, 5000, 10000, 20000, 50000,
            100000, 150000, 200000, 250000, 300000, 350000,
            400000, 450000, 500000, 550000, 600000, 650000, 700000
        ].filter(c => c <= maxInstructions);

        for (const checkpoint of checkpoints) {
            const toStep = checkpoint - position;
            if (toStep > 0) {
                await this.stepBoth(toStep);
                position = checkpoint;
            }

            const jsbeebState = this.jsbeeb.getState();
            const beebiumState = await this.beebium.getMachineState();
            const cycleDiff = beebiumState.cycles - jsbeebState.cycles;

            if (onProgress) {
                onProgress(position, cycleDiff);
            }

            if (cycleDiff !== lastCycleDiff) {
                changes.push({
                    approximateInstruction: checkpoint,
                    previousDiff: lastCycleDiff,
                    newDiff: cycleDiff,
                    delta: cycleDiff - lastCycleDiff,
                    jsbeebCycles: jsbeebState.cycles,
                    beebiumCycles: beebiumState.cycles,
                    jsbeebPc: jsbeebState.cpu.pc,
                    beebiumPc: beebiumState.cpu.pc,
                });
            }

            lastCycleDiff = cycleDiff;
        }

        return changes;
    }

    /**
     * Find where both emulators' PCs first diverge.
     * Steps instruction-by-instruction from current position.
     *
     * @param maxInstructions - Maximum instructions to check
     * @param onProgress - Callback for progress updates (called every 1000 instructions)
     * @returns Divergence info or null if no divergence found
     */
    async findFirstPcDivergence(
        maxInstructions: number = 50_000,
        onProgress?: (instruction: number, pc: number, cycleDiff: number) => void
    ): Promise<PcDivergence | null> {
        for (let i = 0; i < maxInstructions; i++) {
            // Get state BEFORE stepping
            const jsbeebBefore = this.jsbeeb.getCpuState();
            const beebiumBefore = await this.beebium.getCpuState();

            // Step both by 1 instruction
            await this.stepBoth(1);

            // Get state AFTER stepping
            const jsbeebAfter = this.jsbeeb.getCpuState();
            const beebiumAfter = await this.beebium.getCpuState();

            // Compare PCs
            if (jsbeebAfter.pc !== beebiumAfter.pc) {
                const mem = this.jsbeeb.readMemory(jsbeebBefore.pc, 8);
                return {
                    instruction: i + 1,
                    jsbeebBefore,
                    beebiumBefore,
                    jsbeebAfter,
                    beebiumAfter,
                    memoryAtPc: mem,
                    cycleDiff: (await this.beebium.getMachineState()).cycles - this.jsbeeb.getCycles(),
                };
            }

            // Progress callback
            if (onProgress && (i + 1) % 1000 === 0) {
                const jsState = this.jsbeeb.getState();
                const beeState = await this.beebium.getMachineState();
                onProgress(i + 1, jsState.cpu.pc, beeState.cycles - jsState.cycles);
            }
        }

        return null;
    }

    /**
     * Synchronize both emulators after reset.
     * Steps past initialization differences so both are at the same instruction.
     *
     * After reset:
     * - jsbeeb: PC=$D9CD (reset vector), has executed 0 cycles
     * - Beebium: May be at different state due to reset sequence simulation
     *
     * This method steps both until they reach a known sync point.
     */
    async synchronizeAfterReset(): Promise<{ syncPc: number; jsbeebCycles: number; beebiumCycles: number }> {
        // Step jsbeeb to execute first instruction
        await this.jsbeeb.stepInstructions(1);

        // Step Beebium to execute first instruction
        await this.beebium.stepInstruction(1);

        const jsbeebState = this.jsbeeb.getCpuState();
        const beebiumState = await this.beebium.getCpuState();

        // If PCs match, we're synchronized
        if (jsbeebState.pc === beebiumState.pc) {
            return {
                syncPc: jsbeebState.pc,
                jsbeebCycles: this.jsbeeb.getCycles(),
                beebiumCycles: (await this.beebium.getMachineState()).cycles,
            };
        }

        // Otherwise, step until PCs match (up to 10 instructions)
        for (let i = 0; i < 10; i++) {
            if (jsbeebState.pc < beebiumState.pc) {
                await this.jsbeeb.stepInstructions(1);
            } else {
                await this.beebium.stepInstruction(1);
            }

            const js = this.jsbeeb.getCpuState();
            const bee = await this.beebium.getCpuState();

            if (js.pc === bee.pc) {
                return {
                    syncPc: js.pc,
                    jsbeebCycles: this.jsbeeb.getCycles(),
                    beebiumCycles: (await this.beebium.getMachineState()).cycles,
                };
            }
        }

        throw new Error('Failed to synchronize emulators after reset');
    }

    /**
     * Format a comparison result for display.
     */
    static formatResult(result: ComparisonResult): string {
        const lines: string[] = [];

        if (result.match) {
            lines.push('✓ States match');
        } else {
            lines.push(`✗ ${result.divergences.length} divergence(s) found:`);
            for (const div of result.divergences) {
                lines.push(
                    `  ${div.component}.${div.field}: ` +
                    `jsbeeb=${formatValue(div.jsbeebValue)} ` +
                    `beebium=${formatValue(div.beebiumValue)}`
                );
            }
        }

        lines.push(`Cycles: jsbeeb=${result.jsbeebCycles} beebium=${result.beebiumCycles}`);

        return lines.join('\n');
    }
}

/**
 * Format a value for display.
 */
function formatValue(value: unknown): string {
    if (typeof value === 'number') {
        return `0x${value.toString(16).toUpperCase().padStart(2, '0')}`;
    }
    if (typeof value === 'boolean') {
        return value ? 'true' : 'false';
    }
    return String(value);
}
