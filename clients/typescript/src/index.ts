/**
 * Beebium TypeScript client library.
 *
 * Provides a typed interface for controlling BBC Micro emulator instances
 * via gRPC.
 */

export { Beebium } from "./client.js";
export { Connection } from "./connection.js";
export { ServerProcess, type ServerProcessOptions } from "./server-process.js";

export { Debugger, type ExecutionState, type ExecutionStateEvent, type Breakpoint, type StepResult, StopReason } from "./debugger.js";
export { CPU, type Registers, carry, zero, interruptDisable, decimal, breakFlag, overflow, negative, formatRegisters } from "./cpu.js";
export { Memory, AddressSpace, BusAccessor, PeekAccessor, Region, type MemoryRegionInfo } from "./memory.js";
export { Keyboard, type KeyboardState } from "./keyboard.js";
export { Video, type VideoConfig, type Frame } from "./video.js";
export { System, type Provenance, type MachineIdentity, type ServerStatusEvent, type ShutdownResponse, type ShutdownConditionStatus, type AdvertisementState, ServerStatus, ShutdownMode } from "./system.js";
export { Disc, Drive, type DiscMetadata, type DriveStatus, type DiscControllerStatus, type DiscEvent, type DiscControllerInfo, DriveState, DiscEventType } from "./disc.js";
export { Econet, type EconetStatus, type AdlcStatus, type HandshakeStatus, type PeerInfo } from "./econet.js";
export { Tube, type TubeStatus } from "./tube.js";
export { Via, ViaId, type ViaState } from "./via.js";
export { Crtc, type CrtcState } from "./crtc.js";
export { VideoUla, type VideoUlaState } from "./video-ula.js";
export { AddressableLatch, type AddressableLatchState } from "./latch.js";
export { Sound, type SoundChannelState, type SoundGeneratorState } from "./sound.js";
export { TubeUlaInspection, type TubeUlaState } from "./tube-ula.js";
export { Basic } from "./basic.js";
export { TubeSystem } from "./tube-system.js";
export { cStr, parseCStr, pascalStr, parsePascalStr, paddedStr, parsePaddedStr } from "./strings.js";
export { readMode7Screen, screenContains, SCREEN_MODES, MODE7_BASE, MODE7_BYTES_PER_LINE, MODE7_LINES, type ReadFn, type ScreenModeInfo } from "./screen.js";

export {
    BeebiumError,
    ConnectionError,
    ServerStartupError,
    ServerNotFoundError,
    DebuggerError,
    InvalidConditionError,
    MemoryAccessError,
    TimeoutError,
    DiscError,
    EconetError,
} from "./exceptions.js";

export const DEFAULT_GRPC_PORT = 0xBEEB;
export const VERSION = "0.1.0";
