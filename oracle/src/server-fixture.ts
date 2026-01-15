/**
 * Beebium Server Test Fixture
 *
 * Manages the lifecycle of a Beebium server process for integration tests.
 */

import { spawn, type ChildProcess } from 'child_process';
import { BeebiumClient } from './beebium-client.js';

export interface ServerFixtureOptions {
    /** Path to beebium-model-b executable */
    executablePath?: string;
    /** gRPC port (default: auto-select available port) */
    port?: number;
    /** Model type: 'B' or 'B+' (default: 'B') */
    model?: 'B' | 'B+';
    /** Additional command-line arguments */
    args?: string[];
    /** Connection timeout in ms (default: 5000) */
    timeout?: number;
}

const DEFAULT_OPTIONS: Required<Omit<ServerFixtureOptions, 'executablePath' | 'args'>> = {
    port: 50051,
    model: 'B',
    timeout: 5000,
};

/**
 * Finds the Beebium server executable.
 */
function findExecutable(model: 'B' | 'B+'): string {
    // Try common build locations
    const candidates = [
        // Relative to oracle directory
        `../build/src/server/beebium-model-b${model === 'B+' ? '-plus' : ''}`,
        // Absolute path for the project
        `/Users/rjs/Code/beebium/build/src/server/beebium-model-b${model === 'B+' ? '-plus' : ''}`,
    ];

    // For now, just return the absolute path
    // In production, you'd check if the file exists
    return candidates[1];
}

export class ServerFixture {
    private process: ChildProcess | null = null;
    private client: BeebiumClient | null = null;
    private readonly port: number;
    private readonly model: 'B' | 'B+';
    private readonly timeout: number;
    private readonly args: string[];
    private readonly executablePath: string;

    constructor(options: ServerFixtureOptions = {}) {
        this.port = options.port ?? DEFAULT_OPTIONS.port;
        this.model = options.model ?? DEFAULT_OPTIONS.model;
        this.timeout = options.timeout ?? DEFAULT_OPTIONS.timeout;
        // Always use --wait=grpc for controlled startup
        this.args = ['--wait=grpc', ...(options.args ?? [])];
        this.executablePath = options.executablePath ?? findExecutable(this.model);
    }

    /**
     * Start the server and wait for it to be ready.
     * @returns Connected BeebiumClient
     */
    async start(): Promise<BeebiumClient> {
        if (this.process) {
            throw new Error('Server already running');
        }

        const args = [
            '--port', String(this.port),
            ...this.args,
        ];

        return new Promise((resolve, reject) => {
            const proc = spawn(this.executablePath, args, {
                stdio: ['ignore', 'pipe', 'pipe'],
            });

            this.process = proc;

            // Collect stderr for error reporting
            let stderr = '';
            proc.stderr?.on('data', (data: Buffer) => {
                stderr += data.toString();
            });

            // Handle early exit (failed to start)
            proc.on('error', (err) => {
                this.process = null;
                reject(new Error(`Failed to start server: ${err.message}`));
            });

            proc.on('exit', (code) => {
                if (code !== 0 && this.process) {
                    this.process = null;
                    reject(new Error(`Server exited with code ${code}: ${stderr}`));
                }
            });

            // Wait for server to be ready by polling the gRPC connection
            const startTime = Date.now();
            const tryConnect = async () => {
                try {
                    const client = BeebiumClient.connect(`localhost:${this.port}`);
                    await client.getState();
                    this.client = client;
                    resolve(client);
                } catch {
                    if (Date.now() - startTime > this.timeout) {
                        await this.stop();
                        reject(new Error(`Server did not become ready within ${this.timeout}ms`));
                    } else if (this.process) {
                        setTimeout(tryConnect, 100);
                    }
                }
            };

            // Give server a moment to start
            setTimeout(tryConnect, 200);
        });
    }

    /**
     * Stop the server and clean up.
     */
    async stop(): Promise<void> {
        if (this.client) {
            this.client.close();
            this.client = null;
        }

        if (this.process) {
            const proc = this.process;
            this.process = null;

            return new Promise((resolve) => {
                proc.on('exit', () => resolve());
                proc.kill('SIGTERM');

                // Force kill after 2 seconds if SIGTERM doesn't work
                setTimeout(() => {
                    if (!proc.killed) {
                        proc.kill('SIGKILL');
                    }
                    resolve();
                }, 2000);
            });
        }
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
        return this.process !== null;
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
