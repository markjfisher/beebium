/**
 * Debugger control interface for the Beebium TypeScript client.
 *
 * Provides execution control, stepping, and breakpoint management.
 */

import type { DebuggerControlClient } from "./generated/debugger.js";
import type {
    ExecutionState as ProtoExecutionState,
    ExecutionStateEvent as ProtoExecutionStateEvent,
    StepResponse as ProtoStepResponse,
    Breakpoint as ProtoBreakpoint,
} from "./generated/debugger.js";
import { StopReason } from "./generated/debugger.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable } from "./stream-utils.js";
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

export interface ExecutionStateEvent {
    reason: StopReason;
    state: ExecutionState;
    message: string;
}

export type WatchpointType = "read" | "write" | "both";

export interface WatchpointInfo {
    id: number;
    startAddress: number;
    endAddress: number;
    type: WatchpointType;
}

export interface StepResult {
    success: boolean;
    error: string;
    instructionsExecuted: number;
    cyclesExecuted: number;
    state: ExecutionState;
}

export { StopReason };

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

function toExecutionStateEvent(proto: ProtoExecutionStateEvent): ExecutionStateEvent {
    return {
        reason: proto.reason,
        state: proto.state
            ? toExecutionState(proto.state)
            : { isRunning: false, cycleCount: 0, haltReason: "", sequence: 0 },
        message: proto.message,
    };
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

    /**
     * Add a breakpoint on an address range. Returns the breakpoint ID.
     *
     * @param address - Start address (inclusive).
     * @param options.endAddress - End address (exclusive). 0 = address+1 (single address).
     *   Use 0x10000 for a full-range breakpoint that fires at every instruction boundary.
     * @param options.condition - Expression evaluated on hit. Empty = unconditional.
     * @param options.stopCounterpart - Signal the other processor to stop.
     */
    async addBreakpoint(
        address: number,
        options: { endAddress?: number; condition?: string; stopCounterpart?: boolean } = {},
    ): Promise<number> {
        const { endAddress = 0, condition = "", stopCounterpart = false } = options;
        const response = await promisify<
            { startAddress: number; endAddress: number; condition: string; stopCounterpart: boolean },
            { success: boolean; id: number }
        >(
            this.stub as unknown as Record<string, Function>,
            "addBreakpoint",
            { startAddress: address, endAddress, condition, stopCounterpart },
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

    /** Add a watchpoint on an address range. Returns the watchpoint ID. */
    async addWatchpoint(
        startAddress: number,
        endAddress: number,
        type: WatchpointType = "both",
    ): Promise<number> {
        const typeMap: Record<WatchpointType, number> = { read: 0, write: 1, both: 2 };
        const response = await promisify<
            { startAddress: number; endAddress: number; type: number },
            { success: boolean; id: number }
        >(
            this.stub as unknown as Record<string, Function>,
            "addWatchpoint",
            { startAddress, endAddress, type: typeMap[type] },
        );
        if (!response.success) {
            throw new DebuggerError(
                `addWatchpoint failed at 0x${startAddress.toString(16)}-0x${endAddress.toString(16)}`,
            );
        }
        return response.id;
    }

    /** Remove a watchpoint by ID. Returns true if found and removed. */
    async removeWatchpoint(id: number): Promise<boolean> {
        const response = await promisify<{ id: number }, { success: boolean }>(
            this.stub as unknown as Record<string, Function>,
            "removeWatchpoint",
            { id },
        );
        return response.success;
    }

    /** List all current watchpoints. */
    async listWatchpoints(): Promise<WatchpointInfo[]> {
        const typeMap: Record<number, WatchpointType> = { 0: "read", 1: "write", 2: "both" };
        const response = await promisify<
            {},
            { watchpoints: Array<{ id: number; startAddress: number; endAddress: number; type: number }> }
        >(
            this.stub as unknown as Record<string, Function>,
            "listWatchpoints",
            {},
        );
        return response.watchpoints.map((wp) => ({
            id: wp.id,
            startAddress: wp.startAddress,
            endAddress: wp.endAddress,
            type: typeMap[wp.type] ?? "both",
        }));
    }

    /** Clear all watchpoints. Returns the number of watchpoints removed. */
    async clearWatchpoints(): Promise<number> {
        const response = await promisify<{}, { countRemoved: number }>(
            this.stub as unknown as Record<string, Function>,
            "clearWatchpoints",
            {},
        );
        return response.countRemoved;
    }

    /**
     * Subscribe to execution state change events as an async iterable.
     *
     * The server sends the current state immediately, then pushes events
     * whenever the execution state changes (breakpoint hit, manual stop,
     * run resumed, etc.).
     */
    async *watchExecutionState(): AsyncGenerator<ExecutionStateEvent> {
        const stream = this.stub.watchExecutionState({});
        for await (const event of toAsyncIterable(stream)) {
            yield toExecutionStateEvent(event);
        }
    }

    /**
     * Wait for the machine to stop executing.
     *
     * Subscribes to the execution state stream and waits for a transition
     * to the stopped state. If the machine is already stopped when this is
     * called, it waits until the machine runs and then stops again.
     */
    async waitForStop(): Promise<ExecutionStateEvent> {
        let sawRunning = false;
        for await (const event of this.watchExecutionState()) {
            if (event.state.isRunning) {
                sawRunning = true;
            } else if (sawRunning) {
                return event;
            }
        }
        throw new DebuggerError("Execution state stream ended without a stop event");
    }

    /**
     * Run until execution reaches the given address.
     *
     * Sets a temporary breakpoint, subscribes to execution state events,
     * starts execution, and waits for the machine to stop.
     */
    async runUntil(address: number): Promise<ExecutionStateEvent> {
        const bpId = await this.addBreakpoint(address);
        try {
            const stopPromise = this.waitForStop();
            await this.run();
            return await stopPromise;
        } finally {
            await this.removeBreakpoint(bpId);
        }
    }
}
