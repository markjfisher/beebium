import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        globals: true,
        environment: 'node',
        include: ['tests/**/*.test.ts'],
        testTimeout: 30000,
        globalSetup: './vitest.setup.ts',
        // Run tests sequentially to avoid port conflicts and resource contention
        pool: 'forks',
        poolOptions: {
            forks: {
                singleFork: true,  // Run all tests in a single fork
            },
        },
    },
});
