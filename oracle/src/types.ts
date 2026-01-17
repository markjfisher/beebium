/**
 * Shared state types for differential testing between jsbeeb and Beebium.
 */

/** 6502 CPU register state */
export interface CpuState {
    a: number;      // Accumulator (0-255)
    x: number;      // X index register (0-255)
    y: number;      // Y index register (0-255)
    sp: number;     // Stack pointer (0-255)
    pc: number;     // Program counter (0-65535)
    p: number;      // Processor status flags (0-255)
}

/**
 * 6522 VIA state.
 *
 * Timer value normalization:
 * The t1c and t2c fields report the "effective" timer value - what the CPU
 * would read if it accessed the timer register at this moment. This includes
 * the pending decrement that will occur at the trailing edge of the current
 * cycle. Both jsbeeb and Beebium wrappers normalize to this convention,
 * despite having different internal representations:
 *
 *   - jsbeeb uses doubled internal counters; wrapper applies ((t1c & 0xFFFF) - 1) & 0xFFFF
 *   - Beebium reports effective_t1() which is already (internal - 1)
 *
 * See jsbeeb-oracle.ts extractViaState() for full normalization details.
 */
export interface ViaState {
    ora: number;    // Output Register A
    orb: number;    // Output Register B
    ira: number;    // Input Register A
    irb: number;    // Input Register B
    ddra: number;   // Data Direction Register A
    ddrb: number;   // Data Direction Register B
    t1c: number;    // Timer 1 counter (effective value, normalized)
    t1l: number;    // Timer 1 latch (16-bit)
    t2c: number;    // Timer 2 counter (effective value, normalized)
    t2l: number;    // Timer 2 latch (8-bit only - 6522 doesn't latch T2CH)
    acr: number;    // Auxiliary Control Register
    pcr: number;    // Peripheral Control Register
    ifr: number;    // Interrupt Flag Register
    ier: number;    // Interrupt Enable Register
    sr: number;     // Shift Register
    ca1: boolean;   // Control line CA1
    ca2: boolean;   // Control line CA2
    cb1: boolean;   // Control line CB1
    cb2: boolean;   // Control line CB2
}

/** 6845 CRTC state */
export interface CrtcState {
    registers: number[];    // All 18 CRTC registers (R0-R17)
    addressRegister: number; // Currently selected register
}

/** Video ULA state */
export interface VideoUlaState {
    control: number;        // Control register
    palette: number[];      // 16-entry palette (logical -> physical)
}

/** Video timing state for debugging vsync issues */
export interface VideoTimingState {
    inVsync: boolean;           // Currently in vsync
    vpulseCounter: number;      // Vsync pulse counter
    vpulseWidth: number;        // Vsync pulse width from R3
    horizCounter: number;       // Horizontal character counter
    vertCounter: number;        // Vertical character counter
    doEvenFrameLogic: boolean;  // Even frame interlace logic active
    frameCount: number;         // Frame counter
    hadVsyncThisRow: boolean;   // Already had vsync in this character row
}

/** Complete machine state snapshot */
export interface MachineState {
    cpu: CpuState;
    systemVia: ViaState;
    userVia: ViaState;
    crtc: CrtcState;
    videoUla: VideoUlaState;
    cycles: number;
}

/** Result of comparing two states */
export interface ComparisonResult {
    match: boolean;
    jsbeebCycles: number;
    beebiumCycles: number;
    divergences: Divergence[];
}

/** A single divergence between emulator states */
export interface Divergence {
    component: 'cpu' | 'memory' | 'system_via' | 'user_via' | 'crtc' | 'video_ula';
    field: string;
    jsbeebValue: unknown;
    beebiumValue: unknown;
}

/** Address range for memory comparison */
export interface AddressRange {
    start: number;
    end: number;
}

/**
 * Mask for comparing 6502 P (processor status) register.
 *
 * Bits 4 and 5 are excluded because they don't represent real CPU state:
 * - Bit 5: Unused, always reads as 1 when pushed to stack
 * - Bit 4: B flag, only meaningful when P is pushed (distinguishes BRK from IRQ)
 *
 * Different emulators store these bits differently, but the difference
 * doesn't affect emulation correctness.
 */
export const P_STATUS_MASK = 0b11001111;  // 0xCF - excludes bits 4 and 5

/** Server status types */
export enum ServerStatusType {
    READY = 'SERVER_STATUS_READY',
    SHUTTING_DOWN = 'SERVER_STATUS_SHUTTING_DOWN',
}

/** Server status event from WatchServerStatus stream */
export interface ServerStatusEvent {
    status: ServerStatusType;
    message: string;
    shutdownGraceMs: number;
}

/**
 * Record of a point where cycle difference between emulators changed.
 * Used by findCycleDifferenceChanges() for coarse bisection.
 */
export interface CycleDiffChange {
    /** Approximate instruction count where change was detected */
    approximateInstruction: number;
    /** Cycle difference before this change */
    previousDiff: number;
    /** Cycle difference after this change */
    newDiff: number;
    /** Change in cycle difference (newDiff - previousDiff) */
    delta: number;
    /** jsbeeb cycle count at this point */
    jsbeebCycles: number;
    /** Beebium cycle count at this point */
    beebiumCycles: number;
    /** jsbeeb PC at this point */
    jsbeebPc: number;
    /** Beebium PC at this point */
    beebiumPc: number;
}

/**
 * Detailed information about where PCs first diverged.
 * Used by findFirstPcDivergence() for fine-grained analysis.
 */
export interface PcDivergence {
    /** Instruction number where divergence occurred */
    instruction: number;
    /** jsbeeb CPU state before the diverging instruction */
    jsbeebBefore: CpuState;
    /** Beebium CPU state before the diverging instruction */
    beebiumBefore: CpuState;
    /** jsbeeb CPU state after the diverging instruction */
    jsbeebAfter: CpuState;
    /** Beebium CPU state after the diverging instruction */
    beebiumAfter: CpuState;
    /** Memory bytes at PC where divergence occurred (for disassembly) */
    memoryAtPc: Uint8Array;
    /** Cycle difference at point of divergence */
    cycleDiff: number;
}
