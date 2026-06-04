/**
 * Client for the host-serial extension's typed config API.
 *
 * The host-serial extension bridges the BBC serial port to a host PTY or serial
 * device. This client queries and re-points that bridge (mode / path / baud)
 * programmatically -- the scripting-friendly equivalent of the GUI panel,
 * without the declarative ExtensionUi control tree. Requires the server to be
 * launched with --host-serial.
 */

import type {
    HostSerialClient,
    HostSerialConfig as ProtoHostSerialConfig,
} from "./generated/host_serial.js";
import { promisify } from "./call-utils.js";

export interface HostSerialConfig {
    /** "pty" or "device". */
    mode: string;
    /** pty slave path, or the opened device path. */
    path: string;
    /** Host line speed (device mode; informational for pty). */
    baud: number;
    /** Is the host port currently open? */
    serialOpen: boolean;
    /** OS error text when serialOpen is false. */
    openError: string;
}

/** Fields to change in setConfig(); omitted fields are kept (a partial update). */
export interface HostSerialSetConfigOptions {
    /** Only "device" is accepted at runtime; "pty" is rejected. */
    mode?: string;
    path?: string;
    baud?: number;
}

/** Query and re-point the host-serial bridge. */
export class HostSerial {
    private readonly stub: HostSerialClient;

    constructor(stub: HostSerialClient) {
        this.stub = stub;
    }

    /** Read the current bridge configuration and open state. */
    async getConfig(): Promise<HostSerialConfig> {
        const response = await promisify<{}, ProtoHostSerialConfig>(
            this.stub as unknown as Record<string, Function>,
            "getConfig",
            {},
        );
        return fromProto(response);
    }

    /**
     * Re-point the bridge. A partial update: only the fields you pass change;
     * the rest are kept. mode may only be "device" at runtime (a pty is created
     * only at startup). The re-point applies on the next emulation tick, so the
     * returned config may still show the previous device until it lands -- call
     * getConfig() to confirm. Never blocks.
     */
    async setConfig(options: HostSerialSetConfigOptions = {}): Promise<HostSerialConfig> {
        const request: { mode?: string; path?: string; baud?: number } = {};
        if (options.mode !== undefined) request.mode = options.mode;
        if (options.path !== undefined) request.path = options.path;
        if (options.baud !== undefined) request.baud = options.baud;
        const response = await promisify<typeof request, ProtoHostSerialConfig>(
            this.stub as unknown as Record<string, Function>,
            "setConfig",
            request,
        );
        return fromProto(response);
    }
}

function fromProto(response: ProtoHostSerialConfig): HostSerialConfig {
    return {
        mode: response.mode,
        path: response.path,
        baud: response.baud,
        serialOpen: response.serialOpen,
        openError: response.openError,
    };
}
