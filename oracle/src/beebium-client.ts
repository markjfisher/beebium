/**
 * Beebium gRPC Client
 *
 * TypeScript client for connecting to beebium-server via gRPC.
 * Provides access to debugger, CPU, memory, and device state.
 */

import * as grpc from '@grpc/grpc-js';
import * as protoLoader from '@grpc/proto-loader';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import type { CpuState, ViaState, CrtcState, VideoUlaState, MachineState, ServerStatusEvent } from './types.js';
import { ServerStatusType, P_STATUS_MASK } from './types.js';
import { EventEmitter } from 'events';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Paths to proto files
const DEBUGGER_PROTO_PATH = join(__dirname, '../../src/service/proto/debugger.proto');
const SYSTEM_PROTO_PATH = join(__dirname, '../../src/service/proto/system.proto');

const protoLoaderOptions = {
    keepCase: false,
    longs: Number,
    enums: String,
    defaults: true,
    oneofs: true,
};

// Load proto definitions
const debuggerPackageDefinition = protoLoader.loadSync(DEBUGGER_PROTO_PATH, protoLoaderOptions);
const systemPackageDefinition = protoLoader.loadSync(SYSTEM_PROTO_PATH, protoLoaderOptions);

const debuggerProtoDescriptor = grpc.loadPackageDefinition(debuggerPackageDefinition) as any;
const systemProtoDescriptor = grpc.loadPackageDefinition(systemPackageDefinition) as any;

const beebium = debuggerProtoDescriptor.beebium;
const beebiumSystem = systemProtoDescriptor.beebium;

export interface StepResult {
    success: boolean;
    instructionsExecuted: number;
    cyclesExecuted: number;
    isRunning: boolean;
}

/**
 * Event emitter for server status notifications.
 * Emits:
 *   - 'status': ServerStatusEvent - when server sends a status update
 *   - 'ready': void - when server reports ready status
 *   - 'shutdown': { graceMs: number, message: string } - when server is shutting down
 *   - 'error': Error - on stream error
 *   - 'end': void - when stream ends
 */
export class ServerStatusWatcher extends EventEmitter {
    private call: grpc.ClientReadableStream<any> | null = null;

    /** @internal */
    _setCall(call: grpc.ClientReadableStream<any>): void {
        this.call = call;

        call.on('data', (response: any) => {
            const event: ServerStatusEvent = {
                status: response.status as ServerStatusType,
                message: response.message || '',
                shutdownGraceMs: response.shutdownGraceMs || 0,
            };

            this.emit('status', event);

            // Emit convenience events
            if (event.status === ServerStatusType.READY) {
                this.emit('ready');
            } else if (event.status === ServerStatusType.SHUTTING_DOWN) {
                this.emit('shutdown', {
                    graceMs: event.shutdownGraceMs,
                    message: event.message,
                });
            }
        });

        call.on('error', (error: Error) => {
            // CANCELLED errors are expected when we cancel the stream
            if ((error as any).code !== grpc.status.CANCELLED) {
                this.emit('error', error);
            }
        });

        call.on('end', () => {
            this.emit('end');
        });
    }

    /**
     * Cancel the status watch subscription.
     */
    cancel(): void {
        if (this.call) {
            this.call.cancel();
            this.call = null;
        }
    }
}

export class BeebiumClient {
    private client: any;
    private systemClient: any;
    private cycleCount: number = 0;

    constructor(target: string = 'localhost:50051') {
        this.client = new beebium.DebuggerControl(
            target,
            grpc.credentials.createInsecure()
        );
        this.systemClient = new beebiumSystem.SystemService(
            target,
            grpc.credentials.createInsecure()
        );
    }

    /**
     * Connect to a running beebium-server.
     */
    static connect(target: string = 'localhost:50051'): BeebiumClient {
        return new BeebiumClient(target);
    }

    /**
     * Close the connection.
     */
    close(): void {
        grpc.closeClient(this.client);
        grpc.closeClient(this.systemClient);
    }

    /**
     * Helper to promisify gRPC calls.
     */
    private call<T>(method: string, request: any): Promise<T> {
        return new Promise((resolve, reject) => {
            this.client[method](request, (error: any, response: T) => {
                if (error) reject(error);
                else resolve(response);
            });
        });
    }

    /**
     * Get execution state.
     */
    async getState(): Promise<{ isRunning: boolean; cycleCount: number; sequence: number }> {
        const response = await this.call<any>('GetState', {});
        return {
            isRunning: response.isRunning,
            cycleCount: Number(response.cycleCount),
            sequence: Number(response.sequence),
        };
    }

    /**
     * Stop execution.
     */
    async stop(): Promise<void> {
        await this.call('Stop', {});
    }

    /**
     * Resume execution.
     */
    async run(): Promise<void> {
        await this.call('Run', {});
    }

    /**
     * Reset the machine.
     */
    async reset(): Promise<void> {
        await this.call('Reset', {});
        this.cycleCount = 0;
    }

    /**
     * Step one or more instructions.
     */
    async stepInstruction(count: number = 1): Promise<StepResult> {
        const response = await this.call<any>('StepInstruction', { count });
        this.cycleCount += Number(response.cyclesExecuted);
        return {
            success: response.success,
            instructionsExecuted: response.instructionsExecuted,
            cyclesExecuted: Number(response.cyclesExecuted),
            isRunning: response.state?.isRunning ?? false,
        };
    }

    /**
     * Step one or more cycles.
     */
    async stepCycle(count: number = 1): Promise<StepResult> {
        const response = await this.call<any>('StepCycle', { count });
        this.cycleCount += Number(response.cyclesExecuted);
        return {
            success: response.success,
            instructionsExecuted: response.instructionsExecuted,
            cyclesExecuted: Number(response.cyclesExecuted),
            isRunning: response.state?.isRunning ?? false,
        };
    }

    /**
     * Get CPU register state.
     *
     * Values are normalized for comparison with jsbeeb:
     * - PC is decremented by 1 (Beebium reports next fetch address, jsbeeb reports instruction address)
     * - P has bits 4-5 masked out (these don't represent real CPU state)
     */
    async getCpuState(): Promise<CpuState> {
        const response = await this.call<any>('Get6502State', {});
        return {
            a: response.a,
            x: response.x,
            y: response.y,
            sp: response.sp,
            pc: (response.pc - 1) & 0xFFFF,  // Normalize: Beebium pre-increments PC during fetch
            p: response.p & P_STATUS_MASK,   // Normalize: exclude bits 4-5
        };
    }

    /**
     * Set CPU register state.
     */
    async setCpuState(state: Partial<CpuState>): Promise<void> {
        await this.call('Set6502State', state);
    }

    /**
     * Get System VIA state.
     */
    async getSystemViaState(): Promise<ViaState> {
        const response = await this.call<any>('GetSystemViaState', {});
        return this.mapViaResponse(response);
    }

    /**
     * Get User VIA state.
     */
    async getUserViaState(): Promise<ViaState> {
        const response = await this.call<any>('GetUserViaState', {});
        return this.mapViaResponse(response);
    }

    /**
     * Map VIA gRPC response to ViaState.
     */
    private mapViaResponse(response: any): ViaState {
        return {
            ora: response.ora,
            orb: response.orb,
            ira: response.ira,
            irb: response.irb,
            ddra: response.ddra,
            ddrb: response.ddrb,
            t1c: response.t1c,
            t1l: response.t1l,
            t2c: response.t2c,
            t2l: response.t2l,
            acr: response.acr,
            pcr: response.pcr,
            ifr: response.ifr,
            ier: response.ier,
            sr: response.sr,
            ca1: response.ca1,
            ca2: response.ca2,
            cb1: response.cb1,
            cb2: response.cb2,
        };
    }

    /**
     * Get CRTC state.
     */
    async getCrtcState(): Promise<CrtcState> {
        const response = await this.call<any>('GetCrtcState', {});
        return {
            registers: response.registers || [],
            addressRegister: response.addressRegister,
        };
    }

    /**
     * Get Video ULA state.
     */
    async getVideoUlaState(): Promise<VideoUlaState> {
        const response = await this.call<any>('GetVideoUlaState', {});
        return {
            control: response.control,
            palette: response.palette || [],
        };
    }

    /**
     * Read memory (with side effects).
     */
    async readMemory(address: number, length: number): Promise<Uint8Array> {
        const response = await this.call<any>('ReadMemory', { address, length });
        return new Uint8Array(response.data);
    }

    /**
     * Peek memory (without side effects).
     */
    async peekMemory(address: number, length: number): Promise<Uint8Array> {
        const response = await this.call<any>('PeekMemory', { address, length });
        return new Uint8Array(response.data);
    }

    /**
     * Write memory.
     */
    async writeMemory(address: number, data: Uint8Array): Promise<void> {
        await this.call('WriteMemory', { address, data: Buffer.from(data) });
    }

    /**
     * Add a breakpoint.
     */
    async addBreakpoint(address: number): Promise<number> {
        const response = await this.call<any>('AddBreakpoint', { address });
        return response.id;
    }

    /**
     * Remove a breakpoint.
     */
    async removeBreakpoint(id: number): Promise<void> {
        await this.call('RemoveBreakpoint', { id });
    }

    /**
     * Clear all breakpoints.
     */
    async clearBreakpoints(): Promise<number> {
        const response = await this.call<any>('ClearBreakpoints', {});
        return response.countRemoved;
    }

    /**
     * Get complete machine state snapshot.
     */
    async getMachineState(): Promise<MachineState> {
        const [cpu, systemVia, userVia, crtc, videoUla, state] = await Promise.all([
            this.getCpuState(),
            this.getSystemViaState(),
            this.getUserViaState(),
            this.getCrtcState(),
            this.getVideoUlaState(),
            this.getState(),
        ]);

        return {
            cpu,
            systemVia,
            userVia,
            crtc,
            videoUla,
            cycles: state.cycleCount,  // Use actual server cycle count
        };
    }

    /**
     * Get current cycle count.
     */
    getCycles(): number {
        return this.cycleCount;
    }

    /**
     * Run until PC reaches the specified address.
     * Uses stepInstruction internally since breakpoints don't work during free-run.
     * @param address - Target address
     * @param timeoutMs - Timeout in milliseconds (default: 30000)
     * @param progressCallback - Optional callback for progress reporting
     */
    async runUntilAddress(
        address: number,
        timeoutMs: number = 30000,
        progressCallback?: (cycles: number, pc: number) => void
    ): Promise<void> {
        const startTime = Date.now();
        const batchSize = 1000;  // Step in batches for efficiency

        while (Date.now() - startTime < timeoutMs) {
            // Step a batch of instructions
            for (let i = 0; i < batchSize; i++) {
                const result = await this.stepInstruction(1);

                // Check if we hit the target address
                const cpu = await this.getCpuState();
                if (cpu.pc === address) {
                    return;
                }

                // Check timeout within batch
                if (Date.now() - startTime >= timeoutMs) {
                    throw new Error(`Timeout waiting for address $${address.toString(16).toUpperCase()}`);
                }
            }

            // Report progress between batches
            if (progressCallback) {
                const cpu = await this.getCpuState();
                const state = await this.getState();
                progressCallback(state.cycleCount, cpu.pc);
            }
        }

        throw new Error(`Timeout waiting for address $${address.toString(16).toUpperCase()}`);
    }

    /**
     * Subscribe to server status events.
     * Returns a ServerStatusWatcher that emits events when the server status changes.
     *
     * @example
     * ```typescript
     * const watcher = client.watchServerStatus();
     *
     * watcher.on('ready', () => {
     *     console.log('Server is ready');
     * });
     *
     * watcher.on('shutdown', ({ graceMs, message }) => {
     *     console.log(`Server shutting down in ${graceMs}ms: ${message}`);
     *     // Clean up and disconnect
     * });
     *
     * watcher.on('end', () => {
     *     console.log('Status stream ended');
     * });
     *
     * // Later, to stop watching:
     * watcher.cancel();
     * ```
     */
    watchServerStatus(): ServerStatusWatcher {
        const watcher = new ServerStatusWatcher();
        const call = this.systemClient.WatchServerStatus({});
        watcher._setCall(call);
        return watcher;
    }

    /**
     * Wait for the server to be ready.
     * Convenience method that subscribes to status and waits for READY event.
     *
     * @param timeoutMs - Timeout in milliseconds (default: 5000)
     * @returns Promise that resolves when server is ready
     */
    async waitForReady(timeoutMs: number = 5000): Promise<void> {
        return new Promise((resolve, reject) => {
            const watcher = this.watchServerStatus();
            const timeout = setTimeout(() => {
                watcher.cancel();
                reject(new Error('Timeout waiting for server ready'));
            }, timeoutMs);

            watcher.on('ready', () => {
                clearTimeout(timeout);
                watcher.cancel();
                resolve();
            });

            watcher.on('error', (error: Error) => {
                clearTimeout(timeout);
                watcher.cancel();
                reject(error);
            });

            watcher.on('end', () => {
                clearTimeout(timeout);
                // Stream ended without ready event - might be normal
            });
        });
    }
}
