/**
 * Econet management interface for the Beebium TypeScript client.
 *
 * Provides Econet hardware configuration, status queries, and
 * AUN peer management.
 */

import type {
    EconetServiceClient,
    GetEconetStatusResponse as ProtoGetEconetStatusResponse,
    AdlcStatus as ProtoAdlcStatus,
    HandshakeStatus as ProtoHandshakeStatus,
    EnableEconetResponse as ProtoEnableEconetResponse,
    DisableEconetResponse as ProtoDisableEconetResponse,
    SetStationIdResponse as ProtoSetStationIdResponse,
    SetConnectedResponse as ProtoSetConnectedResponse,
    AddPeerResponse as ProtoAddPeerResponse,
    RemovePeerResponse as ProtoRemovePeerResponse,
    ListPeersResponse as ProtoListPeersResponse,
    EconetPeer as ProtoEconetPeer,
} from "./generated/econet.js";
import { promisify } from "./call-utils.js";
import { EconetError } from "./exceptions.js";

export interface AdlcStatus {
    cr1: number;
    cr2: number;
    cr3: number;
    cr4: number;
    sr1: number;
    sr2: number;
    irqOutput: boolean;
    txFifoEmpty: boolean;
    txFifoFull: boolean;
    rxFifoEmpty: boolean;
    rxFifoFull: boolean;
    txFrameField: string;
    rxFrameField: string;
    pseLevel: number;
    ctsInput: boolean;
}

export interface HandshakeStatus {
    stage: string;
    flagFillActive: boolean;
}

export interface EconetStatus {
    hasEconetSocket: boolean;
    enabled: boolean;
    stationId: number;
    aunMode: boolean;
    connected: boolean;
    aunPort: number;
    peerCount: number;
    adlc: AdlcStatus | undefined;
    handshake: HandshakeStatus | undefined;
}

export interface PeerInfo {
    net: number;
    stn: number;
    ipAddress: string;
    port: number;
}

function toAdlcStatus(proto: ProtoAdlcStatus | undefined): AdlcStatus | undefined {
    if (!proto) return undefined;
    return {
        cr1: proto.cr1,
        cr2: proto.cr2,
        cr3: proto.cr3,
        cr4: proto.cr4,
        sr1: proto.sr1,
        sr2: proto.sr2,
        irqOutput: proto.irqOutput,
        txFifoEmpty: proto.txFifoEmpty,
        txFifoFull: proto.txFifoFull,
        rxFifoEmpty: proto.rxFifoEmpty,
        rxFifoFull: proto.rxFifoFull,
        txFrameField: proto.txFrameField,
        rxFrameField: proto.rxFrameField,
        pseLevel: proto.pseLevel,
        ctsInput: proto.ctsInput,
    };
}

function toHandshakeStatus(proto: ProtoHandshakeStatus | undefined): HandshakeStatus | undefined {
    if (!proto) return undefined;
    return {
        stage: proto.stage,
        flagFillActive: proto.flagFillActive,
    };
}

function toEconetStatus(proto: ProtoGetEconetStatusResponse): EconetStatus {
    return {
        hasEconetSocket: proto.hasEconetSocket,
        enabled: proto.enabled,
        stationId: proto.stationId,
        aunMode: proto.aunMode,
        connected: proto.connected,
        aunPort: proto.aunPort,
        peerCount: proto.peerCount,
        adlc: toAdlcStatus(proto.adlc),
        handshake: toHandshakeStatus(proto.handshake),
    };
}

function toPeerInfo(proto: ProtoEconetPeer): PeerInfo {
    return {
        net: proto.net,
        stn: proto.stn,
        ipAddress: proto.ipAddress,
        port: proto.port,
    };
}

/**
 * Econet management interface.
 *
 * Provides Econet hardware configuration, status queries, and
 * AUN peer management.
 */
export class Econet {
    private readonly stub: EconetServiceClient;

    constructor(stub: EconetServiceClient) {
        this.stub = stub;
    }

    /** Get the full Econet hardware status. */
    async getStatus(): Promise<EconetStatus> {
        const response = await promisify<{}, ProtoGetEconetStatusResponse>(
            this.stub as unknown as Record<string, Function>,
            "getEconetStatus",
            {},
        );
        return toEconetStatus(response);
    }

    /** Whether Econet hardware is currently enabled. */
    async isEnabled(): Promise<boolean> {
        return (await this.getStatus()).enabled;
    }

    /** Get the current station ID. */
    async getStationId(): Promise<number> {
        return (await this.getStatus()).stationId;
    }

    /** List all configured AUN peers. */
    async getPeers(): Promise<PeerInfo[]> {
        const response = await promisify<{}, ProtoListPeersResponse>(
            this.stub as unknown as Record<string, Function>,
            "listPeers",
            {},
        );
        return response.peers.map(toPeerInfo);
    }

    /**
     * Enable Econet hardware.
     *
     * @param stationId - Station number (1-254).
     * @param options.aunPort - UDP port to bind (0 = default 32768).
     * @param options.noNetwork - If true, fit hardware with no network connection.
     * @returns The actual AUN port that was bound.
     */
    async enable(
        stationId: number,
        options?: { aunPort?: number; noNetwork?: boolean },
    ): Promise<number> {
        const response = await promisify<
            { stationId: number; aunPort: number; noNetwork: boolean },
            ProtoEnableEconetResponse
        >(
            this.stub as unknown as Record<string, Function>,
            "enableEconet",
            {
                stationId,
                aunPort: options?.aunPort ?? 0,
                noNetwork: options?.noNetwork ?? false,
            },
        );
        if (!response.success) {
            throw new EconetError(`Enable Econet failed: ${response.error}`);
        }
        return response.actualAunPort;
    }

    /** Set the station ID (takes effect on next machine reset). */
    async setStationId(stationId: number): Promise<void> {
        const response = await promisify<{ stationId: number }, ProtoSetStationIdResponse>(
            this.stub as unknown as Record<string, Function>,
            "setStationId",
            { stationId },
        );
        if (!response.success) {
            throw new EconetError(`Set station ID failed: ${response.error}`);
        }
    }

    /** Connect or disconnect the network cable. */
    async setConnected(connected: boolean): Promise<void> {
        const response = await promisify<{ connected: boolean }, ProtoSetConnectedResponse>(
            this.stub as unknown as Record<string, Function>,
            "setConnected",
            { connected },
        );
        if (!response.success) {
            throw new EconetError(`Set connected failed: ${response.error}`);
        }
    }

    /** Disable Econet hardware. */
    async disable(): Promise<void> {
        const response = await promisify<{}, ProtoDisableEconetResponse>(
            this.stub as unknown as Record<string, Function>,
            "disableEconet",
            {},
        );
        if (!response.success) {
            throw new EconetError(`Disable Econet failed: ${response.error}`);
        }
    }

    /**
     * Add an AUN peer mapping.
     *
     * @param net - Econet network number (0-127).
     * @param stn - Econet station number (1-254).
     * @param ipAddress - Dotted-quad IP address.
     * @param port - UDP port (0 = default 32768).
     */
    async addPeer(net: number, stn: number, ipAddress: string, port?: number): Promise<void> {
        const response = await promisify<
            { net: number; stn: number; ipAddress: string; port: number },
            ProtoAddPeerResponse
        >(
            this.stub as unknown as Record<string, Function>,
            "addPeer",
            { net, stn, ipAddress, port: port ?? 0 },
        );
        if (!response.success) {
            throw new EconetError(`Add peer failed: ${response.error}`);
        }
    }

    /**
     * Remove an AUN peer mapping.
     *
     * @param net - Econet network number.
     * @param stn - Econet station number.
     */
    async removePeer(net: number, stn: number): Promise<void> {
        const response = await promisify<{ net: number; stn: number }, ProtoRemovePeerResponse>(
            this.stub as unknown as Record<string, Function>,
            "removePeer",
            { net, stn },
        );
        if (!response.success) {
            throw new EconetError(`Remove peer failed: ${response.error}`);
        }
    }
}
