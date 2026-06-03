/**
 * Client for the rpc-serial extension.
 *
 * The rpc-serial extension makes the RPC client the device on the far end of
 * the BBC's serial wire: send() injects bytes for the BBC to receive, receive()
 * collects bytes the BBC has transmitted. Requires the server to be launched
 * with --rpc-serial (so the extension owns the serial port).
 */

import type {
    RpcSerialClient,
    RpcSerialSendResponse as ProtoRpcSerialSendResponse,
    RpcSerialReceiveResponse as ProtoRpcSerialReceiveResponse,
    RpcSerialStatus as ProtoRpcSerialStatus,
} from "./generated/rpc_serial.js";
import { promisify } from "./call-utils.js";

export interface RpcSerialStatus {
    /** Bytes the BBC sent, awaiting receive(). */
    txPending: number;
    /** Bytes queued (via send()) to deliver to the BBC. */
    rxPending: number;
}

/**
 * Drive the client-driven serial peer provided by the rpc-serial extension.
 */
export class RpcSerial {
    private readonly stub: RpcSerialClient;

    constructor(stub: RpcSerialClient) {
        this.stub = stub;
    }

    /**
     * Inject bytes for the BBC to receive.
     *
     * Returns the number of bytes accepted, which is fewer than data.length when
     * the receive queue is full; resend data.slice(accepted) after the BBC has
     * read some. Never blocks.
     */
    async send(data: Uint8Array): Promise<number> {
        const response = await promisify<{ data: Buffer }, ProtoRpcSerialSendResponse>(
            this.stub as unknown as Record<string, Function>,
            "send",
            { data: Buffer.from(data) },
        );
        return response.accepted;
    }

    /** Collect bytes the BBC has transmitted (0 = all currently available). */
    async receive(maxBytes = 0): Promise<Uint8Array> {
        const response = await promisify<
            { maxBytes: number },
            ProtoRpcSerialReceiveResponse
        >(
            this.stub as unknown as Record<string, Function>,
            "receive",
            { maxBytes },
        );
        return new Uint8Array(response.data);
    }

    /** Pending byte counts in each direction. */
    async getStatus(): Promise<RpcSerialStatus> {
        const response = await promisify<{}, ProtoRpcSerialStatus>(
            this.stub as unknown as Record<string, Function>,
            "getStatus",
            {},
        );
        return { txPending: response.txPending, rxPending: response.rxPending };
    }
}
