import { describe, it, expect, vi } from "vitest";
import { Econet } from "../src/econet.js";
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

describe("Econet", () => {
    describe("getStatus", () => {
        it("maps all fields including optional adlc and handshake", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: true,
                    enabled: true,
                    stationId: 100,
                    aunMode: true,
                    connected: true,
                    adlc: {
                        cr1: 0x01,
                        cr2: 0x02,
                        cr3: 0x03,
                        cr4: 0x04,
                        sr1: 0x10,
                        sr2: 0x20,
                        irqOutput: true,
                        txFifoEmpty: true,
                        txFifoFull: false,
                        rxFifoEmpty: false,
                        rxFifoFull: false,
                        txFrameField: "DATA",
                        rxFrameField: "IDLE",
                        pseLevel: 3,
                        ctsInput: true,
                    },
                    handshake: {
                        stage: "SCOUT_ACK",
                        flagFillActive: true,
                    },
                }),
            });
            const econet = new Econet(stub as any);
            const status = await econet.getStatus();

            expect(status.hasEconetSocket).toBe(true);
            expect(status.enabled).toBe(true);
            expect(status.stationId).toBe(100);
            expect(status.aunMode).toBe(true);
            expect(status.connected).toBe(true);

            expect(status.adlc).toBeDefined();
            expect(status.adlc!.cr1).toBe(0x01);
            expect(status.adlc!.sr1).toBe(0x10);
            expect(status.adlc!.irqOutput).toBe(true);
            expect(status.adlc!.txFrameField).toBe("DATA");
            expect(status.adlc!.pseLevel).toBe(3);
            expect(status.adlc!.ctsInput).toBe(true);

            expect(status.handshake).toBeDefined();
            expect(status.handshake!.stage).toBe("SCOUT_ACK");
            expect(status.handshake!.flagFillActive).toBe(true);
        });

        it("maps undefined adlc and handshake", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: false,
                    enabled: false,
                    stationId: 0,
                    aunMode: false,
                    connected: false,
                    adlc: undefined,
                    handshake: undefined,
                }),
            });
            const econet = new Econet(stub as any);
            const status = await econet.getStatus();
            expect(status.adlc).toBeUndefined();
            expect(status.handshake).toBeUndefined();
        });
    });

    describe("enable", () => {
        it("sends correct stationId and returns actual port", async () => {
            const stub = createMockStub({
                enableEconet: () => ({
                    success: true,
                    error: "",
                    actualAunPort: 32769,
                }),
            });
            const econet = new Econet(stub as any);
            const port = await econet.enable(100);
            expect(port).toBe(32769);
            expect(stub.enableEconet).toHaveBeenCalledWith(
                { stationId: 100, aunPort: 0, noNetwork: false },
                expect.any(Function),
            );
        });

        it("passes options when provided", async () => {
            const stub = createMockStub({
                enableEconet: () => ({
                    success: true,
                    error: "",
                    actualAunPort: 40000,
                }),
            });
            const econet = new Econet(stub as any);
            await econet.enable(50, { aunPort: 40000, noNetwork: true });
            expect(stub.enableEconet).toHaveBeenCalledWith(
                { stationId: 50, aunPort: 40000, noNetwork: true },
                expect.any(Function),
            );
        });

        it("throws EconetError on failure", async () => {
            const stub = createMockStub({
                enableEconet: () => ({
                    success: false,
                    error: "no socket",
                    actualAunPort: 0,
                }),
            });
            const econet = new Econet(stub as any);
            await expect(econet.enable(100)).rejects.toThrow(EconetError);
            await expect(econet.enable(100)).rejects.toThrow("Enable Econet failed: no socket");
        });
    });

    describe("disable", () => {
        it("calls correct RPC", async () => {
            const stub = createMockStub({
                disableEconet: () => ({ success: true, error: "" }),
            });
            const econet = new Econet(stub as any);
            await econet.disable();
            expect(stub.disableEconet).toHaveBeenCalledWith({}, expect.any(Function));
        });

        it("throws EconetError on failure", async () => {
            const stub = createMockStub({
                disableEconet: () => ({ success: false, error: "not enabled" }),
            });
            const econet = new Econet(stub as any);
            await expect(econet.disable()).rejects.toThrow(EconetError);
        });
    });

    // addPeer / removePeer cases removed: those RPCs moved from
    // EconetService to AunService in the prior branch and the TS
    // client does not yet have an AunService wrapper. See
    // project_typescript_client_cutover.md in project memory.

    describe("convenience methods", () => {
        it("isEnabled delegates to getStatus", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: true,
                    enabled: true,
                    stationId: 100,
                    aunMode: true,
                    connected: true,
                    adlc: undefined,
                    handshake: undefined,
                }),
            });
            const econet = new Econet(stub as any);
            expect(await econet.isEnabled()).toBe(true);
        });

        it("getStationId delegates to getStatus", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: true,
                    enabled: true,
                    stationId: 42,
                    aunMode: false,
                    connected: false,
                    adlc: undefined,
                    handshake: undefined,
                }),
            });
            const econet = new Econet(stub as any);
            expect(await econet.getStationId()).toBe(42);
        });
    });
});
