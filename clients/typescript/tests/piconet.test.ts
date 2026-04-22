import { describe, it, expect, vi } from "vitest";
import { Piconet } from "../src/piconet.js";

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

describe("Piconet", () => {
    describe("getStatus", () => {
        it("reports a connected adapter", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    devicePath: "/dev/tty.usbmodem101",
                    serialOpen: true,
                }),
            });
            const piconet = new Piconet(stub as any);
            const status = await piconet.getStatus();
            expect(status.devicePath).toBe("/dev/tty.usbmodem101");
            expect(status.serialOpen).toBe(true);
        });

        it("reports a disconnected adapter", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    devicePath: "/dev/nonexistent",
                    serialOpen: false,
                }),
            });
            const piconet = new Piconet(stub as any);
            const status = await piconet.getStatus();
            expect(status.devicePath).toBe("/dev/nonexistent");
            expect(status.serialOpen).toBe(false);
        });

        it("sends empty request body", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    devicePath: "",
                    serialOpen: false,
                }),
            });
            const piconet = new Piconet(stub as any);
            await piconet.getStatus();
            expect(stub.getStatus).toHaveBeenCalledWith({}, expect.any(Function));
        });
    });
});
