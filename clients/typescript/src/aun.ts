/**
 * AUN (Acorn Universal Networking) transport-specific operations.
 *
 * These RPCs are surfaced by AunService when AUN is the active Econet
 * transport on the server. Use bbc.transport.getActive() to confirm AUN
 * is the active transport before calling these methods; otherwise the
 * RPC returns an error indicating "AUN backend is not active".
 */

import type {
    AunServiceClient,
    AunGetStatusResponse as ProtoAunGetStatusResponse,
    AunListPeersResponse as ProtoAunListPeersResponse,
    AunSetConnectedResponse as ProtoAunSetConnectedResponse,
    AunAddPeerResponse as ProtoAunAddPeerResponse,
    AunRemovePeerResponse as ProtoAunRemovePeerResponse,
} from "./generated/aun.js";
import { AunPeerSource as ProtoAunPeerSource } from "./generated/aun.js";
import { promisify } from "./call-utils.js";
import { EconetError } from "./exceptions.js";

export interface AunStatus {
    connected: boolean;
    localPort: number;
    peerCount: number;
}

/**
 * Where an AUN peer entry came from.
 *
 * Operator-configured peers (CLI `--aun map=`, the preset's
 * `econet.transport.parameters`, or `addPeer()`) always take precedence
 * over discovered peers in the routing table.
 */
export enum PeerSource {
    OperatorConfigured = "operator-configured",
    Discovered = "discovered",
}

export interface PeerInfo {
    net: number;
    stn: number;
    ipAddress: string;
    port: number;
    /**
     * Provenance of this entry. `OperatorConfigured` for entries
     * added via `--aun map=` / preset / `addPeer`; `Discovered` for
     * entries auto-populated by the AUN extension's mDNS subscriber.
     * Older servers that don't carry the proto field default to
     * `OperatorConfigured` (the only kind they had).
     */
    source: PeerSource;
}

/**
 * AUN-specific RPCs (peer table, cable plug, port status).
 *
 * Available on the server's gRPC surface only when AUN is the active
 * Econet transport. Check bbc.transport.getActive() first if your
 * code might run against a server configured for Piconet or no
 * transport.
 */
export class Aun {
    private readonly stub: AunServiceClient;

    constructor(stub: AunServiceClient) {
        this.stub = stub;
    }

    /** Read the AUN backend status. */
    async getStatus(): Promise<AunStatus> {
        const response = await promisify<{}, ProtoAunGetStatusResponse>(
            this.stub as unknown as Record<string, Function>,
            "getStatus",
            {},
        );
        return {
            connected: response.connected,
            localPort: response.localPort,
            peerCount: response.peerCount,
        };
    }

    /** Enumerate all configured AUN peers. */
    async listPeers(): Promise<PeerInfo[]> {
        const response = await promisify<{}, ProtoAunListPeersResponse>(
            this.stub as unknown as Record<string, Function>,
            "listPeers",
            {},
        );
        return response.peers.map((p) => ({
            net: p.net,
            stn: p.stn,
            ipAddress: p.ipAddress,
            port: p.port,
            // UNSPECIFIED collapses to OperatorConfigured so a newer
            // client reading an older server's response behaves the
            // same way it always has -- pre-discovery servers only
            // ever published operator-configured peers.
            source: p.source === ProtoAunPeerSource.AUN_PEER_SOURCE_DISCOVERED
                ? PeerSource.Discovered
                : PeerSource.OperatorConfigured,
        }));
    }

    /**
     * Plug or unplug the simulated network cable.
     *
     * While disconnected the ADLC sees DCD high (no carrier).
     *
     * @throws EconetError if the AUN backend is not active or the call fails.
     */
    async setConnected(connected: boolean): Promise<void> {
        const response = await promisify<{ connected: boolean }, ProtoAunSetConnectedResponse>(
            this.stub as unknown as Record<string, Function>,
            "setConnected",
            { connected },
        );
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Add an Econet address to UDP endpoint peer mapping.
     *
     * @param net - Econet network number (0-127).
     * @param stn - Econet station number (1-254).
     * @param ipAddress - Dotted-quad IP address.
     * @param port - UDP port (0 = use AUN default 32768).
     * @throws EconetError if the call fails.
     */
    async addPeer(
        net: number,
        stn: number,
        ipAddress: string,
        port: number = 0,
    ): Promise<void> {
        const response = await promisify<
            { net: number; stn: number; ipAddress: string; port: number },
            ProtoAunAddPeerResponse
        >(
            this.stub as unknown as Record<string, Function>,
            "addPeer",
            { net, stn, ipAddress, port },
        );
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Remove a peer mapping by Econet address.
     *
     * @throws EconetError if the call fails.
     */
    async removePeer(net: number, stn: number): Promise<void> {
        const response = await promisify<
            { net: number; stn: number },
            ProtoAunRemovePeerResponse
        >(
            this.stub as unknown as Record<string, Function>,
            "removePeer",
            { net, stn },
        );
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }
}
