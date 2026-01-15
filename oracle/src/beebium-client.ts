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
import type { CpuState, ViaState, CrtcState, VideoUlaState, MachineState } from './types.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Path to proto file
const PROTO_PATH = join(__dirname, '../../src/service/proto/debugger.proto');

// Load proto definition
const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
    keepCase: false,
    longs: Number,
    enums: String,
    defaults: true,
    oneofs: true,
});

const protoDescriptor = grpc.loadPackageDefinition(packageDefinition) as any;
const beebium = protoDescriptor.beebium;

export interface StepResult {
    success: boolean;
    instructionsExecuted: number;
    cyclesExecuted: number;
    isRunning: boolean;
}

export class BeebiumClient {
    private client: any;
    private cycleCount: number = 0;

    constructor(target: string = 'localhost:50051') {
        this.client = new beebium.DebuggerControl(
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
     */
    async getCpuState(): Promise<CpuState> {
        const response = await this.call<any>('Get6502State', {});
        return {
            a: response.a,
            x: response.x,
            y: response.y,
            sp: response.sp,
            pc: response.pc,
            p: response.p,
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
        const [cpu, systemVia, userVia, crtc, videoUla] = await Promise.all([
            this.getCpuState(),
            this.getSystemViaState(),
            this.getUserViaState(),
            this.getCrtcState(),
            this.getVideoUlaState(),
        ]);

        return {
            cpu,
            systemVia,
            userVia,
            crtc,
            videoUla,
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
