/**
 * Vitest global setup - runs before all tests
 * Kills any zombie beebium processes to ensure clean test environment.
 */

import { exec } from 'child_process';
import { promisify } from 'util';

const execAsync = promisify(exec);

export async function setup() {
    // Kill any existing beebium processes before tests start
    try {
        await execAsync('pkill -9 beebium-model-b 2>/dev/null || true');
        // Brief pause to ensure ports are released
        await new Promise(resolve => setTimeout(resolve, 200));
    } catch {
        // No processes to kill, which is fine
    }
}

export async function teardown() {
    // Kill any remaining beebium processes after all tests complete
    try {
        await execAsync('pkill -9 beebium-model-b 2>/dev/null || true');
    } catch {
        // No processes to kill, which is fine
    }
}
