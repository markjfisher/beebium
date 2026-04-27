/**
 * Integration tests for typing under CAPS LOCK and SHIFT LOCK.
 *
 * Reproduces and exercises the fix for GitHub issue #5: text typed via
 * the TypeScript API must take account of CAPS LOCK and SHIFT LOCK,
 * otherwise `keyboard.type("hello")` produces "HELLO" on screen because
 * the BBC Micro defaults to CAPS LOCK on after boot.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";
import { screenContains, type ReadFn } from "../src/screen.js";

const HOST_CLOCK_HZ = 2_000_000;

const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

async function launchHost(): Promise<Beebium> {
    const host = await Beebium.launch({ model: "B", timeoutMs: 20000 });
    activeHosts.add(host);
    return host;
}

function readFn(bbc: Beebium): ReadFn {
    return async (addr: number, len: number) =>
        bbc.memory.address.peek.read(addr, len);
}

async function runForEmulatedSeconds(bbc: Beebium, seconds: number): Promise<void> {
    const targetCycles = Math.floor(seconds * HOST_CLOCK_HZ);
    const startCycles = (await bbc.debugger.getState()).cycleCount;
    const wasRunning = await bbc.debugger.isRunning();
    if (!wasRunning) {
        await bbc.debugger.run();
    }
    while ((await bbc.debugger.getState()).cycleCount - startCycles < targetCycles) {
        await new Promise(r => setTimeout(r, 50));
    }
    if (!wasRunning) {
        await bbc.debugger.stop();
    }
}

async function bootToBasicStopped(bbc: Beebium): Promise<void> {
    await runForEmulatedSeconds(bbc, 2.0);
    if (await bbc.debugger.isRunning()) {
        await bbc.debugger.stop();
    }
}

async function bootAndRun(bbc: Beebium): Promise<void> {
    await bootToBasicStopped(bbc);
    await bbc.debugger.run();
}

describe("Integration: Keyboard lock keys (issue #5)", () => {
    it("BBC Micro boots with CAPS LOCK on, SHIFT LOCK off (via IndicatorService)", async () => {
        const bbc = await launchHost();
        await bootToBasicStopped(bbc);
        expect(await bbc.indicators.get("caps-lock-led")).toBe(255);
        expect(await bbc.indicators.get("shift-lock-led")).toBe(0);
    });

    it("type('abc') with CAPS LOCK on echoes as 'ABC' (current behaviour)", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        expect(await bbc.indicators.get("caps-lock-led")).toBe(255);

        await bbc.keyboard.type("abc");
        await runForEmulatedSeconds(bbc, 1.0);

        const found = await screenContains(readFn(bbc), "ABC");
        const lower = await screenContains(readFn(bbc), "abc");
        expect(found).toBe(true);
        expect(lower).toBe(false);
    });

    it("withTextInput disables CAPS LOCK so type('abc') echoes as 'abc'", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);

        await bbc.keyboard.withTextInput(async () => {
            expect(await bbc.indicators.get("caps-lock-led")).toBe(0);

            await bbc.keyboard.type("abc");
            await runForEmulatedSeconds(bbc, 1.0);

            const lower = await screenContains(readFn(bbc), "abc");
            const upper = await screenContains(readFn(bbc), "ABC");
            expect(lower).toBe(true);
            expect(upper).toBe(false);
        });
    });

    it("withTextInput restores CAPS LOCK to its prior state on exit", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        const before = await bbc.indicators.get("caps-lock-led");
        expect(before).toBe(255);

        await bbc.keyboard.withTextInput(async () => {
            // No-op body
        });

        expect(await bbc.indicators.get("caps-lock-led")).toBe(before);
    });

    it("tapCapsLock toggles the IndicatorService CAPS LOCK LED", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        const before = await bbc.indicators.get("caps-lock-led");

        await bbc.keyboard.tapCapsLock();

        const after = await bbc.indicators.get("caps-lock-led");
        expect(after).not.toBe(before);
        expect([0, 255]).toContain(after);
    });

    it("withTextInput throws if the emulator is not running", async () => {
        const bbc = await launchHost();
        await bootToBasicStopped(bbc);

        await expect(
            bbc.keyboard.withTextInput(async () => { /* no-op */ }),
        ).rejects.toThrow(/running emulator/);
    });

    it("tapCapsLock throws if the emulator is not running", async () => {
        const bbc = await launchHost();
        await bootToBasicStopped(bbc);

        await expect(bbc.keyboard.tapCapsLock()).rejects.toThrow(/running emulator/);
    });
});
