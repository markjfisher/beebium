/**
 * Main Beebium client class for the TypeScript client.
 *
 * Provides a unified facade for interacting with a beebium emulator
 * instance, either by connecting to an existing server or by launching
 * and managing a server subprocess.
 */

import { randomUUID } from "node:crypto";
import { Connection } from "./connection.js";
import { ServerProcess, type ServerProcessOptions } from "./server-process.js";
import { Debugger } from "./debugger.js";
import { CPU } from "./cpu.js";
import { Memory } from "./memory.js";
import { Keyboard } from "./keyboard.js";
import { Video } from "./video.js";
import { System } from "./system.js";
import { Disc } from "./disc.js";
import { Econet } from "./econet.js";
import { Tube } from "./tube.js";
import { Via, ViaId } from "./via.js";
import { Crtc } from "./crtc.js";
import { VideoUla } from "./video-ula.js";
import { AddressableLatch } from "./latch.js";
import { Sound } from "./sound.js";
import { TubeUlaInspection } from "./tube-ula.js";
import { Basic } from "./basic.js";
import { ConnectionError } from "./exceptions.js";

/** Default gRPC port for the Beebium server (0xBEEB). */
const DEFAULT_GRPC_PORT = 0xBEEB;

/**
 * Main client for interacting with a Beebium emulator instance.
 *
 * Can either connect to an existing server or manage its own server process.
 *
 * Usage:
 *     // Connect to existing server
 *     const bbc = await Beebium.connect();
 *     try {
 *         await bbc.debugger.stop();
 *         await bbc.memory.address.bus.writeByte(0x1000, 0x42);
 *     } finally {
 *         await bbc.close();
 *     }
 *
 *     // Launch and manage server
 *     const bbc = await Beebium.launch({ model: "B" });
 *     try {
 *         await bbc.keyboard.type("PRINT 42\r");
 *     } finally {
 *         await bbc.close();
 *     }
 *
 *     // Using async dispose
 *     await using bbc = await Beebium.launch({ model: "B" });
 *     await bbc.keyboard.type("PRINT 42\r");
 */
export class Beebium {
    private connection: Connection;
    private server: ServerProcess | null;
    private instanceUuid: string;

    // Lazy subsystem caches
    private _debugger?: Debugger;
    private _cpu?: CPU;
    private _memory?: Memory;
    private _keyboard?: Keyboard;
    private _video?: Video;
    private _system?: System;
    private _disc?: Disc;
    private _econet?: Econet;
    private _tube?: Tube;
    private _systemVia?: Via;
    private _userVia?: Via;
    private _crtc?: Crtc;
    private _videoUla?: VideoUla;
    private _addressableLatch?: AddressableLatch;
    private _sound?: Sound;
    private _tubeUla?: TubeUlaInspection;
    private _basic?: Basic;

    private constructor(
        connection: Connection,
        server?: ServerProcess,
        instanceUuid?: string,
    ) {
        this.connection = connection;
        this.server = server ?? null;
        this.instanceUuid = instanceUuid ?? randomUUID();
    }

    /**
     * Connect to an already-running beebium-server.
     *
     * @param target - The gRPC target string (e.g. "localhost:48875").
     *     Defaults to localhost on DEFAULT_GRPC_PORT (0xBEEB = 48875).
     * @param timeoutMs - Connection timeout in milliseconds (default 5000).
     * @returns A connected Beebium client.
     * @throws TimeoutError if the channel does not become ready in time.
     */
    static async connect(target?: string, timeoutMs = 5000): Promise<Beebium> {
        const effectiveTarget = target ?? `localhost:${DEFAULT_GRPC_PORT}`;
        const connection = new Connection(effectiveTarget);
        await connection.waitForReady(timeoutMs);
        return new Beebium(connection);
    }

    /**
     * Launch a beebium-server subprocess and connect to it.
     *
     * The server is managed by this client instance. Calling close()
     * will stop the server process.
     *
     * @param options - Server process options (model, args, etc.) plus
     *     an optional timeoutMs for the connection (default 5000).
     * @returns A connected Beebium client managing the server process.
     * @throws ServerStartupError if the server fails to start.
     * @throws TimeoutError if the connection cannot be established.
     */
    static async launch(
        options: ServerProcessOptions & { timeoutMs?: number },
    ): Promise<Beebium> {
        const { timeoutMs = 5000, ...serverOptions } = options;
        const server = new ServerProcess(serverOptions);
        await server.start(serverOptions.timeout ?? timeoutMs);
        const connection = new Connection(server.target);
        await connection.waitForReady(timeoutMs);
        return new Beebium(connection, server, server.provenanceUuid);
    }

    /** The gRPC target string. */
    get target(): string {
        return this.connection.target;
    }

    // =========================================================================
    // Lazy subsystem properties
    // =========================================================================

    /** Access debugger control (execution, breakpoints). */
    get debugger(): Debugger {
        if (this._debugger === undefined) {
            this._debugger = new Debugger(this.connection.debuggerStub);
        }
        return this._debugger;
    }

    /** Access 6502 CPU registers. */
    get cpu(): CPU {
        if (this._cpu === undefined) {
            this._cpu = new CPU(this.connection.debuggerStub);
        }
        return this._cpu;
    }

    /** Access memory read/write. */
    get memory(): Memory {
        if (this._memory === undefined) {
            this._memory = new Memory(this.connection.debuggerStub);
        }
        return this._memory;
    }

    /** Access keyboard input. */
    get keyboard(): Keyboard {
        if (this._keyboard === undefined) {
            this._keyboard = new Keyboard(this.connection.keyboardStub);
        }
        return this._keyboard;
    }

    /** Access video frame streaming. */
    get video(): Video {
        if (this._video === undefined) {
            this._video = new Video(this.connection.videoStub);
        }
        return this._video;
    }

    /** Access system information and server status. */
    get system(): System {
        if (this._system === undefined) {
            this._system = new System(this.connection.systemStub, this.instanceUuid);
        }
        return this._system;
    }

    /** Access floppy disc drive management. */
    get disc(): Disc {
        if (this._disc === undefined) {
            this._disc = new Disc(this.connection.discStub);
        }
        return this._disc;
    }

    /** Access Econet networking management. */
    get econet(): Econet {
        if (this._econet === undefined) {
            this._econet = new Econet(this.connection.econetStub);
        }
        return this._econet;
    }

    /** Access Tube coprocessor management. */
    get tube(): Tube {
        if (this._tube === undefined) {
            this._tube = new Tube(this.connection.tubeStub);
        }
        return this._tube;
    }

    /** Access System VIA (6522) state. */
    get systemVia(): Via {
        if (this._systemVia === undefined) {
            this._systemVia = new Via(this.connection.deviceInspectionStub, ViaId.SYSTEM);
        }
        return this._systemVia;
    }

    /** Access User VIA (6522) state. */
    get userVia(): Via {
        if (this._userVia === undefined) {
            this._userVia = new Via(this.connection.deviceInspectionStub, ViaId.USER);
        }
        return this._userVia;
    }

    /** Access CRTC (6845) state. */
    get crtc(): Crtc {
        if (this._crtc === undefined) {
            this._crtc = new Crtc(this.connection.deviceInspectionStub);
        }
        return this._crtc;
    }

    /** Access Video ULA state. */
    get videoUla(): VideoUla {
        if (this._videoUla === undefined) {
            this._videoUla = new VideoUla(this.connection.deviceInspectionStub);
        }
        return this._videoUla;
    }

    /** Access addressable latch (IC32) state. */
    get addressableLatch(): AddressableLatch {
        if (this._addressableLatch === undefined) {
            this._addressableLatch = new AddressableLatch(this.connection.deviceInspectionStub);
        }
        return this._addressableLatch;
    }

    /** Access SN76489 sound chip state. */
    get sound(): Sound {
        if (this._sound === undefined) {
            this._sound = new Sound(this.connection.deviceInspectionStub);
        }
        return this._sound;
    }

    /** Access Tube ULA device state (side-effect-free inspection). */
    get tubeUla(): TubeUlaInspection {
        if (this._tubeUla === undefined) {
            this._tubeUla = new TubeUlaInspection(this.connection.deviceInspectionStub);
        }
        return this._tubeUla;
    }

    /** Access BBC BASIC workflow helpers. */
    get basic(): Basic {
        if (this._basic === undefined) {
            this._basic = new Basic(this);
        }
        return this._basic;
    }

    /**
     * Connect to the parasite's gRPC server.
     *
     * Queries the host's Tube status for the parasite's gRPC address,
     * then returns a new Beebium instance connected to the parasite.
     *
     * @param timeoutMs - Connection timeout in milliseconds (default 5000).
     * @returns A Beebium client connected to the parasite's gRPC server.
     * @throws ConnectionError if the parasite is not connected or the
     *     connection cannot be established.
     */
    async connectParasite(timeoutMs = 5000): Promise<Beebium> {
        const status = await this.tube.getStatus();
        if (!status.parasiteConnected) {
            throw new ConnectionError("No parasite is connected");
        }
        const address = status.parasiteGrpcAddress;
        if (!address) {
            throw new ConnectionError(
                "Parasite has not registered its gRPC endpoint",
            );
        }
        return Beebium.connect(address, timeoutMs);
    }

    // =========================================================================
    // Emulated time helpers
    // =========================================================================

    /**
     * Run the emulator for the specified number of emulated seconds.
     *
     * Computes the cycle count from the clock speed and steps that many
     * cycles synchronously. The emulator must be stopped on entry and
     * is left stopped on return.
     *
     * @param seconds - Number of emulated BBC-time seconds to run.
     */
    async runForEmulatedSeconds(seconds: number): Promise<void> {
        const clockHz = await this.system.getClockSpeedHz() || 2_000_000;
        const cycles = Math.round(seconds * clockHz);
        if (await this.debugger.isRunning()) {
            await this.debugger.stop();
        }
        await this.debugger.stepCycles(cycles);
    }

    /**
     * Run the emulator until predicate returns true or the emulated time
     * budget expires.
     *
     * The predicate is called periodically (after at least 1 emulated
     * second to allow keyboard input and Tube protocol processing).
     * The emulator is stopped during predicate evaluation and left
     * stopped on return.
     *
     * @param predicate - Async function returning true when the condition is met.
     * @param emulatedSeconds - Maximum emulated time to run.
     * @param pollIntervalMs - Real-time ms between polls (default 20).
     * @returns true if the predicate was satisfied, false on timeout.
     */
    async runUntilOrTimeout(
        predicate: () => Promise<boolean>,
        emulatedSeconds: number,
        pollIntervalMs = 20,
    ): Promise<boolean> {
        const clockHz = await this.system.getClockSpeedHz() || 2_000_000;
        const cycleBudget = Math.round(emulatedSeconds * clockHz);
        const startCycles = (await this.debugger.getState()).cycleCount;
        const targetCycles = startCycles + cycleBudget;
        const minCyclesBeforeCheck = clockHz; // 1 emulated second

        if (await this.debugger.isStopped()) {
            await this.debugger.run();
        }
        try {
            while (true) {
                await new Promise(r => setTimeout(r, pollIntervalMs));
                const state = await this.debugger.getState();

                if (state.cycleCount >= targetCycles) {
                    await this.debugger.stop();
                    return predicate();
                }

                if (state.cycleCount - startCycles >= minCyclesBeforeCheck) {
                    await this.debugger.stop();
                    if (await predicate()) {
                        return true;
                    }
                    await this.debugger.run();
                }
            }
        } finally {
            if (await this.debugger.isRunning()) {
                await this.debugger.stop();
            }
        }
    }

    /**
     * Close the connection and stop any managed server process.
     *
     * For managed servers (created via launch()), this first requests
     * graceful shutdown via RPC before stopping the process.
     */
    async close(): Promise<void> {
        // Try graceful RPC shutdown first if we're managing a server
        if (this.server !== null && this.server.isRunning) {
            try {
                const { ShutdownMode } = await import("./system.js");
                const response = await this.system.requestShutdown(
                    ShutdownMode.GRACEFUL,
                );
                if (response.accepted) {
                    await new Promise((r) => setTimeout(r, 100));
                }
            } catch {
                // If RPC fails, fall back to SIGTERM via stop()
            }
        }

        this.connection.close();

        if (this.server !== null) {
            await this.server.stop();
            this.server = null;
        }
    }

    /** Async dispose support (using `await using`). */
    async [Symbol.asyncDispose](): Promise<void> {
        await this.close();
    }
}
