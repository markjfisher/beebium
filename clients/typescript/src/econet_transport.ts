/**
 * Econet transport discovery (which transport is active on the server).
 *
 * Wraps EconetTransportService. Use this to decide whether to drive
 * bbc.aun or bbc.piconet (or any future transport-specific service):
 * each transport extension has a canonical name ("aun", "piconet",
 * ...) which maps one-to-one with the corresponding service stub.
 */

import type {
    EconetTransportServiceClient,
    ListTransportsResponse as ProtoListTransportsResponse,
    GetActiveTransportResponse as ProtoGetActiveTransportResponse,
} from "./generated/econet_transport.js";
import { promisify } from "./call-utils.js";

export interface TransportInfo {
    /**
     * Canonical extension name -- "aun", "piconet", etc. Maps to the
     * transport-specific gRPC service the client should use.
     */
    name: string;

    /** Human-readable description from the extension manifest. */
    description: string;

    /**
     * True if this transport is the one currently producing the
     * backend behind EconetSocket on the server.
     */
    active: boolean;
}

/**
 * Discover which Econet transport extension is active.
 *
 * Usage:
 *     const active = await bbc.transport.getActive();
 *     if (active === undefined) {
 *         console.log("No Econet transport configured");
 *     } else if (active.name === "aun") {
 *         await bbc.aun.addPeer(0, 254, "192.168.1.10");
 *     } else if (active.name === "piconet") {
 *         console.log((await bbc.piconet.getStatus()).devicePath);
 *     }
 */
export class EconetTransport {
    private readonly stub: EconetTransportServiceClient;

    constructor(stub: EconetTransportServiceClient) {
        this.stub = stub;
    }

    /**
     * List all econet transports the server knows about.
     *
     * Returns one entry per loaded transport extension. The active
     * field is true for whichever one is currently producing the
     * wire backend; for BBC machine variants at most one will be
     * active.
     */
    async list(): Promise<TransportInfo[]> {
        const response = await promisify<{}, ProtoListTransportsResponse>(
            this.stub as unknown as Record<string, Function>,
            "listTransports",
            {},
        );
        return response.transports.map((t) => ({
            name: t.name,
            description: t.description,
            active: t.active,
        }));
    }

    /** The single active transport, or undefined if none configured. */
    async getActive(): Promise<TransportInfo | undefined> {
        const response = await promisify<{}, ProtoGetActiveTransportResponse>(
            this.stub as unknown as Record<string, Function>,
            "getActiveTransport",
            {},
        );
        if (!response.active) {
            return undefined;
        }
        return {
            name: response.active.name,
            description: response.active.description,
            active: response.active.active,
        };
    }
}
