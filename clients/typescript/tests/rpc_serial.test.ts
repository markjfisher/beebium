import { describe, it, expect, vi } from "vitest";
import { RpcSerial } from "../src/rpc_serial.js";

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, callback: Function) => {
            try {
                callback(null, handler(request));
            } catch (err) {
                callback(err);
            }
        });
    }
    return stub;
}

describe("RpcSerial", () => {
    it("send forwards the bytes and returns the accepted count", async () => {
        let seen: any;
        const stub = createMockStub({
            send: (req) => {
                seen = req;
                return { accepted: 3 };
            },
        });
        const rpc = new RpcSerial(stub as any);

        const accepted = await rpc.send(new Uint8Array([1, 2, 3, 4]));
        expect(accepted).toBe(3);
        expect(Buffer.from(seen.data).equals(Buffer.from([1, 2, 3, 4]))).toBe(true);
    });

    it("receive forwards maxBytes and returns the bytes", async () => {
        let seen: any;
        const stub = createMockStub({
            receive: (req) => {
                seen = req;
                return { data: Buffer.from([5, 6, 7]) };
            },
        });
        const rpc = new RpcSerial(stub as any);

        const data = await rpc.receive(16);
        expect(seen.maxBytes).toBe(16);
        expect(Array.from(data)).toEqual([5, 6, 7]);
    });

    it("receive defaults maxBytes to 0", async () => {
        const stub = createMockStub({ receive: () => ({ data: Buffer.from([]) }) });
        const rpc = new RpcSerial(stub as any);

        await rpc.receive();
        expect((stub.receive as any).mock.calls[0][0].maxBytes).toBe(0);
    });

    it("getStatus returns the pending counts", async () => {
        const stub = createMockStub({
            getStatus: () => ({ txPending: 12, rxPending: 5 }),
        });
        const rpc = new RpcSerial(stub as any);

        const status = await rpc.getStatus();
        expect(status.txPending).toBe(12);
        expect(status.rxPending).toBe(5);
    });
});
