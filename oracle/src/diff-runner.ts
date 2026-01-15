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
        // Add breakpoint to Beebium
        const bpId = await this.beebium.addBreakpoint(address);

        try {
            // Run jsbeeb until address
            await this.jsbeeb.runUntilAddress(address);

            // Run Beebium until breakpoint
            await this.beebium.run();

            // Wait for Beebium to stop (hit breakpoint)
            let state = await this.beebium.getState();
            const startTime = Date.now();
            while (state.isRunning && Date.now() - startTime < 10000) {
                await new Promise(r => setTimeout(r, 10));
                state = await this.beebium.getState();
            }

            if (state.isRunning) {
                await this.beebium.stop();
                throw new Error("Beebium did not hit breakpoint in time");
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
