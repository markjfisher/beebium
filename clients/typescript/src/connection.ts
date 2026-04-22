/**
 * gRPC connection management for the Beebium client.
 */

import { ChannelCredentials } from "@grpc/grpc-js";
import { ConnectionError, TimeoutError } from "./exceptions.js";

import { DebuggerControlClient, ParasiteDebuggerControlClient } from "./generated/debugger.js";
import { DeviceInspectionClient } from "./generated/debugger.js";
import { SystemServiceClient } from "./generated/system.js";
import { KeyboardServiceClient } from "./generated/keyboard.js";
import { VideoServiceClient } from "./generated/video.js";
import { DiscServiceClient } from "./generated/disc.js";
import { EconetServiceClient } from "./generated/econet.js";
import { TubeServiceClient } from "./generated/tube.js";
import { AunServiceClient } from "./generated/aun.js";
import { PiconetServiceClient } from "./generated/piconet_service.js";
import { EconetTransportServiceClient } from "./generated/econet_transport.js";
import { ExtensionUiServiceClient } from "./generated/extension_ui.js";

import type { DebuggerControlClient as DebuggerControlClientType } from "./generated/debugger.js";
import type { ParasiteDebuggerControlClient as ParasiteDebuggerControlClientType } from "./generated/debugger.js";
import type { DeviceInspectionClient as DeviceInspectionClientType } from "./generated/debugger.js";
import type { SystemServiceClient as SystemServiceClientType } from "./generated/system.js";
import type { KeyboardServiceClient as KeyboardServiceClientType } from "./generated/keyboard.js";
import type { VideoServiceClient as VideoServiceClientType } from "./generated/video.js";
import type { DiscServiceClient as DiscServiceClientType } from "./generated/disc.js";
import type { EconetServiceClient as EconetServiceClientType } from "./generated/econet.js";
import type { TubeServiceClient as TubeServiceClientType } from "./generated/tube.js";
import type { AunServiceClient as AunServiceClientType } from "./generated/aun.js";
import type { PiconetServiceClient as PiconetServiceClientType } from "./generated/piconet_service.js";
import type { EconetTransportServiceClient as EconetTransportServiceClientType } from "./generated/econet_transport.js";
import type { ExtensionUiServiceClient as ExtensionUiServiceClientType } from "./generated/extension_ui.js";

/**
 * Manages a gRPC connection to a Beebium emulator server.
 *
 * Service stubs are created lazily on first access, all sharing the same
 * underlying channel.
 */
export class Connection {
    private readonly _target: string;
    private readonly credentials: ChannelCredentials;
    private _closed = false;

    private _debuggerStub: DebuggerControlClientType | null = null;
    private _parasiteDebuggerStub: ParasiteDebuggerControlClientType | null = null;
    private _deviceInspectionStub: DeviceInspectionClientType | null = null;
    private _systemStub: SystemServiceClientType | null = null;
    private _keyboardStub: KeyboardServiceClientType | null = null;
    private _videoStub: VideoServiceClientType | null = null;
    private _discStub: DiscServiceClientType | null = null;
    private _econetStub: EconetServiceClientType | null = null;
    private _tubeStub: TubeServiceClientType | null = null;
    private _aunStub: AunServiceClientType | null = null;
    private _piconetStub: PiconetServiceClientType | null = null;
    private _econetTransportStub: EconetTransportServiceClientType | null = null;
    private _extensionUiStub: ExtensionUiServiceClientType | null = null;

    constructor(target: string) {
        this._target = target;
        this.credentials = ChannelCredentials.createInsecure();
    }

    /** The target address string (e.g. "localhost:50051"). */
    get target(): string {
        return this._target;
    }

    /** Whether the connection has not been closed. */
    get isConnected(): boolean {
        return !this._closed;
    }

    private ensureOpen(): void {
        if (this._closed) {
            throw new ConnectionError("Connection is closed");
        }
    }

    // -- Lazy stub accessors --------------------------------------------------

    get debuggerStub(): DebuggerControlClientType {
        this.ensureOpen();
        if (!this._debuggerStub) {
            this._debuggerStub = new DebuggerControlClient(
                this._target,
                this.credentials,
            );
        }
        return this._debuggerStub;
    }

    get parasiteDebuggerStub(): ParasiteDebuggerControlClientType {
        this.ensureOpen();
        if (!this._parasiteDebuggerStub) {
            this._parasiteDebuggerStub = new ParasiteDebuggerControlClient(
                this._target,
                this.credentials,
            );
        }
        return this._parasiteDebuggerStub;
    }

    get deviceInspectionStub(): DeviceInspectionClientType {
        this.ensureOpen();
        if (!this._deviceInspectionStub) {
            this._deviceInspectionStub = new DeviceInspectionClient(
                this._target,
                this.credentials,
            );
        }
        return this._deviceInspectionStub;
    }

    get systemStub(): SystemServiceClientType {
        this.ensureOpen();
        if (!this._systemStub) {
            this._systemStub = new SystemServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._systemStub;
    }

    get keyboardStub(): KeyboardServiceClientType {
        this.ensureOpen();
        if (!this._keyboardStub) {
            this._keyboardStub = new KeyboardServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._keyboardStub;
    }

    get videoStub(): VideoServiceClientType {
        this.ensureOpen();
        if (!this._videoStub) {
            this._videoStub = new VideoServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._videoStub;
    }

    get discStub(): DiscServiceClientType {
        this.ensureOpen();
        if (!this._discStub) {
            this._discStub = new DiscServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._discStub;
    }

    get econetStub(): EconetServiceClientType {
        this.ensureOpen();
        if (!this._econetStub) {
            this._econetStub = new EconetServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._econetStub;
    }

    get tubeStub(): TubeServiceClientType {
        this.ensureOpen();
        if (!this._tubeStub) {
            this._tubeStub = new TubeServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._tubeStub;
    }

    get aunStub(): AunServiceClientType {
        this.ensureOpen();
        if (!this._aunStub) {
            this._aunStub = new AunServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._aunStub;
    }

    get piconetStub(): PiconetServiceClientType {
        this.ensureOpen();
        if (!this._piconetStub) {
            this._piconetStub = new PiconetServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._piconetStub;
    }

    get econetTransportStub(): EconetTransportServiceClientType {
        this.ensureOpen();
        if (!this._econetTransportStub) {
            this._econetTransportStub = new EconetTransportServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._econetTransportStub;
    }

    get extensionUiStub(): ExtensionUiServiceClientType {
        this.ensureOpen();
        if (!this._extensionUiStub) {
            this._extensionUiStub = new ExtensionUiServiceClient(
                this._target,
                this.credentials,
            );
        }
        return this._extensionUiStub;
    }

    /**
     * Wait until the underlying channel is ready, or until the timeout expires.
     *
     * This forces creation of the system stub (and thus the channel) then
     * uses the client's built-in waitForReady mechanism.
     *
     * @param timeoutMs - Maximum time to wait in milliseconds.
     * @throws TimeoutError if the channel does not become ready in time.
     * @throws ConnectionError if the connection has been closed.
     */
    async waitForReady(timeoutMs: number): Promise<void> {
        this.ensureOpen();

        const client = this.systemStub;
        const deadline = Date.now() + timeoutMs;

        return new Promise<void>((resolve, reject) => {
            client.waitForReady(deadline, (error?: Error) => {
                if (error) {
                    reject(
                        new TimeoutError(
                            `Channel to ${this._target} did not become ready within ${timeoutMs}ms`,
                        ),
                    );
                } else {
                    resolve();
                }
            });
        });
    }

    /**
     * Close the connection and all stubs. After calling close(), stub
     * accessors will throw ConnectionError.
     */
    close(): void {
        if (this._closed) {
            return;
        }
        this._closed = true;

        const stubs = [
            this._debuggerStub,
            this._parasiteDebuggerStub,
            this._deviceInspectionStub,
            this._systemStub,
            this._keyboardStub,
            this._videoStub,
            this._discStub,
            this._econetStub,
            this._tubeStub,
            this._aunStub,
            this._piconetStub,
            this._econetTransportStub,
            this._extensionUiStub,
        ];

        for (const stub of stubs) {
            if (stub) {
                stub.close();
            }
        }

        this._debuggerStub = null;
        this._parasiteDebuggerStub = null;
        this._deviceInspectionStub = null;
        this._systemStub = null;
        this._keyboardStub = null;
        this._videoStub = null;
        this._discStub = null;
        this._econetStub = null;
        this._tubeStub = null;
        this._aunStub = null;
        this._piconetStub = null;
        this._econetTransportStub = null;
        this._extensionUiStub = null;
    }
}
