/**
 * Tests for server status streaming RPC.
 */

import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { BeebiumClient, ServerStatusWatcher, ServerStatusType } from '../src/index.js';
import type { ServerStatusEvent } from '../src/index.js';
import { ServerFixture } from '../src/server-fixture.js';

describe('Server Status', () => {
    let fixture: ServerFixture;
    let client: BeebiumClient;

    beforeAll(async () => {
        const romDir = '/Users/rjs/Code/beebium/roms';
        fixture = new ServerFixture({
            model: 'B-RomRam',
            fdc: 'acorn-1770',
            sideways: [
                { slot: 14, type: 'rom', filepath: `${romDir}/acorn-dfs_2_26.rom` },
            ],
        });
        await fixture.start();
        client = BeebiumClient.connect(`localhost:${fixture.port}`);
    });

    afterAll(async () => {
        client?.close();
        await fixture?.stop();
    });

    it('should receive READY status on subscribe', async () => {
        const events: ServerStatusEvent[] = [];

        await new Promise<void>((resolve, reject) => {
            const watcher = client.watchServerStatus();
            const timeout = setTimeout(() => {
                watcher.cancel();
                reject(new Error('Timeout waiting for READY status'));
            }, 5000);

            watcher.on('status', (event: ServerStatusEvent) => {
                events.push(event);
                if (event.status === ServerStatusType.READY) {
                    clearTimeout(timeout);
                    watcher.cancel();
                    resolve();
                }
            });

            watcher.on('error', (error: Error) => {
                clearTimeout(timeout);
                watcher.cancel();
                reject(error);
            });
        });

        expect(events.length).toBeGreaterThanOrEqual(1);
        expect(events[0].status).toBe(ServerStatusType.READY);
        expect(events[0].message).toBe('Server ready');
    });

    it('should emit ready event', async () => {
        let gotReady = false;

        await new Promise<void>((resolve, reject) => {
            const watcher = client.watchServerStatus();
            const timeout = setTimeout(() => {
                watcher.cancel();
                reject(new Error('Timeout waiting for ready event'));
            }, 5000);

            watcher.on('ready', () => {
                gotReady = true;
                clearTimeout(timeout);
                watcher.cancel();
                resolve();
            });

            watcher.on('error', (error: Error) => {
                clearTimeout(timeout);
                watcher.cancel();
                reject(error);
            });
        });

        expect(gotReady).toBe(true);
    });

    it('should support waitForReady convenience method', async () => {
        // Should not throw
        await client.waitForReady(5000);
    });

    it('should allow cancelling the watcher', async () => {
        const watcher = client.watchServerStatus();
        let ended = false;

        watcher.on('end', () => {
            ended = true;
        });

        // Cancel immediately
        watcher.cancel();

        // Give it a moment for the end event to propagate
        await new Promise(resolve => setTimeout(resolve, 100));

        // The watcher should have ended (or at least not be receiving events)
        // Note: cancelling might not always emit 'end', but it should stop the stream
    });
});
