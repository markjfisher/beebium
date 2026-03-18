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

    /** Run both processors. Ensures both are running afterwards. */
    async run(): Promise<void> {
        if (!await this.host.debugger.isRunning()) await this.host.debugger.run();
        if (!await this.parasite.debugger.isRunning()) await this.parasite.debugger.run();
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
    /**
     * Run both processors until predicate returns true or the budget expires.
     *
     * Execution proceeds in chunks of emulated time. At the end of each
     * chunk, both processors stop (via a server-side cycle-budget
     * breakpoint on the host with stopCounterpart), the predicate is
     * evaluated, and if false, both resume. No wall-clock polling.
     */
    async runUntil(
        predicate: () => Promise<boolean>,
        emulatedSeconds: number,
        chunkSeconds = 0.1,
    ): Promise<boolean> {
        const clockHz = await this.host.system.getClockSpeedHz() || 2_000_000;
        const totalBudget = Math.round(emulatedSeconds * clockHz);
        const chunkCycles = Math.round(chunkSeconds * clockHz);
        const startCycles = (await this.host.debugger.getState()).cycleCount;
        const deadlineCycles = startCycles + totalBudget;

        try {
            while ((await this.host.debugger.getState()).cycleCount < deadlineCycles) {
                const currentCycles = (await this.host.debugger.getState()).cycleCount;
                const chunkTarget = Math.min(currentCycles + chunkCycles, deadlineCycles);

                const bpId = await this.host.debugger.addBreakpoint(0x0000, {
                    endAddress: 0x10000,
                    condition: `cycles >= ${chunkTarget}`,
                    stopCounterpart: true,
                });

                // Ensure both stopped, subscribe to the stream, record
                // the sequence, THEN run. Any stopped event with a sequence
                // higher than recorded after run() is our breakpoint.
                await this.stop();
                const iter = this.host.debugger.watchExecutionState();
                const initial = await iter.next();
                const runSequence = initial.value!.state.sequence;
                await this.run();
                for await (const event of iter) {
                    if (!event.state.isRunning && event.state.sequence > runSequence) break;
                }

                if (await this.parasite.debugger.isRunning()) {
                    await this.parasite.debugger.stop();
                }
                await this.host.debugger.removeBreakpoint(bpId);

                if (await predicate()) {
                    return true;
                }
            }
            return predicate();
        } finally {
            await this.stop();
        }
    }

    /**
     * Run both processors for the given emulated time. No polling.
     */
    async runFor(emulatedSeconds: number): Promise<void> {
        await this.runUntil(async () => false, emulatedSeconds);
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
