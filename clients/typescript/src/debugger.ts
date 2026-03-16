/**
 * Debugger control interface for the Beebium TypeScript client.
 *
 * Provides execution control, stepping, and breakpoint management.
 */

import type { DebuggerControlClient } from "./generated/debugger.js";
import type {
    ExecutionState as ProtoExecutionState,
    StepResponse as ProtoStepResponse,
    Breakpoint as ProtoBreakpoint,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";
import { DebuggerError } from "./exceptions.js";

export interface ExecutionState {
    isRunning: boolean;
    cycleCount: number;
    haltReason: string;
    sequence: number;
}

export interface Breakpoint {
    id: number;
    address: number;
}

export interface StepResult {
    success: boolean;
    error: string;
    instructionsExecuted: number;
    cyclesExecuted: number;
    state: ExecutionState;
}

function toExecutionState(proto: ProtoExecutionState): ExecutionState {
    return {
        isRunning: proto.isRunning,
        cycleCount: proto.cycleCount,
        haltReason: proto.haltReason,
        sequence: proto.sequence,
    };
}

function toStepResult(proto: ProtoStepResponse): StepResult {
    return {
        success: proto.success,
        error: proto.error,
        instructionsExecuted: proto.instructionsExecuted,
        cyclesExecuted: proto.cyclesExecuted,
        state: proto.state
            ? toExecutionState(proto.state)
            : { isRunning: false, cycleCount: 0, haltReason: "", sequence: 0 },
    };
}

function toBreakpoint(proto: ProtoBreakpoint): Breakpoint {
    return {
        id: proto.id,
        address: proto.address,
    };
}

function delay(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * Debugger control interface.
 *
 * Provides execution control, stepping, and breakpoint management.
 */
export class Debugger {
    private readonly stub: DebuggerControlClient;

    constructor(stub: DebuggerControlClient) {
        this.stub = stub;
    }

    /** Get the current execution state. */
    async getState(): Promise<ExecutionState> {
        const response = await promisify<{}, ProtoExecutionState>(
            this.stub as unknown as Record<string, Function>,
            "getState",
            {},
        );
        return toExecutionState(response);
    }

    /** Start execution. Throws DebuggerError if the request is not successful. */
    async run(): Promise<void> {
        const response = await promisify<{}, { success: boolean; error: string }>(
            this.stub as unknown as Record<string, Function>,
            "run",
            {},
        );
        if (!response.success) {
            throw new DebuggerError(`run failed: ${response.error}`);
        }
    }

    /** Stop execution. Returns the execution state after stopping. */
    async stop(): Promise<ExecutionState> {
        const response = await promisify<{}, { success: boolean; state?: ProtoExecutionState }>(
            this.stub as unknown as Record<string, Function>,
            "stop",
            {},
        );
        if (!response.success) {
            throw new DebuggerError("stop failed");
        }
        if (!response.state) {
            throw new DebuggerError("stop returned no state");
        }
        return toExecutionState(response.state);
    }

    /** Reset the machine. */
    async reset(): Promise<void> {
        const response = await promisify<{}, { success: boolean }>(
            this.stub as unknown as Record<string, Function>,
            "reset",
            {},
        );
        if (!response.success) {
            throw new DebuggerError("reset failed");
        }
    }

    /** Step by instruction count. Returns the result of the step operation. */
    async step(count: number = 1): Promise<StepResult> {
        const response = await promisify<{ count: number }, ProtoStepResponse>(
            this.stub as unknown as Record<string, Function>,
            "stepInstruction",
            { count },
        );
        if (!response.success) {
            throw new DebuggerError(`step failed: ${response.error}`);
        }
        return toStepResult(response);
    }

    /** Step by cycle count. Returns the result of the step operation. */
    async stepCycles(count: number = 1): Promise<StepResult> {
        const response = await promisify<{ count: number }, ProtoStepResponse>(
            this.stub as unknown as Record<string, Function>,
            "stepCycle",
            { count },
        );
        if (!response.success) {
            throw new DebuggerError(`stepCycles failed: ${response.error}`);
        }
        return toStepResult(response);
    }

    /** Whether the machine is currently running. */
    async isRunning(): Promise<boolean> {
        const state = await this.getState();
        return state.isRunning;
    }

    /** Whether the machine is currently stopped. */
    async isStopped(): Promise<boolean> {
        const state = await this.getState();
        return !state.isRunning;
    }

    /** Add a breakpoint at the given address. Returns the breakpoint ID. */
    async addBreakpoint(address: number): Promise<number> {
        const response = await promisify<{ address: number }, { success: boolean; id: number }>(
            this.stub as unknown as Record<string, Function>,
            "addBreakpoint",
            { address },
        );
        if (!response.success) {
            throw new DebuggerError(`addBreakpoint failed at address 0x${address.toString(16)}`);
        }
        return response.id;
    }

    /** Remove a breakpoint by ID. Returns true if the breakpoint was found and removed. */
    async removeBreakpoint(id: number): Promise<boolean> {
        const response = await promisify<{ id: number }, { success: boolean }>(
            this.stub as unknown as Record<string, Function>,
            "removeBreakpoint",
            { id },
        );
        return response.success;
    }

    /** List all current breakpoints. */
    async listBreakpoints(): Promise<Breakpoint[]> {
        const response = await promisify<{}, { breakpoints: ProtoBreakpoint[] }>(
            this.stub as unknown as Record<string, Function>,
            "listBreakpoints",
            {},
        );
        return response.breakpoints.map(toBreakpoint);
    }

    /** Clear all breakpoints. Returns the number of breakpoints removed. */
    async clearBreakpoints(): Promise<number> {
        const response = await promisify<{}, { countRemoved: number }>(
            this.stub as unknown as Record<string, Function>,
            "clearBreakpoints",
            {},
        );
        return response.countRemoved;
    }

    /**
     * Run until execution reaches the given address.
     *
     * Sets a temporary breakpoint, starts execution, polls until stopped,
     * then removes the breakpoint.
     */
    async runUntil(address: number, pollIntervalMs: number = 1): Promise<ExecutionState> {
        const bpId = await this.addBreakpoint(address);
        try {
            await this.run();
            while (true) {
                await delay(pollIntervalMs);
                const state = await this.getState();
                if (!state.isRunning) {
                    return state;
                }
            }
        } finally {
            await this.removeBreakpoint(bpId);
        }
    }
}
