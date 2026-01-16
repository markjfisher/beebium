/**
 * Beebium Test Oracle
 *
 * Differential testing framework using jsbeeb as a reference emulator.
 */

export { JsbeebOracle } from './jsbeeb-oracle.js';
export { BeebiumClient, ServerStatusWatcher } from './beebium-client.js';
export { DiffRunner, type DiffRunnerOptions } from './diff-runner.js';
export { ServerFixture, createServerFixture, type ServerFixtureOptions, type RomSlot, type SidewaysConfig } from './server-fixture.js';
export { ServerStatusType } from './types.js';
export type {
    CpuState,
    ViaState,
    CrtcState,
    VideoUlaState,
    MachineState,
    ComparisonResult,
    Divergence,
    AddressRange,
    ServerStatusEvent,
    CycleDiffChange,
    PcDivergence,
} from './types.js';
