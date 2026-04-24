import { describe, it, expect, vi } from "vitest";
import { Aun, PeerSource } from "../src/aun.js";
import { AunPeerSource } from "../src/generated/aun.js";
import { EconetError } from "../src/exceptions.js";

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, callback: Function) => {
            try {
                const response = handler(request);
                callback(null, response);
            } catch (err) {
                callback(err);
            }
        });
    }
    return stub;
}

describe("Aun", () => {
    describe("getStatus", () => {
        it("maps all fields from the response", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    connected: true,
                    localPort: 32768,
                    peerCount: 3,
                }),
            });
            const aun = new Aun(stub as any);
            const status = await aun.getStatus();
            expect(status.connected).toBe(true);
            expect(status.localPort).toBe(32768);
            expect(status.peerCount).toBe(3);
        });

        it("reports disconnected status", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    connected: false,
                    localPort: 0,
                    peerCount: 0,
                }),
            });
            const aun = new Aun(stub as any);
            const status = await aun.getStatus();
            expect(status.connected).toBe(false);
            expect(status.localPort).toBe(0);
            expect(status.peerCount).toBe(0);
        });
    });

    describe("listPeers", () => {
        it("returns empty list when no peers configured", async () => {
            const stub = createMockStub({
                listPeers: () => ({ peers: [] }),
            });
            const aun = new Aun(stub as any);
            expect(await aun.listPeers()).toEqual([]);
        });

        it("maps each peer entry to PeerInfo", async () => {
            const stub = createMockStub({
                listPeers: () => ({
                    peers: [
                        {
                            net: 0,
                            stn: 254,
                            ipAddress: "192.168.1.10",
                            port: 32768,
                            source: AunPeerSource.AUN_PEER_SOURCE_OPERATOR_CONFIGURED,
                        },
                        {
                            net: 0,
                            stn: 100,
                            ipAddress: "10.0.0.1",
                            port: 33000,
                            source: AunPeerSource.AUN_PEER_SOURCE_DISCOVERED,
                        },
                    ],
                }),
            });
            const aun = new Aun(stub as any);
            const peers = await aun.listPeers();
            expect(peers).toHaveLength(2);
            expect(peers[0]).toEqual({
                net: 0,
                stn: 254,
                ipAddress: "192.168.1.10",
                port: 32768,
                source: PeerSource.OperatorConfigured,
            });
            expect(peers[1]!.stn).toBe(100);
            expect(peers[1]!.ipAddress).toBe("10.0.0.1");
            expect(peers[1]!.source).toBe(PeerSource.Discovered);
        });

        it("falls back to OperatorConfigured for UNSPECIFIED source", async () => {
            // Older servers don't populate the source field; UNSPECIFIED
            // is the proto default and must collapse to the historical
            // operator-only behaviour.
            const stub = createMockStub({
                listPeers: () => ({
                    peers: [
                        {
                            net: 0,
                            stn: 254,
                            ipAddress: "192.168.1.10",
                            port: 32768,
                            source: AunPeerSource.AUN_PEER_SOURCE_UNSPECIFIED,
                        },
                    ],
                }),
            });
            const aun = new Aun(stub as any);
            const peers = await aun.listPeers();
            expect(peers[0]!.source).toBe(PeerSource.OperatorConfigured);
        });
    });

    describe("setConnected", () => {
        it("sends connected=true and resolves on success", async () => {
            const stub = createMockStub({
                setConnected: () => ({ success: true, error: "" }),
            });
            const aun = new Aun(stub as any);
            await aun.setConnected(true);
            expect(stub.setConnected).toHaveBeenCalledWith(
                { connected: true },
                expect.any(Function),
            );
        });

        it("sends connected=false", async () => {
            const stub = createMockStub({
                setConnected: () => ({ success: true, error: "" }),
            });
            const aun = new Aun(stub as any);
            await aun.setConnected(false);
            expect(stub.setConnected).toHaveBeenCalledWith(
                { connected: false },
                expect.any(Function),
            );
        });

        it("throws EconetError on failure", async () => {
            const stub = createMockStub({
                setConnected: () => ({
                    success: false,
                    error: "AUN backend is not active",
                }),
            });
            const aun = new Aun(stub as any);
            await expect(aun.setConnected(true)).rejects.toThrow(EconetError);
            await expect(aun.setConnected(true)).rejects.toThrow(
                "AUN backend is not active",
            );
        });
    });

    describe("addPeer", () => {
        it("sends all fields and defaults port to 0", async () => {
            const stub = createMockStub({
                addPeer: () => ({ success: true, error: "" }),
            });
            const aun = new Aun(stub as any);
            await aun.addPeer(0, 254, "192.168.1.10");
            expect(stub.addPeer).toHaveBeenCalledWith(
                { net: 0, stn: 254, ipAddress: "192.168.1.10", port: 0 },
                expect.any(Function),
            );
        });

        it("passes explicit port when given", async () => {
            const stub = createMockStub({
                addPeer: () => ({ success: true, error: "" }),
            });
            const aun = new Aun(stub as any);
            await aun.addPeer(1, 100, "10.0.0.1", 33000);
            expect(stub.addPeer).toHaveBeenCalledWith(
                { net: 1, stn: 100, ipAddress: "10.0.0.1", port: 33000 },
                expect.any(Function),
            );
        });

        it("throws EconetError on failure", async () => {
            const stub = createMockStub({
                addPeer: () => ({
                    success: false,
                    error: "peer already exists",
                }),
            });
            const aun = new Aun(stub as any);
            await expect(aun.addPeer(0, 254, "1.2.3.4")).rejects.toThrow(
                EconetError,
            );
            await expect(aun.addPeer(0, 254, "1.2.3.4")).rejects.toThrow(
                "peer already exists",
            );
        });
    });

    describe("removePeer", () => {
        it("sends net and stn", async () => {
            const stub = createMockStub({
                removePeer: () => ({ success: true, error: "" }),
            });
            const aun = new Aun(stub as any);
            await aun.removePeer(0, 254);
            expect(stub.removePeer).toHaveBeenCalledWith(
                { net: 0, stn: 254 },
                expect.any(Function),
            );
        });

        it("throws EconetError on failure", async () => {
            const stub = createMockStub({
                removePeer: () => ({ success: false, error: "peer not found" }),
            });
            const aun = new Aun(stub as any);
            await expect(aun.removePeer(0, 1)).rejects.toThrow(EconetError);
            await expect(aun.removePeer(0, 1)).rejects.toThrow("peer not found");
        });
    });
});
