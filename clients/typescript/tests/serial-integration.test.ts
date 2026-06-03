/**
 * Integration tests for the serial clients against a real server.
 *
 * Unit coverage (serial.test.ts / rpc_serial.test.ts) mocks the stubs; these
 * exercise the actual gRPC wiring end-to-end: the WatchSerialStatus stream and
 * the rpc-serial peer.
 */

import { describe, it, expect } from "vitest";
import { Beebium } from "../src/client.js";
import { ServerProcess } from "../src/server-process.js";
import { Connection } from "../src/connection.js";
import { withServer } from "./server-harness.js";

describe("serial integration", () => {
    it("streams an initial snapshot and a pushed change", async () => {
        await withServer(async (conn, server) => {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            if (await bbc.debugger.isRunning()) {
                await bbc.debugger.stop(); // freeze chip state so only our write changes it
            }

            const iter = bbc.serial
                .watchStatus({ minIntervalMs: 20 })
                [Symbol.asyncIterator]();

            const first = await iter.next();
            expect(first.done).toBe(false);
            expect(first.value!.hasSerialSocket).toBe(true);

            // Flip the Serial ULA's RS423/cassette select; the server pushes it.
            const wantRs423 = !first.value!.rs423Selected;
            await bbc.memory.address.bus.writeByte(0xfe10, wantRs423 ? 0x40 : 0x00);

            let changed;
            for (;;) {
                const next = await iter.next();
                if (next.done) break;
                if (next.value.rs423Selected === wantRs423) {
                    changed = next.value;
                    break;
                }
            }
            expect(changed?.rs423Selected).toBe(wantRs423);
            await iter.return?.();
        });
    });

    it("rpc-serial: send queues bytes that status reports", async () => {
        const server = new ServerProcess({ model: "B", args: ["--rpc-serial"] });
        await server.start(10000);
        const conn = new Connection(server.target);
        await conn.waitForReady(5000);
        try {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            // Stop the machine so the queued bytes are not pulled into the ACIA.
            if (await bbc.debugger.isRunning()) {
                await bbc.debugger.stop();
            }

            const accepted = await bbc.rpcSerial.send(new Uint8Array([1, 2, 3, 4, 5]));
            expect(accepted).toBe(5);

            const status = await bbc.rpcSerial.getStatus();
            expect(status.rxPending).toBe(5);
        } finally {
            conn.close();
            await server.stop();
        }
    });
});
