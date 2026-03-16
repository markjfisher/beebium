/**
 * Coupled system abstraction for host-parasite debugging.
 *
 * Manages both host and parasite as a single unit, hiding the complexity
 * of bus-stretch cancellation, pacing asymmetry, and stop ordering.
 */

import type { Beebium } from "./client.js";

export class CoupledSystem {
    private readonly host: Beebium;
    private readonly parasite: Beebium;
    private readonly ownsParasite: boolean;

    /**
     * Create a coupled system from existing host and parasite clients.
     *
     * @param host - The host BBC Micro client.
     * @param parasite - The parasite (second processor) client.
     * @param ownsParasite - If true, close the parasite on close().
     */
    constructor(host: Beebium, parasite: Beebium, ownsParasite = false) {
        this.host = host;
        this.parasite = parasite;
        this.ownsParasite = ownsParasite;
    }

    /**
     * Create a coupled system by discovering the parasite from the host.
     */
    static async fromHost(host: Beebium): Promise<CoupledSystem> {
        const parasite = await host.connectParasite();
        return new CoupledSystem(host, parasite, true);
    }

    /** Get the host client. */
    getHost(): Beebium {
        return this.host;
    }

    /** Get the parasite client. */
    getParasite(): Beebium {
        return this.parasite;
    }

    /** Run both processors. */
    async run(): Promise<void> {
        try { await this.host.debugger.run(); } catch { /* already running */ }
        try { await this.parasite.debugger.run(); } catch { /* already running */ }
    }

    /**
     * Stop both processors (bus-stretch safe).
     *
     * Stopping either side breaks the other out of any bus-stretch
     * spin-wait via the bus_stretch_cancel flag in TubeShared.
     */
    async stop(): Promise<void> {
        if (await this.host.debugger.isRunning()) await this.host.debugger.stop();
        if (await this.parasite.debugger.isRunning()) await this.parasite.debugger.stop();
    }

    /**
     * Run both processors until predicate returns true or the budget expires.
     *
     * Both processors advance at their natural rates. The predicate
     * is evaluated periodically via peek (side-effect-free) without
     * stopping either processor.
     *
     * @param predicate - Async function returning true when the condition is met.
     * @param emulatedSeconds - Maximum BBC-time seconds to run.
     * @param pollIntervalMs - Real-time milliseconds between predicate checks.
     * @returns true if the predicate was satisfied, false on timeout.
     */
    async runUntil(
        predicate: () => Promise<boolean>,
        emulatedSeconds: number,
        pollIntervalMs = 20,
    ): Promise<boolean> {
        const clockHz = await this.host.system.getClockSpeedHz() || 2_000_000;
        const cycleBudget = Math.round(emulatedSeconds * clockHz);
        const startCycles = (await this.host.debugger.getState()).cycleCount;
        const targetCycles = startCycles + cycleBudget;
        const minCyclesBeforeCheck = clockHz;

        try {
            await this.run();

            while (true) {
                await new Promise(r => setTimeout(r, pollIntervalMs));
                const state = await this.host.debugger.getState();

                if (state.cycleCount >= targetCycles) {
                    await this.stop();
                    return predicate();
                }

                if (state.cycleCount - startCycles >= minCyclesBeforeCheck) {
                    if (await predicate()) {
                        await this.stop();
                        return true;
                    }
                }
            }
        } catch (e) {
            await this.stop();
            throw e;
        }
    }

    /**
     * Run both processors for the given emulated time.
     */
    async runFor(emulatedSeconds: number, pollIntervalMs = 20): Promise<void> {
        await this.runUntil(async () => false, emulatedSeconds, pollIntervalMs);
    }

    /**
     * Close the coupled system, stopping both processors.
     * If the parasite was auto-discovered, it is closed.
     */
    async close(): Promise<void> {
        await this.stop();
        if (this.ownsParasite) {
            await this.parasite.close();
        }
    }
}
