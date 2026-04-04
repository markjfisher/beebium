/**
 * Integration tests for CoupledSystem.
 *
 * Launches a Tube-enabled server (host + parasite) and tests
 * coupled execution, predicate-based stopping, and timeout behaviour.
 *
 * Each test gets a fresh server instance.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";
import { CoupledSystem } from "../src/coupled.js";
import { screenContains, type ReadFn } from "../src/screen.js";
import { ServerProcess } from "../src/server-process.js";

const ROM_DIRPATH = process.env.BEEBIUM_ROM_DIR || "/Users/rjs/Code/beebium/roms";
const DFS_ROM_FILEPATH = `${ROM_DIRPATH}/acorn-dfs_2_26.rom`;

/** Track launched hosts for cleanup on timeout. */
const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

/** Launch a Tube-enabled Beebium and return the host client. Tracked for cleanup. */
async function launchTubeServer(): Promise<Beebium> {
    const host = await Beebium.launch({
        args: [
            "--tube", "65C02-3MHz",
            "--fdc", "acorn-1770",
            "--sideways", `14:rom:${DFS_ROM_FILEPATH}`,
        ],
        timeoutMs: 20000,
    });
    activeHosts.add(host);
    return host;
}

/** ReadFn adapter for screenContains. */
function readFn(bbc: Beebium): ReadFn {
    return async (addr: number, len: number) =>
        bbc.memory.address.peek.read(addr, len);
}

// =========================================================================
// CoupledSystem
// =========================================================================

describe("CoupledSystem", () => {
    it("should create from host and connect to parasite", async () => {
        const host = await launchTubeServer();
        try {
            // Boot to Tube banner so parasite is connected
            const found = await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );
            expect(found).toBe(true);

            const coupled = await CoupledSystem.fromHost(host);
            const parasite = coupled.getParasite();
            expect(parasite).toBeDefined();

            const pState = await parasite.debugger.getState();
            expect(pState.cycleCount).toBeGreaterThan(0);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runUntil should resolve when predicate returns true", async () => {
        const host = await launchTubeServer();
        try {
            // Boot to Tube banner
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await CoupledSystem.fromHost(host);

            // Run coupled until we see "BASIC" on screen (it should
            // already be there from the Tube banner screen).
            const result = await coupled.runUntil(
                () => screenContains(readFn(host), "BASIC"),
                5,
            );
            expect(result).toBe(true);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runUntil should return false on timeout", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await CoupledSystem.fromHost(host);

            // Predicate that never matches
            const result = await coupled.runUntil(
                async () => false,
                0.5,
            );
            expect(result).toBe(false);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runUntil should leave both processors stopped", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await CoupledSystem.fromHost(host);
            const parasite = coupled.getParasite();

            await coupled.runUntil(async () => false, 0.5);

            expect(await host.debugger.isRunning()).toBe(false);
            expect(await parasite.debugger.isRunning()).toBe(false);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runFor should advance cycles on both processors", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await CoupledSystem.fromHost(host);
            const parasite = coupled.getParasite();

            const hostCyclesBefore = (await host.debugger.getState()).cycleCount;
            const parasiteCyclesBefore = (await parasite.debugger.getState()).cycleCount;

            await coupled.runFor(0.5);

            const hostCyclesAfter = (await host.debugger.getState()).cycleCount;
            const parasiteCyclesAfter = (await parasite.debugger.getState()).cycleCount;

            // 0.5s at 2MHz host = ~1M cycles
            expect(hostCyclesAfter - hostCyclesBefore).toBeGreaterThan(500_000);
            // Parasite at 3MHz should advance even more
            expect(parasiteCyclesAfter - parasiteCyclesBefore).toBeGreaterThan(500_000);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("stop should halt both processors", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await CoupledSystem.fromHost(host);
            const parasite = coupled.getParasite();

            await coupled.run();

            // Both should be running
            expect(await host.debugger.isRunning()).toBe(true);
            expect(await parasite.debugger.isRunning()).toBe(true);

            await coupled.stop();

            // Both should be stopped
            expect(await host.debugger.isRunning()).toBe(false);
            expect(await parasite.debugger.isRunning()).toBe(false);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("parasite runUntil should stop at an address", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const parasite = await host.connectParasite();

            // Stop parasite, get its current PC, then use runUntil to
            // run to the NEXT instruction after current PC.
            if (await parasite.debugger.isRunning()) {
                await parasite.debugger.stop();
            }
            const regs = await parasite.cpu.getRegisters();
            console.log(`Parasite PC: $${regs.pc.toString(16).toUpperCase()}`);

            // Step one instruction to get a new target PC
            const stepResult = await parasite.debugger.step(1);
            const afterStep = await parasite.cpu.getRegisters();
            console.log(`After step: PC=$${afterStep.pc.toString(16).toUpperCase()}, cycles used=${stepResult.cyclesExecuted}`);

            // Start host running too (parasite may need host for Tube I/O)
            try { await host.debugger.run(); } catch { /* already running */ }

            // Now runUntil the original PC (the parasite is in a loop,
            // so it should come back around)
            const event = await parasite.debugger.runUntil(regs.pc, 10000);
            const finalRegs = await parasite.cpu.getRegisters();
            console.log(`After runUntil: PC=$${finalRegs.pc.toString(16).toUpperCase()}`);

            expect(finalRegs.pc).toBe(regs.pc);
            if (await host.debugger.isRunning()) await host.debugger.stop();

            await parasite.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("parasite debugger.stop() should work when parasite is running", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );
            const parasite = await host.connectParasite();

            // Parasite should be running freely after boot
            expect(await parasite.debugger.isRunning()).toBe(true);

            // Stop should work
            await parasite.debugger.stop();
            expect(await parasite.debugger.isRunning()).toBe(false);

            // Run should work
            await parasite.debugger.run();
            expect(await parasite.debugger.isRunning()).toBe(true);

            // Stop again should work
            await parasite.debugger.stop();
            expect(await parasite.debugger.isRunning()).toBe(false);

            await parasite.close();
        } finally {
            await host.close();
        }
    }, 15000);

    it("runUntil should throw on timeout when breakpoint is never hit", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const parasite = await host.connectParasite();

            // Set a breakpoint at an address the parasite will never reach
            // (address $0001 is zero page, never executed in the idle loop).
            // runUntil should timeout and throw.
            await expect(
                parasite.debugger.runUntil(0x0001, 2000), // 2s timeout
            ).rejects.toThrow(/Timed out/);

            // Parasite should be stopped after timeout
            expect(await parasite.debugger.isRunning()).toBe(false);

            await parasite.close();
        } finally {
            await host.close();
        }
    }, 15000);

    it("parasite cycle-budget breakpoint should fire", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await CoupledSystem.fromHost(host);
            const parasite = coupled.getParasite();

            // Test that a cycle-budget breakpoint on the parasite fires.
            await coupled.stop();
            const parasiteCycles = (await parasite.debugger.getState()).cycleCount;
            const targetCycles = parasiteCycles + 200_000;

            const bpId = await parasite.debugger.addBreakpoint(0x0000, {
                endAddress: 0x10000,
                condition: `cycles >= ${targetCycles}`,
            });

            // Start host too (parasite may need it for Tube I/O)
            try { await host.debugger.run(); } catch { /* already running */ }

            // Use the stream pattern to avoid the race condition.
            // We rely on the stream event for the stopped state rather
            // than a separate isRunning() call, which could race.
            const iter = parasite.debugger.watchExecutionState();
            await iter.next(); // consume initial state
            await parasite.debugger.run();
            let stoppedState: { isRunning: boolean; cycleCount: bigint } | undefined;
            for await (const event of iter) {
                if (!event.state.isRunning) {
                    stoppedState = event.state;
                    break;
                }
            }

            // Parasite should have stopped (confirmed by stream event)
            expect(stoppedState).toBeDefined();
            expect(stoppedState!.isRunning).toBe(false);
            expect(Number(stoppedState!.cycleCount)).toBeGreaterThanOrEqual(targetCycles);

            // Stop host too
            if (await host.debugger.isRunning()) await host.debugger.stop();

            await parasite.debugger.removeBreakpoint(bpId);
            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);
});
