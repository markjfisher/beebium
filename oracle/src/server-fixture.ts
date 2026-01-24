/**
 * Beebium Server Test Fixture
 *
 * Manages the lifecycle of a Beebium server process for integration tests.
 * Uses --port 0 for dynamic port allocation and parses the actual port from stdout.
 */

import { spawn, type ChildProcess } from 'child_process';
import { randomUUID } from 'crypto';
import { BeebiumClient } from './beebium-client.js';

/** Oracle package version - used as provenance version */
const ORACLE_VERSION = '0.1.0';

// Track all spawned processes for cleanup on exit
const activeProcesses = new Set<ChildProcess>();

// Register cleanup handlers once
let cleanupRegistered = false;
function registerCleanupHandlers() {
    if (cleanupRegistered) return;
    cleanupRegistered = true;

    const cleanup = () => {
        for (const proc of activeProcesses) {
            try {
                proc.kill('SIGKILL');
            } catch {
                // Ignore errors
            }
        }
        activeProcesses.clear();
    };

    process.on('exit', cleanup);
    process.on('SIGINT', () => { cleanup(); process.exit(130); });
    process.on('SIGTERM', () => { cleanup(); process.exit(143); });
    process.on('uncaughtException', (err) => {
        console.error('Uncaught exception:', err);
        cleanup();
        process.exit(1);
    });
}

export interface RomSlot {
    /** Sideways ROM slot number (0-15) */
    slot: number;
    /** ROM filename (resolved relative to ROM directory) or absolute path */
    filepath: string;
}

/**
 * Sideways slot configuration for B-RomRam model.
 * Matches the --sideways CLI format: slot:type[:filepath]
 */
export interface SidewaysConfig {
    /** Slot number (0-15) */
    slot: number;
    /** Slot type: 'rom', 'ram', or 'empty' */
    type: 'rom' | 'ram' | 'empty';
    /** Filepath for ROM, or optional pre-load content for RAM */
    filepath?: string;
}

export interface ServerFixtureOptions {
    /** Path to beebium-model-b executable */
    executablePath?: string;
    /** Model type: 'B', 'B+', or 'B-RomRam' (default: 'B') */
    model?: 'B' | 'B+' | 'B-RomRam';
    /** Disc controller to install (Model B and B-RomRam only): 'acorn-1770' or 'none' */
    fdc?: 'acorn-1770' | 'none';
    /** Sideways slot configuration (B-RomRam only) */
    sideways?: SidewaysConfig[];
    /** Legacy sideways ROMs to load (use sideways for B-RomRam) */
    roms?: RomSlot[];
    /** Additional command-line arguments */
    args?: string[];
    /** Connection timeout in ms (default: 5000) */
    timeout?: number;
    /** Override provenance type (default: 'typescript-oracle') */
    provenanceType?: string;
    /** Override provenance version (default: ORACLE_VERSION) */
    provenanceVersion?: string;
}

const DEFAULT_OPTIONS: Required<Omit<ServerFixtureOptions, 'executablePath' | 'args' | 'fdc' | 'roms' | 'sideways'>> = {
    model: 'B',
    timeout: 5000,
    provenanceType: 'typescript-oracle',
    provenanceVersion: ORACLE_VERSION,
};

/** Regex to parse "Listening on port <N>" from server stdout */
const PORT_REGEX = /Listening on port (\d+)/;

/**
 * Finds the Beebium server executable.
 */
function findExecutable(model: 'B' | 'B+' | 'B-RomRam'): string {
    const suffix = model === 'B+' ? '-plus' : model === 'B-RomRam' ? '-romram' : '';
    return `/Users/rjs/Code/beebium/build/src/server/beebium-model-b${suffix}`;
}

/**
 * Kill a process and all its children forcefully.
 */
function killProcess(proc: ChildProcess): void {
    if (!proc.pid) return;

    try {
        // Try SIGTERM first
        proc.kill('SIGTERM');
    } catch {
        // Process may already be dead
    }

    // Schedule a forceful kill if SIGTERM doesn't work
    setTimeout(() => {
        try {
            if (!proc.killed) {
                proc.kill('SIGKILL');
            }
        } catch {
            // Process may already be dead
        }
    }, 500);
}

/**
 * Wait for a process to exit with timeout.
 */
function waitForExit(proc: ChildProcess, timeoutMs: number): Promise<void> {
    return new Promise((resolve) => {
        if (proc.exitCode !== null || proc.killed) {
            resolve();
            return;
        }

        const timeout = setTimeout(() => {
            resolve(); // Don't block forever
        }, timeoutMs);

        proc.once('exit', () => {
            clearTimeout(timeout);
            resolve();
        });
    });
}

export class ServerFixture {
    private process: ChildProcess | null = null;
    private client: BeebiumClient | null = null;
    private port: number = 0;
    private readonly model: 'B' | 'B+' | 'B-RomRam';
    private readonly timeout: number;
    private readonly fdc?: 'acorn-1770' | 'none';
    private readonly sideways: SidewaysConfig[];
    private readonly roms: RomSlot[];
    private readonly extraArgs: string[];
    private readonly executablePath: string;
    private readonly provenanceType: string;
    private readonly provenanceVersion: string;
    private readonly provenanceUuid: string;

    constructor(options: ServerFixtureOptions = {}) {
        this.model = options.model ?? DEFAULT_OPTIONS.model;
        this.timeout = options.timeout ?? DEFAULT_OPTIONS.timeout;
        this.fdc = options.fdc;
        this.sideways = options.sideways ?? [];
        this.roms = options.roms ?? [];
        this.extraArgs = options.args ?? [];
        this.executablePath = options.executablePath ?? findExecutable(this.model);
        this.provenanceType = options.provenanceType ?? DEFAULT_OPTIONS.provenanceType;
        this.provenanceVersion = options.provenanceVersion ?? DEFAULT_OPTIONS.provenanceVersion;
        this.provenanceUuid = randomUUID();

        // Ensure cleanup handlers are registered
        registerCleanupHandlers();
    }

    /**
     * Build command-line arguments for the server.
     */
    private buildArgs(): string[] {
        const args: string[] = [
            '--port', '0',      // Dynamic port allocation
            '--wait=api',       // Controlled startup via gRPC
        ];

        // Disc controller (Model B and B-RomRam have socket)
        if (this.fdc) {
            args.push('--fdc', this.fdc);
        }

        // Sideways slot configuration (B-RomRam model)
        for (const slot of this.sideways) {
            if (slot.type === 'empty') {
                args.push('--sideways', `${slot.slot}:empty`);
            } else if (slot.type === 'ram') {
                args.push('--sideways', slot.filepath
                    ? `${slot.slot}:ram:${slot.filepath}`
                    : `${slot.slot}:ram`);
            } else {
                // ROM requires a filepath
                args.push('--sideways', `${slot.slot}:rom:${slot.filepath}`);
            }
        }

        // Legacy sideways ROMs (Model B)
        for (const rom of this.roms) {
            args.push('--rom', `${rom.slot}:${rom.filepath}`);
        }

        // Provenance flags
        args.push(
            '--provenance-type', this.provenanceType,
            '--provenance-uuid', this.provenanceUuid,
            '--provenance-version', this.provenanceVersion
        );

        // Extra user-provided args
        args.push(...this.extraArgs);

        return args;
    }

    /**
     * Start the server and wait for it to be ready.
     * Uses --port 0 for dynamic port allocation and parses the actual port from stdout.
     * @returns Connected BeebiumClient
     */
    async start(): Promise<BeebiumClient> {
        if (this.process) {
            throw new Error('Server already running');
        }

        const args = this.buildArgs();

        // Server needs to run from the beebium directory to find ROMs
        const cwd = this.executablePath.includes('beebium')
            ? this.executablePath.substring(0, this.executablePath.indexOf('/build'))
            : process.cwd();

        return new Promise((resolve, reject) => {
            const proc = spawn(this.executablePath, args, {
                stdio: ['ignore', 'pipe', 'pipe'],
                cwd,
                detached: false, // Don't create process group
            });

            this.process = proc;
            activeProcesses.add(proc);

            // Remove from tracking when process exits
            proc.on('exit', () => {
                activeProcesses.delete(proc);
            });

            // Collect stderr for error reporting
            let stderr = '';
            proc.stderr?.on('data', (data: Buffer) => {
                stderr += data.toString();
            });

            // Handle spawn errors
            proc.on('error', (err) => {
                this.process = null;
                reject(new Error(`Failed to start server: ${err.message}`));
            });

            // Handle early exit (failed to start)
            proc.on('exit', (code, signal) => {
                if (this.process === proc) {
                    this.process = null;
                    if (code !== 0) {
                        reject(new Error(`Server exited with code ${code}, signal ${signal}: ${stderr}`));
                    }
                }
            });

            // Set up connection timeout
            const connectionTimeout = setTimeout(() => {
                if (this.process === proc) {
                    killProcess(proc);
                    this.process = null;
                    reject(new Error(`Server did not become ready within ${this.timeout}ms`));
                }
            }, this.timeout);

            // Parse stdout for "Listening on port <N>" to discover the allocated port
            let stdout = '';
            let portFound = false;
            proc.stdout?.on('data', (data: Buffer) => {
                stdout += data.toString();

                // Check if we've received the port announcement
                if (!portFound) {
                    const match = stdout.match(PORT_REGEX);
                    if (match) {
                        portFound = true;
                        this.port = parseInt(match[1], 10);

                        // Now we know the port, try to connect
                        const tryConnect = async () => {
                            if (this.process !== proc) {
                                return;
                            }

                            try {
                                const client = BeebiumClient.connect(`localhost:${this.port}`);
                                await client.getState();
                                clearTimeout(connectionTimeout);
                                this.client = client;
                                resolve(client);
                            } catch {
                                // Server not ready yet, retry
                                if (this.process === proc) {
                                    setTimeout(tryConnect, 50);
                                }
                            }
                        };

                        tryConnect();
                    }
                }
            });
        });
    }

    /**
     * Stop the server and clean up.
     * Uses aggressive cleanup with timeouts to prevent hanging.
     */
    async stop(): Promise<void> {
        // Close client first
        if (this.client) {
            try {
                this.client.close();
            } catch {
                // Ignore errors closing client
            }
            this.client = null;
        }

        // Kill server process
        if (this.process) {
            const proc = this.process;
            this.process = null;

            killProcess(proc);

            // Wait for exit with timeout
            await waitForExit(proc, 2000);

            // Force kill if still running
            if (!proc.killed && proc.exitCode === null) {
                try {
                    proc.kill('SIGKILL');
                } catch {
                    // Already dead
                }
            }
        }

        this.port = 0;
    }

    /**
     * Get the connected client (must call start() first).
     */
    getClient(): BeebiumClient {
        if (!this.client) {
            throw new Error('Server not started');
        }
        return this.client;
    }

    /**
     * Get the port the server is running on.
     */
    getPort(): number {
        return this.port;
    }

    /**
     * Check if the server is running.
     */
    isRunning(): boolean {
        return this.process !== null && this.process.exitCode === null;
    }
}

/**
 * Create a fixture that starts before tests and stops after.
 * For use with vitest's beforeAll/afterAll.
 */
export function createServerFixture(options?: ServerFixtureOptions): {
    fixture: ServerFixture;
    setup: () => Promise<BeebiumClient>;
    teardown: () => Promise<void>;
} {
    const fixture = new ServerFixture(options);

    return {
        fixture,
        setup: () => fixture.start(),
        teardown: () => fixture.stop(),
    };
}
