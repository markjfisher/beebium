# Tube Subsystem Design

## Overview

The Tube is Acorn's coprocessor interface: a custom ULA chip providing bidirectional FIFO-based
communication between two independent processor systems. The host (BBC Micro) handles I/O while
the parasite (second processor) runs application code. The two processors have completely
independent clock domains and communicate exclusively through the Tube's register interface.

Beebium's Tube implementation takes a fundamentally different approach from other emulators.
Rather than lockstep single-threaded execution, each second processor runs as an **independent
OS process** communicating with the host via shared memory. This mirrors the real hardware's
fully asynchronous nature: two independent computers linked only by a narrow FIFO channel.

The Tube is configured at startup with `--tube <coprocessor-stem>` (e.g. `--tube 65C02-3MHz`),
following the same optional-peripheral pattern as `--fdc` and `--station`.

### Design principles

1. **Process isolation** -- the parasite is a separate executable with its own event loop,
   clock pacing, and gRPC debug server
2. **Asynchronous fidelity** -- no global clock, no lockstep; each CPU runs at its own rate,
   just as the real hardware did
3. **Shared memory for latency** -- the Tube FIFO registers live in a shared memory region
   accessed via lock-free atomics, giving sub-cycle latency at 2 MHz
4. **FIFO-authoritative model** -- the emulated Tube FIFO governs backpressure and interrupt
   generation; shared memory is the transport, not a buffering layer

## Hardware Reference

### Register map

The Tube ULA exposes 8 bytes of I/O space to each processor. On the host side these appear at
`&FEE0-&FEE7` (Sheila); on the parasite side at `&FEF8-&FEFF`.

| Offset | Host read          | Host write         | Parasite read        | Parasite write     |
|--------|--------------------|--------------------|----------------------|--------------------|
| 0      | R1STAT + flags     | Status flags (S)   | R1STAT + flags       | --                 |
| 1      | R1DATA (24B FIFO)  | R1DATA (1B latch)  | R1DATA (1B read)     | R1DATA (24B FIFO)  |
| 2      | R2STAT             | --                 | R2STAT               | --                 |
| 3      | R2DATA (1B read)   | R2DATA (1B write)  | R2DATA (1B read)     | R2DATA (1B write)  |
| 4      | R3STAT             | --                 | R3STAT               | --                 |
| 5      | R3DATA (2B FIFO)   | R3DATA (2B FIFO)   | R3DATA (2B FIFO)     | R3DATA (2B FIFO)   |
| 6      | R4STAT             | --                 | R4STAT               | --                 |
| 7      | R4DATA (1B read)   | R4DATA (1B write)  | R4DATA (1B read)     | R4DATA (1B write)  |

Note the asymmetry of Register 1: the parasite-to-host direction has a 24-byte FIFO (sized to
hold the longest VDU command for OSWRCH), while the host-to-parasite direction is a single byte
latch (used for event/escape notification).

### Register set details

**Register 1** -- OSWRCH and events
- Parasite to Host: 24-byte FIFO. Carries OSWRCH character output.
- Host to Parasite: 1-byte latch. Delivers event interrupts and escape state.
- Status bits: bit 7 = data available, bit 6 = not full.
- Reading R1DATA on the host clears PIRQ if Register 1 was the source.

**Register 2** -- OS call protocol
- 1-byte latch in each direction.
- Carries non-time-critical OS calls: OSRDCH, OSCLI, OSBYTE, OSWORD, OSBPUT, OSBGET,
  OSFIND, OSARGS, OSFILE, OSGBPB.
- The parasite writes a command byte, then the two sides exchange data bytes through R2DATA
  following the protocol defined in the Tube Software Protocol Specification.

**Register 3** -- bulk NMI-driven transfer
- 2-byte FIFO in each direction.
- Configurable as 1-byte or 2-byte mode via the V control flag.
- In 1-byte mode: each byte written makes the register appear full.
- In 2-byte mode: data available is only asserted when both bytes are present; not-full is only
  asserted when both bytes have been removed.
- Generates PNMI (parasite NMI) for time-critical block transfers (disc loading etc).
- May interface with DMA controller via DRQ/DACK pins on hardware.

**Register 4** -- transfer control
- 1-byte latch in each direction.
- Writing sets an IRQ; reading clears it.
- Used as the control channel for R3 block transfers: the host writes a transfer type byte to
  R4DATA to interrupt the parasite, which then sets up the appropriate NMI handler.
- Also carries error strings from host to parasite.

### Control flags

Written via offset 0 (status register). Bit 7 (S) selects set or clear mode: with S=1 the
other bits are ORed into the control register; with S=0 they are ANDed out.

| Bit | Flag | Meaning                                        |
|-----|------|------------------------------------------------|
| 0   | Q    | Enable HIRQ from Register 4                    |
| 1   | I    | Enable PIRQ from Register 1                    |
| 2   | J    | Enable PIRQ from Register 4                    |
| 3   | M    | Enable PNMI from Register 3                    |
| 4   | V    | Two-byte operation of Register 3               |
| 5   | P    | Activate PRST (parasite reset)                 |
| 6   | T    | Clear all Tube registers                       |
| 7   | S    | Set (1) or clear (0) the indicated flags       |

All flags except T are readable from address 0 as the least significant 6 bits. The F1 (not
full) flag for Register 1 occupies bit 6 of the status read, and bit 7 is the A1 (data
available) flag.

### Interrupt generation

Three interrupt outputs, active low:

**HIRQ** (to host processor):
- Q=1 AND Register 4 parasite-to-host latch has data available.

**PIRQ** (to parasite processor):
- (I=1 AND Register 1 host-to-parasite latch has data available)
- OR (J=1 AND Register 4 host-to-parasite latch has data available).

**PNMI** (to parasite processor):
- M=1, V=0: host-to-parasite R3 has 1+ bytes, OR parasite-to-host R3 has 0 bytes.
  (Single-byte transfers across R3.)
- M=1, V=1: host-to-parasite R3 has 2 bytes, OR parasite-to-host R3 has 0 bytes.
  (Two-byte transfers across R3.)

The PNMI condition is symmetric: it fires both when there is data to read AND when the
outbound FIFO is empty (ready for the parasite to write the next byte/pair).

Interrupts are cleared by removing the cause: reading data clears "data available", writing
data clears "not full" becoming active, etc.

### Reset behaviour

HRST (active low) initialises the Tube to a known state and automatically asserts PRST:
- All control flags (T, P, V, M, J, I, Q) cleared to zero.
- All registers purged.
- **Exception**: Register 3 parasite-to-host FIFO is initialised with one valid but
  insignificant byte. This prevents an immediate spurious PNMI after reset.

The T control bit performs a software reset of all registers (same as HRST) but preserves
P, V, M, J, I, Q.

The P control bit allows the host to reset the parasite processor independently.

### Software protocol

The Tube Software Protocol Specification (ref: SOtube8, Appendix in Application Note 004)
defines the byte-level protocols for all OS calls. Key patterns:

**Non-interrupt protocols** (R1, R2):
- OSWRCH: parasite writes character to R1DATA (host polls R1STAT).
- All other OS calls: parasite writes command byte to R2DATA, then exchanges parameters
  through R2DATA. The host polls R2STAT waiting for commands.

**Interrupt-driven protocols** (R3, R4):
- Host writes transfer type to R4DATA, interrupting parasite.
- Parasite reads type, reads 4-byte address from R4DATA, installs NMI handler.
- Parasite reads synchronisation byte from R4DATA to start transfer.
- Data flows through R3DATA under NMI at rates dictated by the host.

**Transfer types** (written by host to R4DATA):

| Type | Direction | Method   | Notes                                          |
|------|-----------|----------|------------------------------------------------|
| 0    | P to H    | NMI      | Single-byte, 24 us/byte                        |
| 1    | H to P    | NMI      | Single-byte, 24 us/byte                        |
| 2    | P to H    | NMI      | Double-byte, 26 us/pair                        |
| 3    | H to P    | NMI      | Double-byte, 26 us/pair                        |
| 4    | --        | Execute  | Start execution at given address               |
| 5    | --        | Release  | Filing system releases Tube                    |
| 6    | P to H    | Polling  | 256-byte block, 10 us/byte, no NMI             |
| 7    | H to P    | Polling  | 256-byte block, 10 us/byte, no NMI             |

**Startup sequence**: parasite sends startup message via OSWRCH (R1), sends zero byte to
terminate, then waits on R2DATA for the host to load a language ROM (via R3/R4 block
transfer) and issue a type 4 Execute command.

## How Other Emulators Implement the Tube

All four reference emulators (BeebEm, B-Em, B2, jsbeeb) use single-threaded lockstep
execution. This section documents their approaches for reference and contrast.

### BeebEm

- **Synchronisation**: lockstep. Host executes one instruction, then
  `SyncTubeProcessor()` runs the parasite until it catches up:
  `while (TotalTubeCycles < (TotalCycles / 2 * 3))`.
- **FIFO**: shift-based array for R1 (24 bytes). On each host read, elements shift down.
  O(n) per read but simple.
- **Interrupts**: level-triggered for IRQ, edge-detected for NMI (tracks
  `OldTubeNMIStatus` to detect 0-to-1 transitions).
- **Parasites**: 65C02, Z80, ARM (3 variants), 80186, NS32016 -- all in same process.
- **Key files**: `Src/Tube.cpp` (2785 lines), `Src/Tube.h`.

### B-Em

- **Synchronisation**: cycle-budget accumulator. Host accumulates `tubecycles`; when
  threshold reached, calls `tube_exec()` function pointer.
- **FIFO**: circular buffer with head/tail/count for R1.
- **Interrupts**: `tube_updateints()` called after every register access; dispatches to
  processor-specific interrupt setters via function pointers.
- **Parasites**: polymorphic via function pointers (`tube_exec`, `tube_readmem`,
  `tube_writemem`). Supports 6502, Z80, ARM, 6809, 68000, PDP-11, NS32016.
- **Key files**: `src/tube.h`, `src/tube.c`, `src/6502tube.c`.

### B2

- **Synchronisation**: per-cycle lockstep. Each `Update()` call executes exactly one cycle
  on each processor. Parasite executes first.
- **FIFO**: circular buffer with write/read indices and count for R1.
- **Interrupts**: recalculated every cycle via `M6502_SetDeviceIRQ/NMI`.
- **Parasites**: 65C02 only (Rockwell variant). Boot mode flag pages ROM in/out.
- **Key files**: `src/beeb/include/beeb/tube.h`, `src/beeb/src/tube.cpp`.

### jsbeeb

- **Synchronisation**: cooperative polling. Host grants cycles to parasite via
  `tubeStuff(cycles)` in `polltime()`.
- **FIFO**: shift-based array for R1 (same as BeebEm).
- **Interrupts**: `updateInterrupts()` after every register access.
- **Parasites**: 65C02 only (`Tube6502` extends `Base6502`). ROM files exist for Z80/ARM
  but CPU emulators not implemented.
- **Key files**: `src/tube.js` (349 lines), parasite CPU in `src/6502.js`.

### Common patterns

All four emulators share these characteristics:
- Single-threaded: no concurrency between host and parasite.
- Shared memory: both CPUs directly access the same Tube register struct.
- Deterministic: identical inputs produce identical outputs.
- The R1 parasite-to-host FIFO is always 24 bytes.
- NMI edge detection is universally implemented.
- The dummy byte in R3 P-to-H after reset is universally implemented.

### Why Beebium differs

Lockstep works well for single-process emulators but conflicts with Beebium's multi-process
architecture. More importantly, the real Tube hardware was genuinely asynchronous -- two
independent processors with independent oscillators. Beebium's process-separated design is
arguably more faithful to the original hardware's behaviour than lockstep emulation.

## Architecture

### Process model

```
beebium-model-b (host)              beebium-tube-65C02-3MHz (parasite)
┌────────────────────────┐          ┌────────────────────────┐
│  6502 CPU (2 MHz)      │          │  65C02 CPU (3 MHz)     │
│  System VIA, User VIA  │          │  64K RAM + Boot ROM    │
│  CRTC, Video ULA, etc  │          │                        │
│                        │          │                        │
│  ┌──────────────────┐  │          │  ┌──────────────────┐  │
│  │ TubeHostPort     │  │          │  │ TubeParasitePort │  │
│  │ (shared mem)     │  │          │  │ (shared mem)     │  │
│  └────────┬─────────┘  │          │  └────────┬─────────┘  │
│           │            │          │           │            │
│  gRPC: Video, Audio,   │          │  gRPC: Debugger,       │
│  Keyboard, Disc, etc   │          │  System (parasite)     │
└───────────┼────────────┘          └───────────┼────────────┘
            │                                   │
            │  ┌──────────────────────────────┐  │
            └──┤   Shared memory region       ├──┘
               │                              │
               │  ┌────────────────────────┐  │
               │  │ TubeShared (lock-free) │  │
               │  │                        │  │
               │  │  FIFO data + counts    │  │
               │  │  Status flags          │  │
               │  │  Control flags         │  │
               │  └────────────────────────┘  │
               │                              │
               │  Lifecycle mailbox (cold)    │
               └──────────────────────────────┘
              Setup via gRPC TubeService.Connect
```

The Tube ULA state lives in a **shared memory region** mapped into both processes. Both CPU
threads access the FIFOs and status flags directly through lock-free atomic operations. No
IO threads, no staging queues, no syscalls on the data path.

The host process already runs a gRPC server for its frontends. The parasite connects to
this server and calls `TubeService.Connect` to learn the shared memory name, then opens
the shared memory segment by name (`shm_open` on POSIX, `OpenFileMapping` on Windows).
No Unix domain socket or file descriptor passing is needed. Lifecycle messages (reset,
freeze, shutdown) use a small mailbox within the shared memory itself.

### Why shared memory, not sockets

The real Tube ULA is physically shared hardware -- a chip sitting between two buses, with
internal latches that both processors access directly. Shared memory is the closest software
analogue. A socket introduces unnecessary indirection.

Latency comparison at 2 MHz (500 ns/cycle) and 3 MHz (333 ns/cycle):

| Transport        | Latency        | Host cycles | Parasite cycles |
|------------------|----------------|-------------|-----------------|
| Unix socket      | 1-5 us         | 2-10        | 3-15            |
| Shared memory    | 50-100 ns      | < 1         | < 1             |

With a socket, a parasite polling R2STAT in a tight loop pays a multi-microsecond penalty
per iteration (two syscalls, kernel buffer copy, potential context switch). With shared
memory, the same poll is a single atomic load from a cache line -- sub-cycle latency.

Both emulated CPUs are already running continuous pacing loops; neither sleeps waiting for
Tube data. So there is no need for a wakeup mechanism like `eventfd` or `kqueue`. Each CPU
thread simply checks the shared Tube state on every relevant register access, which it does
anyway as part of normal instruction execution.

### Shared memory layout

The shared region contains a single `TubeShared` structure, carefully laid out for
lock-free access:

```
TubeShared (cache-line aligned, standard layout)
├── Header
│   ├── magic: u32              // 0x54554245 ("TUBE")
│   └── version: u32            // protocol version (currently 1)
│
├── Control (written by host, read by both)
│   └── control_flags: atomic<u8>   // Q, I, J, M, V, P (bits 0-5)
│
├── H-to-P registers (written by host, read by parasite)
│   ├── r1_h2p: TubeLatch { value: atomic<u8>, ready: atomic<u8> }
│   ├── r2_h2p: TubeLatch { value: atomic<u8>, ready: atomic<u8> }
│   ├── r3_h2p: TubeReg3  { data: [atomic<u8>; 2], count: atomic<u8>,
│   │                        pending: atomic<u8> }
│   └── r4_h2p: TubeLatch { value: atomic<u8>, ready: atomic<u8> }
│
├── P-to-H registers (written by parasite, read by host)
│   ├── r1_p2h: TubeFifo24 { data: [atomic<u8>; 24], head: atomic<u8>,
│   │                         tail: atomic<u8>, count: atomic<u8> }
│   ├── r2_p2h: TubeLatch  { value: atomic<u8>, ready: atomic<u8> }
│   ├── r3_p2h: TubeReg3   { data: [atomic<u8>; 2], count: atomic<u8>,
│   │                         pending: atomic<u8> }
│   └── r4_p2h: TubeLatch  { value: atomic<u8>, ready: atomic<u8> }
│
├── Lifecycle mailbox (for reset/freeze/shutdown)
│   ├── host_command: atomic<u8>
│   └── parasite_ack: atomic<u8>
│
└── Padding to cache line boundary
```

The `pending` field in `TubeReg3` provides hysteresis for multi-byte transfers:
- `pending = 0` -- writer phase (space available, writer may deposit bytes)
- `pending = 1` -- reader phase (data available, reader may consume bytes)
- Transition to 1: when count reaches the V-dependent threshold (1 or 2) on write
- Transition to 0: when count reaches 0 on read

This prevents status-flag flicker during a two-byte transfer, where the reader might
observe count=1 between the writer's two stores.

All atomic fields use `std::memory_order_release` on writes and `std::memory_order_acquire`
on reads. This provides the necessary happens-before ordering without full barriers.

### Single-writer principle

Each field in the shared structure has exactly one writer:

| Field             | Writer    | Reader(s)       |
|-------------------|-----------|-----------------|
| Control flags     | Host      | Both            |
| H-to-P registers | Host      | Parasite        |
| P-to-H registers | Parasite  | Host            |
| host_command      | Host      | Parasite        |
| parasite_ack      | Parasite  | Host            |

This is the SPSC (single-producer, single-consumer) pattern applied per-field. No CAS
operations are needed. No locks. Just loads and stores with acquire/release semantics.

The R1 parasite-to-host FIFO (24 bytes) requires slightly more care: the parasite writes
at the tail and the host reads from the head. With separate atomic head/tail indices and
a count, this is a standard lock-free SPSC ring buffer.

#### Lock-free and wait-free properties

All shared memory FIFO operations use only atomic loads (acquire) and stores (release).
No compare-and-swap, no mutexes, no system calls on the data path. This makes the
non-stretched paths **wait-free**: every operation completes in a bounded number of
steps regardless of the other side's progress.

The bus-stretching path in `TubeHostPort` (spin-wait on a full register) is
**lock-free** but not wait-free: the spinning thread makes no progress itself, but the
system as a whole makes progress because the parasite thread is guaranteed to eventually
drain the register. In practice the spin duration is bounded by the parasite's instruction
execution time (a few hundred nanoseconds at 3 MHz).

### Status flags and interrupt computation

Each side computes its own interrupt state locally based on the shared data:

**Host side** (on every Tube register access or periodic poll):
1. Read `r4_p2h.ready` (acquire).
2. Read `control.flags` (acquire).
3. Compute: `hirq = flags.q && r4_p2h.ready`.
4. Assert/deassert HIRQ on host CPU.

**Parasite side** (on every Tube register access or periodic poll):
1. Read `r1_h2p.ready` and `r4_h2p.ready` (acquire).
2. Read `control.flags` (acquire).
3. Compute: `pirq = (flags.i && r1_h2p.ready) || (flags.j && r4_h2p.ready)`.
4. Compute PNMI from R3 state and M/V flags.
5. Assert/deassert PIRQ and PNMI (edge-triggered NMI, gated by handler tracking).

No explicit interrupt signalling crosses the process boundary. Each side derives interrupt
state from the shared register data, exactly as the real Tube ULA does internally.

### Backpressure and bus stretching

The Tube ULA provides backpressure through two mechanisms that work at different levels:

**Software-level backpressure** (polling): the MOS and Tube host code poll status flags
before accessing data registers. When the host reads R1STAT and sees `not_full = false`,
it loops and tries again. Similarly, the parasite polls for `data_available = true` before
reading. This is how the software protocol handles normal flow control, and it works
identically in both models.

**Hardware-level backpressure** (bus stretching): on real hardware, when the host CPU
writes to a data register that is already full (R1, R3, or R4 host-to-parasite), the Tube
ULA halts the host CPU's clock until the parasite drains the register. The host CPU is
physically frozen mid-write -- it does not execute further instructions. This is a
safety net for when software-level polling is bypassed or when the host writes faster
than the parasite can consume.

Register 2 is exempt from bus stretching (Application Note 004). R2 handles OS calls
where the protocol guarantees alternating reads and writes, making bus stretching
unnecessary.

Beebium implements bus stretching differently in its two Tube models:

- **TubeUla** (in-process model): buffers the write and sets `stretched_ = true`. The
  caller (test harness or in-process interleaver) must step the parasite until
  `stretched()` returns false before resuming host execution. This models the real
  clock-halting behaviour without requiring actual thread blocking.

- **TubeHostPort** (shared memory model): implements bus stretching as a spin-wait on
  the shared memory FIFO state. When the host writes to a full register, the host thread
  spins until the parasite (running on its own thread or in its own process) drains the
  register. This is the closest software analogue to the real hardware's clock-halting
  behaviour. The spin-wait is cheap because the shared memory read completes in
  nanoseconds (cache coherence latency).

Both models are correct for their intended use: TubeUla for single-threaded testing
where blocking would deadlock, TubeHostPort for multi-threaded/multi-process production
use where the parasite runs independently.

### Latency characteristics

The propagation delay from write to visibility is bounded by cache coherence latency
(typically 50-100 ns on modern hardware). This is well under one emulated cycle on either
processor. In practice, data written by one side becomes visible to the other within the
same or next emulated instruction -- similar to the 1-2 microsecond across-tube transfer
time documented in the Application Note, but actually faster in proportion to emulated
clock speed.

This means:
- A host write to R4DATA is visible to the parasite essentially immediately.
- The parasite's IRQ handler can respond within a few emulated instructions.
- NMI-driven R3 transfers achieve maximum throughput without artificial delays.

### When to check shared state

Each CPU thread checks the shared Tube state:

1. **On every Tube register access** -- the natural point. When the CPU reads R1STAT, the
   wrapper reads the shared `r1_p2h.count` and `r1_h2p.ready` to compute status bits.

2. **Periodically for interrupt responsiveness** -- when data arrives in a register with
   interrupts enabled, the local CPU must notice. Checking at instruction boundaries
   (once per instruction, not per cycle) is sufficient. The real hardware had interrupt
   latency of at least one instruction anyway.

### Lifecycle messages

Reset, freeze, and shutdown are infrequent operations that do not need sub-cycle latency.
These use a simple mailbox in the shared memory:

- Host writes command to `host_command` (e.g. RESET=1, FREEZE=2, SHUTDOWN=3).
- Parasite checks `host_command` periodically (e.g. every N instructions).
- Parasite performs the action, writes acknowledgement to `parasite_ack`.
- Host polls `parasite_ack` until acknowledged.

For shutdown, if the parasite process crashes, the host detects this via `waitpid()` or
equivalent. The shared memory region is cleaned up by the host.

### Shared memory creation

The host creates the shared memory segment before launching the parasite. The parasite
learns the shared memory name via the `TubeService.Connect` gRPC call (see Connection
and Negotiation section below).

**macOS/Linux (POSIX)**:
1. Host calls `shm_open("/beebium-tube-<pid>", O_CREAT | O_RDWR, ...)`.
2. Host calls `ftruncate()` to set the size (e.g. 4096 bytes, page-aligned).
3. Host calls `mmap()` to map it.
4. Host initialises `TubeShared` header and reset state.
5. Parasite receives the name via `TubeService.Connect`, calls `shm_open()` + `mmap()`.
6. Both sides validate the header (magic number, version).

**Windows**:
- Use `CreateFileMapping(INVALID_HANDLE_VALUE, ...)` / `MapViewOfFile()` with a named
  mapping (e.g. `"Local\\beebium-tube-<pid>"`).
- Parasite receives the name via `TubeService.Connect` and calls `OpenFileMapping()`.

The `shm_unlink()` (or equivalent) is called by the host during shutdown, after the
parasite has exited.

### Portability notes

**Atomics across processes**: the `TubeShared` struct uses atomic operations on fields up
to `uint8_t` in size, which are lock-free on all target architectures (x86-64, ARM64).
Rather than placing `std::atomic<uint8_t>` directly in the shared memory layout (which
would require constructor calls on mapped memory), we lay out plain `uint8_t` fields and
access them via `std::atomic_ref<uint8_t>` (C++20). This gives us well-defined atomic
semantics on memory that was not constructed by `std::atomic`.

**Stale segment cleanup**: on POSIX, shared memory segments persist if the host crashes
without calling `shm_unlink()`. On startup, the host should check for stale segments
whose PID (embedded in the name) no longer exists, and unlink them. On Windows, named
mappings are reference-counted and cleaned up automatically when all handles are closed,
so no stale cleanup is needed.

**Memory-mapped alignment**: the `TubeShared` struct should be sized to a whole number of
pages (4096 bytes is more than sufficient). Fields accessed by different processes should
be separated onto different cache lines (64 bytes) to avoid false sharing -- in particular,
the H-to-P registers (written by host) and P-to-H registers (written by parasite) should
be on separate cache lines.

## Connection and Negotiation

### gRPC TubeService

The host process already runs a gRPC server for its frontends. The parasite process
connects to this same server to negotiate the shared memory link via a new `TubeService`:

```protobuf
service TubeService {
    // Parasite connects, receives shared memory details.
    rpc Connect(TubeConnectRequest) returns (TubeConnectResponse);

    // Host requests parasite state for save/restore.
    rpc GetState(TubeGetStateRequest) returns (TubeGetStateResponse);

    // Host sends saved state for restore.
    rpc RestoreState(TubeRestoreStateRequest) returns (TubeRestoreStateResponse);
}

message TubeConnectRequest {
    uint32 protocol_version = 1;
    string parasite_type = 2;       // "6502", "z80", "arm", etc.
    uint32 parasite_clock_hz = 3;
}

message TubeConnectResponse {
    string shared_memory_name = 1;  // POSIX shm name or Windows mapping name
    uint32 shared_memory_size = 2;
    uint32 protocol_version = 3;
}

message TubeGetStateRequest {}

message TubeGetStateResponse {
    bytes cpu_state = 1;
    bytes ram = 2;
    bytes tube_state = 3;
}

message TubeRestoreStateRequest {
    bytes cpu_state = 1;
    bytes ram = 2;
    bytes tube_state = 3;
}

message TubeRestoreStateResponse {}
```

### Connection sequence

The host owns the entire lifecycle. It creates the shared memory, launches the parasite
subprocess, and cleans up when done.

1. Host creates shared memory segment during `install_tube()` (before `machine.reset()`).
   The segment is named `/beebium-tube-<machine-uuid>` for uniqueness.
2. Host starts gRPC server (with `TubeService` registered).
3. Host launches the parasite executable: `beebium-tube-65C02-3MHz --host=localhost:<port>`.
   The port is the actual bound gRPC port (known after `server.start()`).
4. Parasite connects to the host's gRPC server and calls `TubeService.Connect`.
5. Host returns the shared memory name and size.
6. Parasite opens and maps the shared memory, validates header (magic, version).
7. Host deasserts PRST. Parasite CPU begins executing from Boot ROM reset vector.

This reuses the existing gRPC infrastructure rather than introducing a second socket type.
The gRPC connection also provides a natural channel for save-state coordination and other
infrequent operations.

### Lifecycle operations via shared memory

Once the shared memory link is established, all hot-path data flows through shared memory
(see Architecture section above). Infrequent lifecycle operations use the shared memory
mailbox:

| Command      | Value | Meaning                                      |
|--------------|-------|----------------------------------------------|
| NONE         | 0     | No pending command                           |
| RESET        | 1     | Host requests parasite reset                 |
| FREEZE       | 2     | Host requests parasite to stop and dump state|
| SHUTDOWN     | 3     | Host requests parasite to exit               |

The parasite polls `host_command` periodically (e.g. every 1000 instructions) and
acknowledges via `parasite_ack`.

For state dump/restore, the parasite receives FREEZE via the mailbox, then the host calls
`TubeService.GetState` or `TubeService.RestoreState` over gRPC. This keeps bulk data
transfer (64 KB+ of RAM) on gRPC rather than trying to squeeze it through the mailbox.

## Integration with Beebium

### Host configuration

The Tube is an optional peripheral configured at startup, following the pattern established
by `--fdc` (disc controller) and `--station` (Econet). The CLI flag is:

```
--tube <coprocessor-stem>    Attach a Tube coprocessor
```

The stem maps directly to a parasite executable name: `beebium-tube-<stem>`.

| `--tube` value      | Executable                        | Description                    |
|----------------------|-----------------------------------|--------------------------------|
| `65C02-3MHz`         | `beebium-tube-65C02-3MHz`         | Acorn 6502 Second Processor    |
| `65C102-4MHz`        | `beebium-tube-65C102-4MHz`        | Master Turbo (future)          |
| `32016-6MHz-1MB`     | `beebium-tube-32016-6MHz-1MB`     | 32016 Second Processor (future)|

When `--tube` is omitted, the TubeSocket is empty: reads return `&FF` (open bus), writes
are ignored, and OSBYTE &EA correctly reports no Tube present.

No registry is needed (unlike the FDC, which maps names like `acorn-1770` to controller
factories). The stem is simply used to construct the executable name. If the executable
is not found, the server reports an error and exits.

**Executable discovery**: the host looks for the parasite executable in the same directory
as itself (using the build/install `bin/` layout where all Beebium executables are
co-located), falling back to a PATH search.

**Forwarding options to the parasite**: any host CLI option beginning with `--tube-` (note
the trailing hyphen) is forwarded to the parasite process with the `tube-` prefix stripped.
For example, `--tube-rom client.rom` causes the parasite to receive `--rom client.rom`.
This allows the host to pass through parasite-specific options without needing to enumerate
them. The host does not validate forwarded options -- that is the parasite's responsibility.
The `--tube` flag itself (the coprocessor stem) is not forwarded.

#### Socket pattern

The `TubeSocket` follows the same pattern as `DiscControllerSocket` and `EconetSocket`:

- A `HasTubeSocket` C++20 concept detects whether the hardware policy has a Tube socket.
- All three hardware policies (`ModelBHardware`, `ModelBPlusHardware`,
  `ModelBRomRamBoardHardware`) gain a `tube_socket` member, since all real BBC Micros
  had the Tube connector on the underside of the board.
- `install_tube()` in `ServerMain.hpp` checks `HasTubeSocket`, creates shared memory,
  and calls `tube_socket.enable(shared)`.
- `constexpr if` guards Tube-related CLI options, gRPC service registration, and
  shutdown coordination.

```cpp
template<typename T>
concept HasTubeSocket = requires(T& hw) {
    { hw.tube_socket } -> std::same_as<TubeSocket&>;
};
```

#### Two-phase startup

The Tube setup must split across two points in the startup sequence:

- **Phase 1 (before `machine.reset()`)**: create shared memory, populate TubeSocket.
  The socket must be in the memory map before the first instruction executes, because
  the MOS ROM probes `&FEE0-&FEE7` during reset to detect Tube presence.
- **Phase 2 (after `server.start()`)**: launch the parasite subprocess. The parasite
  needs the host's gRPC address to call `TubeService.Connect`, and the actual bound
  port is only known after the gRPC server starts (especially with port 0 for dynamic
  allocation).

Revised startup flow (new steps marked):

```
 1.  Parse CLI arguments -> ServerConfig (including tube_stem)
 2.  Create machine
 3.  Load ROMs (MOS, BASIC, filing system)
 4.  install_disc_controller() if --fdc specified
 5.  install_econet() if --station specified
 6.  install_tube() if --tube specified               <-- NEW: create shm, enable socket
 7.  Load disc images
 8.  Enable video/audio output
 9.  Apply startup options
10.  machine.reset()                                  (MOS detects Tube via register probe)
11.  Create gRPC server, register services (including TubeService)
12.  server.start()
13.  launch_tube_parasite() if --tube specified        <-- NEW: launch subprocess
14.  Run emulation loop
15.  Shutdown: terminate parasite, cleanup shared memory
```

Between steps 10 and 13, the host MOS has detected the Tube and will be polling R1STAT
waiting for the parasite's startup banner. The parasite arrives shortly after step 13
and begins executing its Boot ROM. This brief window where the host is polling an empty
FIFO is harmless -- it matches the real hardware's behaviour when the second processor
is powered on slightly after the host.

### Host-side integration

The host process needs a `TubeSocket` peripheral:

```
TubeSocket
├── implements MemoryMappedDevice (read/write at &FEE0-&FEE7)
├── holds unique_ptr<TubeHostBackend> dispatching to backend
├── methods: enable() [in-process], enable(TubeShared*) [shared memory],
│            disable(), reset(), irq_pending() [HIRQ], stretched()
└── accessors: tube_ula() [cast to TubeUla*], shared_mode() [is TubeHostPort?]
```

**Memory map registration** in all three hardware policies:
```cpp
make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(tube_socket)
```

Note: the Tube uses 3 address bits (A0-A2) to select registers, so the 8-byte register
window is mirrored across the 32-byte Sheila range &FEE0-&FEFF.

**Bus timing**: the Tube is classified as a "fast" device in `BusStretching.hpp` (no 1MHz
stretching). This is already correct in the existing code.

**Interrupt routing**: HIRQ connects to the host CPU's IRQ line via the IRQ aggregator,
as a third binding alongside the System VIA and User VIA:
```cpp
using IrqAggregatorType = IrqAggregator<
    IrqBinding<Via6522, 0>,      // System VIA
    IrqBinding<Via6522, 1>,      // User VIA
    IrqBinding<TubeSocket, 2>    // Tube HIRQ
>;
```
When the socket is empty, `hirq_pending()` always returns false, so the third binding
has no effect.

#### Class hierarchy

`TubeSocket` delegates all register access, IRQ queries, reset, and bus stretching to a
`TubeHostBackend` via a `std::unique_ptr`. Three implementations exist:

```
TubeHostBackend (abstract, virtual)
├── EmptyTubeBackend    -- no second processor (reads return bus value, writes ignored)
├── TubeUla             -- full in-process model (both host and parasite sides)
└── TubeHostPort        -- shared memory adapter (host side only, parasite in another process)
```

- **EmptyTubeBackend**: default state. Returns the last 2 MHz bus value on reads (open bus
  capacitance). No interrupts, no bus stretching. This is what MOS sees when no Tube is
  connected -- OSBYTE &EA correctly reports no Tube.

- **TubeUla**: authoritative FIFO model. Models all four register sets, control flags,
  interrupt computation (HIRQ, PIRQ, PNMI with edge detection), bus stretching with
  pending writes, and reset behaviour. Used for in-process testing (Tier 1 tests) and as
  an alternative to shared memory when both host and parasite run in the same process.
  Exposes `parasite_read()` / `parasite_write()` in addition to the host-side interface.

- **TubeHostPort**: shared memory adapter. Operates on a `TubeShared*` pointer using
  atomic loads and stores. Models only the host's view of the registers. Bus stretching
  uses spin-waits (blocking the host thread) rather than the flag-based approach of
  TubeUla. Used in production when the parasite runs in a separate process.

The socket pattern mirrors `DiscControllerSocket` (which holds `unique_ptr<DiscController>`
dispatching to `NullDiscController` / `WD1770`) and `EconetSocket` (which holds
`unique_ptr<EconetBackend>` dispatching to `EmptyEconetBackend` / `Mc6854`).

**NMI**: the host does not receive NMI from the Tube. The Tube's NMI outputs (PNMI) go to
the parasite only.

**Clock**: the TubeSocket reads directly from shared memory on every register access. For
interrupt responsiveness, a periodic poll (e.g. once per scanline or per instruction) checks
whether HIRQ should be asserted based on the current shared state. No IO thread or staging
queue draining is needed.

**Tube presence detection**: OSBYTE &EA with X=0, Y=&FF returns X=&FF if a Tube is present.
The host-side Tube ROM code handles this; no special emulator support is needed beyond
making the Tube registers respond (which they do when the socket is populated).

### Parasite-side integration

The parasite process is a new executable (`beebium-tube-65C02-3MHz`) containing:

```
beebium-tube-65C02-3MHz (main_tube_65C02_3MHz.cpp)
├── ParasiteRunner -- execution engine
│   ├── ParasiteCpu -- 65C02 CPU (3 MHz, CMOS instruction set)
│   ├── ParasiteMemoryMap -- address decode
│   │   ├── Ram<65536> -- 64 KB RAM
│   │   ├── Rom<2048> -- Boot ROM at &F800-&FFFF (paged out after boot)
│   │   └── TubeParasitePort -- Tube registers at &FEF8-&FEFF
│   └── Interrupt computation (PIRQ, PNMI with NMI handler tracking)
├── Pointer to TubeShared (in shared memory, mapped on startup)
├── Clock pacing loop (independent, targets 3 MHz)
├── gRPC connection to host (for TubeService.Connect handshake)
├── gRPC server (DebuggerControl, SystemService for parasite) [Phase 4, DONE]
└── Service discovery (Bonjour advertisement) [Phase 4, DONE]
```

**Memory map** (implemented by `ParasiteMemoryMap`):
- `&0000-&FEF7`: RAM (64 KB)
- `&FEF8-&FEFF`: Tube registers (`TubeParasitePort`)
- `&FF00-&FFFF`: RAM (or Boot ROM when paged in)

**Boot mode**: on reset, a 2 KB (or 4 KB) Boot ROM is paged in at the top of the address
space. The first access to any Tube register (specifically reading R1STAT at &FEF8) pages
the ROM out, exposing RAM underneath. This matches the hardware boot sequence.

**Interrupt routing**:
- PIRQ connects to the 65C02 IRQ input.
- PNMI connects to the 65C02 NMI input.
- NMI edge detection is handled by `M6502_SetDeviceNMI` (fires on 0-to-1 transitions).
- NMI nesting is prevented by `ParasiteCpu`'s handler tracking: PNMI is not reasserted
  while the CPU is inside an NMI handler (see "NMI handler tracking" above).

**Clock pacing**: the parasite runs its own independent pacing loop targeting 3 MHz
(for 6502 second processor). This uses the same wall-clock pacing mechanism as the host
emulator -- accumulate emulated cycles, sleep if ahead of real time.

### Host-side Tube host code

When a Tube is present, the host MOS needs Tube support code. On real hardware this was
provided by the filing system ROM (NFS, DFS, DNFS, ADFS etc). The host-side code:
- Polls R1STAT and R2STAT for commands from the parasite.
- Executes OS calls on behalf of the parasite.
- Manages R3/R4 block transfers.
- Is loaded into host RAM pages 4-7 during Tube initialisation.

For Beebium, this code is already present in the filing system ROMs. The only requirement is
that the Tube registers respond correctly and the host ROM detects the Tube's presence.

### Parasite Boot ROM and boot mode

The 6502 second processor contains a 4 KB EPROM (IC3, a 2732) of which the top 2 KB is
mapped at `&F800-&FFFF`. This boot ROM contains the parasite-side Tube Operating System
(TOS), labelled "6502 BR" (boot ROM) on the physical chip.

The boot ROM:
1. Copies itself from ROM to underlying RAM at `&F800-&FFFF`.
2. Prints the startup banner "Acorn TUBE 6502 64K" via OSWRCH (R1).
3. Enters the main idle loop polling R1STAT and R2STAT.
4. Handles incoming OS call results.
5. Manages R3/R4 transfer protocols with NMI handlers.
6. Implements the parasite side of all Tube software protocols.

The ROM image (`acorn-tube-6502_1_10.rom`, 2048 bytes) is a binary from the original
hardware and is not part of Beebium's source code.

#### Hardware boot mode

The 6502 second processor has a two-phase boot mechanism controlled by a D-type flip-flop
(IC6, 74LS74). The boot mode latch is set by the power-on reset monostable (IC26, ~100 us
pulse) and cleared by the first access to any Tube register address.

**Boot mode** (from reset until first Tube register access):

- **Clock**: 187.5 kHz (12 MHz / 64). The flip-flop enables a divide-by-16 counter (IC11)
  which gates the divide-by-4 counter (IC12), giving 12 MHz / 4 / 16 = 187.5 kHz on PHI IN.
- **ROM overlay**: the flip-flop enables the ROM chip select (IC3 pin 18).
- **Read/write split**: a NAND gate (IC10 pins 4, 5, 6) suppresses DRAM CAS during read
  cycles (R/W high) but allows CAS during write cycles (R/W low). This means:
  - **Reads** at `&F800-&FFFF` return data from the ROM.
  - **Writes** at `&F800-&FFFF` go to the underlying DRAM.
  - The boot code exploits this to copy itself from ROM to RAM simply by reading each byte
    and writing it back to the same address.
- **Duration**: approximately 0.25 seconds at 187.5 kHz.

**Boot mode termination**:

- When the software accesses any Tube register (`&FEF8-&FEFF`), a NAND gate (IC4, 74LS00)
  decodes the address and resets the flip-flop in IC6. This simultaneously:
  - Deselects the ROM chip.
  - Disables the divide-by-16 counter, allowing IC12 to generate PHI IN at 3 MHz.
- This is a one-way transition: the ROM cannot be re-paged without a hardware reset.
- The first Tube register access is typically the parasite reading R1STAT at `&FEF8` to
  begin communication with the host.

**Normal mode** (after boot mode termination):

- Full 3 MHz clock.
- All memory is RAM.
- The same IC10 NAND gate now suppresses DRAM CAS only when the Tube chip (IC1) is being
  accessed (IC4 pin 9 low). This is how the Tube registers at `&FEF8-&FEFF` "punch through"
  the RAM -- the DRAM is electrically disconnected during Tube accesses.

Reference: *6502 Second Processor Service Manual*, Acorn Computers, Part no 0408,003,
Issue 1, May 1984, Sections 5.1-5.3.

#### Emulation of boot mode

For emulation, the boot mode clock speed difference is cosmetic -- the boot code behaves
identically at any clock rate. The key behaviour to model is the memory map:

```
ParasiteMemoryMap states:

  Boot mode (rom_enabled = true):
    &0000-&F7FF  RAM read/write
    &F800-&FEFF  Read: ROM (2 KB, masked to &7FF)
                 Write: RAM
    &FEF8-&FEFF  Tube registers (override ROM for reads AND writes)
    &FF00-&FFFF  Read: ROM
                 Write: RAM

  Normal mode (rom_enabled = false):
    &0000-&FFFF  RAM read/write
    &FEF8-&FEFF  Tube registers (CAS suppressed, RAM not accessed)
```

The transition from boot mode to normal mode occurs on the first read or write to any
address in `&FEF8-&FEFF`. The `rom_enabled` flag is cleared and cannot be set again
except by reset.

## Tube ULA Model

### Data structures

The `TubeShared` struct lives in the shared memory region and is accessed by both
processes. It uses atomic types for all fields to ensure correct cross-process visibility.

```
TubeShared (in shared memory, cache-line aligned)
├── header
│   ├── magic: u32              // 0x54554245 ("TUBE")
│   └── version: u32
│
├── control_flags: atomic<u8>   // Q, I, J, M, V, P, T (written by host)
│
├── Register 1
│   ├── h2p: { value: atomic<u8>, ready: atomic<u8> }        // host writes
│   └── p2h: { data: [u8; 24], head: atomic<u8>,             // parasite writes
│               tail: atomic<u8>, count: atomic<u8> }
│
├── Register 2
│   ├── h2p: { value: atomic<u8>, ready: atomic<u8> }        // host writes
│   └── p2h: { value: atomic<u8>, ready: atomic<u8> }        // parasite writes
│
├── Register 3
│   ├── h2p: { data: [u8; 2], count: atomic<u8>,             // host writes
│   │           pending: atomic<u8> }
│   └── p2h: { data: [u8; 2], count: atomic<u8>,             // parasite writes
│               pending: atomic<u8> }
│
├── Register 4
│   ├── h2p: { value: atomic<u8>, ready: atomic<u8> }        // host writes
│   └── p2h: { value: atomic<u8>, ready: atomic<u8> }        // parasite writes
│
├── lifecycle_mailbox
│   ├── host_command: atomic<u8>
│   └── parasite_ack: atomic<u8>
│
└── padding to cache line boundary
```

Each process also maintains **local** (non-shared) state for interrupt computation:

```
TubeLocalState (per-process, not shared)
├── in_nmi_handler: bool        // NMI handler tracking (parasite only)
├── last_read_latch: u8         // value returned when FIFO empty
└── boot_mode: bool             // ROM paging state (parasite only)
```

### FIFO implementation

R1 parasite-to-host uses a circular buffer (not a shift register) for O(1) enqueue/dequeue:

```cpp
struct Fifo24 {
    std::array<uint8_t, 24> data;
    uint8_t head = 0;    // read position
    uint8_t tail = 0;    // write position
    uint8_t count = 0;   // number of valid bytes
};
```

R3 uses a simple 2-element array with a count (0, 1, or 2).

All other registers are single-byte latches.

### Interrupt computation

Interrupt outputs are recomputed after every register access:

```
hirq = control.q && r4_p2h.status.data_available

pirq = (control.i && r1_h2p.status.data_available)
    || (control.j && r4_h2p.status.data_available)

if control.m:
    if control.v:  // two-byte mode
        pnmi = (r3_h2p.count == 2) || (r3_p2h.count == 0)
    else:          // one-byte mode
        pnmi = (r3_h2p.count >= 1) || (r3_p2h.count == 0)
else:
    pnmi = false
```

NMI is edge-triggered on the parasite: only fires on 0-to-1 transitions. Edge detection
is handled by the M6502 library's `M6502_SetDeviceNMI`, which internally tracks
`device_nmi_flags` and only sets `nmi_flags` on 0-to-1 transitions of the device mask.

#### NMI handler tracking and nesting prevention

In the cross-process Tube model, the host writes R3 data bytes at native CPU speed
(nanoseconds between writes), far faster than the 16-32 us per byte on real hardware.
Without protection, a second PNMI could fire before the parasite's NMI handler has
finished processing the first byte, corrupting the handler's state.

`ParasiteCpu` tracks whether the CPU is currently inside an NMI handler:

- **Entry**: detected when `cpu.read == M6502ReadType_Interrupt` and `cpu.nmi_flags != 0`
  after the CPU's tick function. At this point the M6502 has begun the interrupt sequence
  but `nmi_flags` has not yet been cleared (that happens at T4). The condition
  `nmi_flags != 0` distinguishes NMI entry from IRQ entry, which is necessary because
  `irq_flags` can be non-zero (PIRQ asserted) simultaneously.

- **Exit**: detected when the CPU is about to execute an RTI instruction
  (`M6502_IsAboutToExecute && dbus == 0x40`).

While `in_nmi_handler` is true, PNMI reassertion is suppressed -- `M6502_SetDeviceNMI`
is not called with the PNMI mask. On NMI entry, `device_nmi_flags` is cleared by calling
`M6502_SetDeviceNMI(mask, 0)`, so that after RTI, reasserting PNMI produces a fresh
0-to-1 edge.

This approach replaces the earlier `prev_pnmi` edge-tracking model, which could not
prevent NMI nesting in the asynchronous cross-process case where multiple PNMI edges
could arrive within a single handler execution.

### Reset state

On reset:
- All control flags cleared to zero.
- All register latches and FIFOs cleared.
- All status flags set to "not full" (writer can write), "data not available" (reader empty).
- R3 parasite-to-host initialised with one dummy byte (count=1) to prevent spurious PNMI.
- `in_nmi_handler` set to false.

## Process Lifecycle

### Startup sequence

See "Two-phase startup" above for how this fits into the host's overall startup flow.
The detailed sequence is:

1. Host `install_tube()`: creates shared memory `/beebium-tube-<machine-uuid>`, maps it,
   initialises `TubeShared` header, populates `TubeSocket` with pointer to shared region.
2. Host `machine.reset()`: MOS probes `&FEE0` and detects Tube present.
3. Host gRPC server starts (with `TubeService` registered).
4. Host `launch_tube_parasite()`: launches `beebium-tube-65C02-3MHz --host=localhost:<port>`.
5. Parasite connects to host gRPC server, calls `TubeService.Connect`.
6. Host returns the shared memory name.
7. Parasite opens and maps the shared memory, validates header.
8. Host deasserts PRST (clears P flag in shared control register).
9. Parasite CPU begins executing from Boot ROM reset vector.
10. Boot ROM prints startup banner via OSWRCH (R1), enters idle loop.
11. Host filing system detects Tube, loads language ROM across R3/R4, issues Execute.
12. Normal operation begins.

### Shutdown sequence

Normal shutdown (host-initiated):

1. Host receives shutdown signal (SIGTERM, gRPC `RequestShutdown`, or window close).
2. Host writes SHUTDOWN to the lifecycle mailbox in shared memory.
3. Parasite detects command (within its polling interval), flushes gRPC streams, exits.
4. Host detects parasite exit via `waitpid()` / `WaitForSingleObject()`.
5. Host unmaps and unlinks shared memory segment.
6. Host continues its own shutdown sequence.

The host's signal handler is wired to trigger parasite shutdown before its own cleanup,
following the existing pattern with `g_request_machine_shutdown` and `g_request_pacing_stop`
function pointers.

### Crash detection

If the parasite process crashes or exits unexpectedly, the host detects this via periodic
`waitpid(WNOHANG)` / `WaitForSingleObject(0)` polling during the emulation loop (e.g.
once per frame). On detection:

1. Host calls `tube_socket.disable()`, reverting to empty-socket behaviour (reads return
   `&FF`, HIRQ deasserted).
2. Host unmaps and unlinks shared memory.
3. Host continues operating without second processor (graceful degradation, mirroring the
   real hardware where you could disconnect the Tube cable).
4. A log message records the crash for diagnostics.

### Save state coordination

1. Host writes FREEZE to the lifecycle mailbox.
2. Parasite detects FREEZE, stops CPU execution, writes ACK.
3. Host calls `TubeService.GetState` via gRPC.
4. Parasite serialises CPU state + RAM and returns it.
5. Host combines its own state with parasite state and shared Tube state into unified
   save file.

Restore:
1. Host creates shared memory and launches fresh parasite process.
2. After `TubeService.Connect`, host calls `TubeService.RestoreState` with saved data.
3. Parasite restores CPU state, RAM, and resumes execution.

## 6502 Second Processor Specifics

### Hardware specification

| Property          | Value                                    |
|-------------------|------------------------------------------|
| CPU               | 65C02 (CMOS, Rockwell or WDC)           |
| Clock speed       | 3 MHz                                   |
| RAM               | 64 KB                                   |
| Boot ROM          | 2 KB                                    |
| ROM address       | &F800-&FFFF (paged out after boot)       |
| Tube registers    | &FEF8-&FEFF                             |
| Reset vector      | &FFFC-&FFFD (in Boot ROM)               |
| IRQ vector        | &FFFE-&FFFF (in RAM after boot)         |
| NMI vector        | &FFFA-&FFFB (in RAM after boot)         |

### Memory map

```
&0000-&00EE   Zero page (available to user code)
&00EF-&00FF   Zero page (used by Tube OS)
&0100-&01FF   Stack
&0200-&02FF   Tube OS workspace
&0300-&03FF   Tube OS workspace
&0400-&07FF   Current language workspace
&0800-&BFFF   User program area (OSHWM to HIMEM)
&C000-&EFFF   Additional user area
&F000-&F7FF   RAM (no ROM here -- always RAM)
&F800-&FEF7   RAM (or Boot ROM when paged in)
  &FEF8-&FEFF Tube registers (always active, even when ROM paged in)
&FF00-&FFFF   RAM (or Boot ROM when paged in; vectors here)
```

### Boot ROM paging

The Boot ROM is a 2 KB (2048-byte) image mapped at `&F800-&FFFF` on the parasite side.
The real hardware uses a single 2 KB EPROM (or a 4 KB EPROM with code in the upper half
and the lower half empty/`&FF`). There is no ROM at `&F000-&F7FF` -- that range is always
RAM.

Some ROM library images circulate as 4 KB files padded with `&FF` in the first 2 KB; these
are full EPROM dumps. Beebium uses the canonical 2 KB image directly (`Rom<2048>` at
`&F800-&FFFF`). The ROM file is `6502Tube.rom`, version 1.10 (MD5 `cd6ba85e22adec70b6d863de4c053db7`).

On reset, the ROM is paged in and the reset vector at `&FFFC-&FFFD` points into it. The
Boot ROM's reset handler copies itself into the underlying RAM before performing its first
Tube register access. The ROM is deselected (paged out, exposing RAM) when the parasite
first reads R1STAT (address `&FEF8`, register offset 0). Since the code is already in RAM
at that point, execution continues seamlessly.

Note: B-Em mirrors the 2 KB ROM across `&F000-&FFFF` (masking with `& 0x7FF`). BeebEm
takes a different approach: it copies the ROM bytes directly into RAM at `&F800` on reset,
avoiding a separate ROM device entirely. Beebium uses a proper `Rom<2048>` with a
`boot_mode` flag that controls whether reads from `&F800-&FFFF` see ROM or RAM.

### 65C02 instruction set

The parasite uses a CMOS 65C02, which adds instructions beyond the NMOS 6502:
- PHX, PLX, PHY, PLY (push/pull index registers)
- STZ (store zero)
- TRB, TSB (test and reset/set bits)
- BRA (unconditional branch)
- BBR, BBS, RMB, SMB (bit manipulation -- Rockwell extensions)
- Indirect addressing without page-crossing bugs
- Unused opcodes are NOPs (not undefined behaviour)

Beebium's existing 6502 library supports CMOS variants.

### Code reuse from the host emulator

The parasite is a much simpler machine than a BBC Micro, but it is still a machine: a CPU,
a memory map, and a handful of peripherals. Much of Beebium's existing infrastructure can
be reused directly or with minor adaptation.

**Reused as-is**:
- **6502 C library** (`src/6502/`): instantiate with `M6502_cmos6502_config` instead of
  the NMOS config. The same cycle-accurate instruction execution, the same interrupt
  handling, the same `M6502_SetDeviceIRQ` / `M6502_SetDeviceNMI` API.
- **Ram<N> template** (`devices/Ram.hpp`): `Ram<65536>` for the parasite's 64 KB.
- **Rom<N> template** (`devices/Rom.hpp`): `Rom<2048>` for the Boot ROM at `&F800-&FFFF`.
- **Memory region/map pattern** (`MemoryRegion.hpp`, `MemoryMap.hpp`): `make_region` for
  address decode, mirroring, first-match dispatch. The parasite memory map is trivial:
  RAM at &0000-&FEF7, Tube at &FEF8-&FEFF, RAM/ROM at &FF00-&FFFF.
- **CpuPolicy** (`CpuPolicy.hpp`): `Cmos65C02` policy already exists.
- **ROM loading utilities**: the same file-loading code used for sideways ROMs and OS ROM
  works for the Boot ROM.

**New but following established patterns**:
- **`ParasiteMemoryMap`**: analogous to the host's memory map but vastly simpler.
  Contains `Ram<65536>`, `Rom<2048>`, a `TubeParasitePort` reference, and a `boot_mode`
  flag controlling ROM overlay. Implements `read()`, `write()`, `reset()`. No VIAs,
  no CRTC, no Video ULA, no sound chip, no disc controller, no Econet.
- **`ParasiteRunner`**: the execution engine for the parasite. Rather than reusing the
  host's `Machine` template (which is tightly coupled to the BBC Micro's tick ordering:
  VIA ticking, VideoBinding, bus stretching, sound chip, Econet NMI), `ParasiteRunner`
  provides a clean, minimal execution loop:
  1. Tick `ParasiteCpu` (one CPU cycle, including interrupt computation).
  2. Check lifecycle mailbox (every N cycles).

  No VIA ticking, no video, no sound, no host-side bus stretching.
- **`ParasiteCpu`**: wrapper around the 6502 C library, instantiated with
  `M6502_cmos6502_config` for the CMOS instruction set. Each `tick()` call:
  1. Executes one CPU cycle via the M6502 tick function.
  2. Performs the memory read/write.
  3. Asserts/deasserts PIRQ from `TubeParasitePort` state.
  4. Detects NMI handler entry (interrupt sequence with `nmi_flags != 0`) and
     exit (RTI opcode), tracking `in_nmi_handler`.
  5. Reasserts PNMI from `TubeParasitePort` state only when not inside an NMI handler.
  6. Advances the cycle count.
- **Clock pacing**: the same wall-clock pacing strategy as the host server (accumulate
  emulated cycles, compare to real time, sleep if ahead), but targeting 3 MHz.
- **gRPC service layer**: the parasite reuses the same `DebuggerControl` and
  `SystemService` proto definitions and C++ implementations as the host via
  `ParasiteServer`. `DeviceInspection` (host-only BBC Micro devices) is not registered.

**Not needed for the parasite**:
- Via6522, SystemViaPeripheral, AddressableLatch
- Crtc6845, VideoUla, Saa5050, FrameRenderer, VideoBinding
- Sn76489, AudioBuffer
- DiscControllerSocket, WD1770
- EconetSocket, Mc6854
- BusStretching (parasite has no 1MHz bus)
- OutputQueue (no video/audio streaming from parasite)
- Indicators (no LEDs)

The parasite executable links against the 6502 library and a subset of the core library
(Ram, Rom, MemoryRegion, MemoryMap). It does not link against the full emulation core
with its VIA, video, and sound dependencies.

## gRPC Service Sharing

The parasite process exposes its own gRPC server so debugger clients and frontends can
interact with it independently. Rather than duplicating proto definitions and service
implementations, Beebium should share as much as possible between host and parasite.

### Services by category

| Service          | Host | Parasite | Sharing strategy                          |
|------------------|------|----------|-------------------------------------------|
| DebuggerControl  | Yes  | Yes      | Split into generic + device-specific       |
| SystemService    | Yes  | Yes      | Reuse as-is                               |
| TubeService      | Yes  | No       | Host-only (connection negotiation)         |
| VideoService     | Yes  | No       | Host-only                                 |
| AudioService     | Yes  | No       | Host-only                                 |
| KeyboardService  | Yes  | No       | Host-only                                 |
| DiscService      | Yes  | No       | Host-only                                 |
| IndicatorService | Yes  | No       | Host-only                                 |
| SidewaysService  | Yes  | No       | Host-only                                 |

### Splitting the debugger service

The current `DebuggerControl` in `debugger.proto` contains two categories of RPC:

**Generic 6502 methods** (work on any 6502 machine):
- Execution control: `GetState`, `Run`, `Stop`, `Reset`, `StepInstruction`, `StepCycle`
- Memory access: `ReadMemory`, `WriteMemory`, `PeekMemory`
- Memory regions: `GetMemoryRegions`, `PeekRegion`, `ReadRegion`, `WriteRegion`
- Breakpoints: `AddBreakpoint`, `RemoveBreakpoint`, `ListBreakpoints`, `ClearBreakpoints`
- CPU state: `Get6502State`, `Set6502State`

**BBC Micro device inspection** (host-specific hardware):
- `GetSystemViaState`, `GetUserViaState`
- `GetCrtcState`, `GetVideoUlaState`
- `GetAddressableLatchState`
- `GetSoundGeneratorState`

The refactoring approach is to split `debugger.proto` into two service definitions:

```protobuf
// debugger.proto -- generic 6502 debugger (shared by host and parasite)
service Cpu6502Debugger {
    // Execution control
    rpc GetState(Empty) returns (ExecutionState);
    rpc Run(Empty) returns (RunResponse);
    rpc Stop(Empty) returns (StopResponse);
    rpc Reset(Empty) returns (ResetResponse);
    rpc StepInstruction(StepRequest) returns (StepResponse);
    rpc StepCycle(StepRequest) returns (StepResponse);

    // Memory access
    rpc ReadMemory(ReadMemoryRequest) returns (ReadMemoryResponse);
    rpc WriteMemory(WriteMemoryRequest) returns (WriteMemoryResponse);
    rpc PeekMemory(PeekMemoryRequest) returns (PeekMemoryResponse);

    // Memory regions
    rpc GetMemoryRegions(GetMemoryRegionsRequest) returns (GetMemoryRegionsResponse);
    rpc PeekRegion(RegionAccessRequest) returns (RegionAccessResponse);
    rpc ReadRegion(RegionAccessRequest) returns (RegionAccessResponse);
    rpc WriteRegion(WriteRegionRequest) returns (WriteRegionResponse);

    // Breakpoints
    rpc AddBreakpoint(AddBreakpointRequest) returns (AddBreakpointResponse);
    rpc RemoveBreakpoint(RemoveBreakpointRequest) returns (RemoveBreakpointResponse);
    rpc ListBreakpoints(Empty) returns (ListBreakpointsResponse);
    rpc ClearBreakpoints(Empty) returns (ClearBreakpointsResponse);

    // CPU state
    rpc Get6502State(Get6502StateRequest) returns (Cpu6502State);
    rpc Set6502State(Set6502StateRequest) returns (Set6502StateResponse);
}

// device_inspection.proto -- BBC Micro hardware (host-only)
service DeviceInspection {
    rpc GetSystemViaState(GetSystemViaStateRequest) returns (ViaState);
    rpc GetUserViaState(GetUserViaStateRequest) returns (ViaState);
    rpc GetCrtcState(GetCrtcStateRequest) returns (CrtcState);
    rpc GetVideoUlaState(GetVideoUlaStateRequest) returns (VideoUlaState);
    rpc GetAddressableLatchState(GetAddressableLatchStateRequest) returns (AddressableLatchState);
    rpc GetSoundGeneratorState(GetSoundGeneratorStateRequest) returns (SoundGeneratorState);
}
```

All message types remain in the same files or a shared messages file -- only the service
definitions are split. The host server registers both `Cpu6502Debugger` and
`DeviceInspection`; the parasite server registers only `Cpu6502Debugger`.

**Backward compatibility**: this is a breaking change to the gRPC service name (from
`DebuggerControl` to `Cpu6502Debugger`). Since there are no stable external clients yet,
this is acceptable. The macOS frontend and Python client are updated at the same time.

**`simulated_pc` field**: the optional `simulated_pc` field on memory read/write requests
exists for Model B+ shadow RAM routing. On the parasite (which has flat 64 KB RAM), this
field is simply ignored. Since it is `optional`, clients that do not use it pay no cost,
and the parasite implementation can omit any shadow RAM logic.

### C++ implementation

The existing `DebuggerServiceImpl` is already templated on `MachineType`:

```cpp
template <typename MachineType>
class DebuggerServiceImpl : public DebuggerControl::Service { ... };
```

After the split, this becomes two classes:

```cpp
template <typename MachineType>
class Cpu6502DebuggerImpl : public Cpu6502Debugger::Service { ... };

template <typename MachineType>
class DeviceInspectionImpl : public DeviceInspection::Service { ... };
```

`Cpu6502DebuggerImpl` works with any machine type that provides the generic interface
(execution control, memory access, CPU state). Both `Machine<ModelBHardware>` and
`ParasiteRunner` satisfy this. `DeviceInspectionImpl` requires the device-specific
accessors and is only instantiated for host machine types.

### SystemService reuse

`SystemService` is already generic. It deals with:
- Machine identity (UUID, name, model type)
- Launch provenance
- Connection tracking
- Shutdown coordination
- mDNS advertisement

None of these are host-specific. The parasite's `SystemService` works identically:
the parasite has its own machine identity (`model_type: "6502Tube"`,
`model_name: "65C02 Second Processor"`), its own UUID, and its own shutdown lifecycle.

The proto `SystemInfo` message even has reserved fields for `cpu_type` and
`coprocessor_type` (lines 94-95 of `system.proto`), anticipating this use case.

No changes to `system.proto` are needed. The existing `SystemServiceImpl` template
can be instantiated for both host and parasite machine types.

### What a unified debugger client sees

A debugger client (Python, macOS frontend, or future standalone debugger) connecting to
either host or parasite sees the same `Cpu6502Debugger` service:

```
Host (beebium-model-b)          Parasite (beebium-tube-65C02-3MHz)
├── Cpu6502Debugger              ├── Cpu6502Debugger        ← same interface
├── DeviceInspection             ├── SystemService          ← same interface
├── SystemService                └── (no other services)
├── VideoService
├── AudioService
├── KeyboardService
├── DiscService
├── IndicatorService
├── SidewaysService
└── TubeService
```

A Python debugging script that sets breakpoints, inspects memory, and steps through
6502 code works unmodified on either processor -- it just connects to a different
gRPC endpoint. Device-specific inspection (VIA registers, CRTC state, etc.) is only
available when connected to the host, and the client can discover this by checking
which services the server advertises.

### Service discovery

The parasite advertises itself via mDNS (Bonjour/Avahi) with a distinct service type
or TXT record field so clients can distinguish host from parasite:

```
Host:     _beebium._tcp  TXT: model=ModelB, role=host, tube=65C02-3MHz
Parasite: _beebium._tcp  TXT: model=65C02-3MHz, role=parasite, host_uuid=<uuid>
```

The host's `tube` TXT field is only present when `--tube` is specified. The `host_uuid`
field in the parasite's advertisement links it to its host, enabling a client to show a
tree view of host + attached coprocessors.

## Future Second Processors

The process-separated architecture makes it straightforward to support additional second
processor types. Each is a new executable, selected via the `--tube` flag:

| `--tube` stem        | Executable                          | Clock   | RAM    |
|----------------------|-------------------------------------|---------|--------|
| `65C02-3MHz`          | `beebium-tube-65C02-3MHz`           | 3 MHz   | 64 KB  |
| `65C102-4MHz`         | `beebium-tube-65C102-4MHz`          | 4 MHz   | 64 KB  |
| `Z80-6MHz`            | `beebium-tube-Z80-6MHz`             | 6 MHz   | 64 KB  |
| `32016-6MHz-1MB`      | `beebium-tube-32016-6MHz-1MB`       | 6 MHz   | 1 MB   |
| `ARM2-8MHz-4MB`       | `beebium-tube-ARM2-8MHz-4MB`        | 8 MHz   | 4 MB   |
| `80186-8MHz-1MB`      | `beebium-tube-80186-8MHz-1MB`       | 8 MHz   | 1 MB   |

The shared memory `TubeShared` structure is processor-agnostic. All register semantics
and software protocols are the same regardless of the parasite processor architecture.

Different processors map the Tube registers at different addresses in their own address
spaces:
- 6502: &FEF8-&FEFF
- Z80: I/O ports or memory-mapped (varies by implementation)
- ARM: &01000000-&0100001C (word-addressed)
- 32016: memory-mapped in 32016 address space

But the underlying shared memory layout is identical. Each parasite executable simply
wires its local address decode to the same `TubeShared` region.

## Implementation Phases

### Phase 1: Tube ULA model and host peripheral -- COMPLETE

- `TubeShared` struct with all register data, control flags, and lifecycle mailbox.
- `TubeUla`: full in-process model with register read/write, FIFO semantics, control
  flag manipulation (S/T/P/V/M/J/I/Q), interrupt computation (HIRQ, PIRQ, PNMI with
  edge detection), bus stretching with pending writes, hard reset and soft reset.
- `TubeHostBackend` abstract interface with `EmptyTubeBackend`, `TubeUla`, and
  `TubeHostPort` implementations.
- `TubeSocket` as a MemoryMappedDevice wrapping `TubeHostBackend`, following the
  `DiscControllerSocket` / `EconetSocket` pattern.
- `HasTubeSocket` C++20 concept. Wired into all three hardware policies.
- HIRQ added to host IRQ aggregator as third binding.
- 23 test cases with 375 assertions in `test_tube_ula.cpp`.

### Phase 2: Shared memory and connection -- COMPLETE

- `TubeHostPort`: host-side shared memory adapter with spin-wait bus stretching.
- `TubeParasitePort`: parasite-side shared memory adapter with interrupt computation.
- `TubeShared` struct used in shared memory region with atomic fields.
- `TubeService` proto and gRPC service implementation (`TubeService.Connect`).
- Lifecycle mailbox (Reset, Shutdown commands via `host_command` / `parasite_ack`).
- `SubprocessManager` for parasite process lifecycle.
- Tests: `test_tube_shared.cpp` (shared struct), `test_tube_host_port.cpp` (host adapter),
  `test_tube_parasite_port.cpp` (parasite adapter), `test_tube_shared_threads.cpp`
  (cross-thread visibility), `test_tube_shared_memory.cpp` (OS-level shared memory),
  `test_tube_socket.cpp` (socket pattern), `test_tube_end_to_end.cpp` (protocol scenarios).

### Phase 3: 6502 parasite process -- COMPLETE

- `beebium-tube-65C02-3MHz` executable (`main_tube_65C02_3MHz.cpp`).
- `ParasiteRunner`: execution engine wrapping 65C02 CPU, 64 KB RAM, Boot ROM, and
  `TubeParasitePort`.
- `ParasiteMemoryMap`: address decode with boot mode ROM overlay at `&F800-&FFFF` and
  Tube registers at `&FEF8-&FEFF`.
- `ParasiteCpu`: 65C02 CPU wrapper using CMOS instruction set.
- Boot ROM loading and paging (ROM paged out on first Tube register access).
- Integration test: `test_boot_tube.cpp` -- full host + parasite boot with interleaved
  execution, verifies "Acorn TUBE 6502 64K" banner on host screen.

### Phase 4: Parasite gRPC services and debugger refactoring -- COMPLETE

- Split `debugger.proto`: `DebuggerControl` (generic, 6502) and `DeviceInspection` (host-only
  BBC Micro devices: VIAs, CRTC, Video ULA, addressable latch, SN76489).
- Extracted `DeviceInspectionServiceImpl` from `DebuggerControlServiceImpl` into
  `DeviceInspectionService.hpp`. `fill_via_state()` helper moved with it.
- Host `Server<T>` registers both `DebuggerControl` and `DeviceInspection`.
- Python client: `Connection` creates both `DebuggerControlStub` and `DeviceInspectionStub`.
  Device wrappers (`crtc.py`, `via.py`, `video_ula.py`, `latch.py`, `sound.py`) accept
  `DeviceInspectionStub`. Swift stubs regenerated (no code changes needed).
- Added debugger interface to `ParasiteRunner`: `sequence()`, `step()`, CPU register
  accessors/setters, `read()`/`write()`/`peek()`, `memory()`, `set_instruction_callback()`.
- Added `ParasiteMemoryMap` region model: `MACHINE_TYPE`, `peek()`, `get_memory_regions()`,
  `peek_region()`, `read_region()`, `write_region()`.
- `ParasiteServer` class: lightweight gRPC server hosting `DebuggerControl` and `SystemService`
  only (no video/audio/keyboard/disc/indicator/sideways/econet/tube services).
- Parasite executable (`main_tube_65C02_3MHz.cpp`) starts `ParasiteServer` with `--port` and
  `--advertise` CLI options. Calls `TubeService.RegisterEndpoint` on the host after its own
  gRPC server starts.
- Coprocessor discovery: `TubeService.RegisterEndpoint` RPC stores the parasite's gRPC
  address. `TubeService.GetStatus` returns `parasite_grpc_address` for client discovery.
  `TubeConnectResponse` includes `host_uuid` for mDNS linkage.
- Parasite mDNS TXT records: `role=parasite`, `processor=65C02`, `clock_mhz=3`,
  `host_uuid=<uuid>`.
- New test file: `test_parasite_grpc.cpp` (9 test cases) verifying debugger RPCs on parasite
  and `DeviceInspection` returning UNIMPLEMENTED.
- `Cpu6502State` proto extended with interrupt handler tracking fields (`in_nmi_handler`,
  `in_irq_handler`, `nmi_pending`, `irq_pending`, `device_irq_flags`, `device_nmi_flags`),
  exposed via `Get6502State` for both host and parasite.

### Phase 5: Save state -- NOT STARTED

- FREEZE/FREEZE_ACK protocol.
- Parasite state serialisation (CPU + RAM + Tube).
- Coordinated save/restore between host and parasite.

## Testing Strategy

Testing is organised into three tiers, with 10 test files currently implemented. All use
Catch2 C++ tests following the same patterns as the rest of the test suite (component
instantiation, `TEST_CASE` with tags, `SECTION` nesting).

### Actual test file inventory

| File                          | Tier | Description                                          |
|-------------------------------|------|------------------------------------------------------|
| `test_tube_ula.cpp`           | 1    | In-process TubeUla model (23 cases, 375 assertions)  |
| `test_tube_shared.cpp`        | 1    | TubeShared struct layout and reset state              |
| `test_tube_host_port.cpp`     | 2    | TubeHostPort shared memory adapter                    |
| `test_tube_parasite_port.cpp` | 2    | TubeParasitePort shared memory adapter                |
| `test_tube_shared_threads.cpp`| 2    | Cross-thread atomic visibility                        |
| `test_tube_shared_memory.cpp` | 2    | OS-level shared memory creation/mapping               |
| `test_tube_socket.cpp`        | 1    | TubeSocket enable/disable/backend dispatch            |
| `test_tube_end_to_end.cpp`    | 2    | End-to-end protocol scenarios over shared memory      |
| `test_boot_tube.cpp`          | 3    | Full host + parasite boot integration (requires ROMs) |
| `test_parasite_grpc.cpp`      | 2    | Parasite gRPC services (DebuggerControl, SystemService)|

### Tier 1: In-process Tube ULA tests (no shared memory, no processors)

These tests instantiate `TubeUla` directly within a single test process. Host-side and
parasite-side access is simulated by calling `host_read` / `host_write` / `parasite_read` /
`parasite_write` directly -- there are no emulated processors, no shared memory mapping,
and no gRPC. This makes the tests fast, deterministic, and easy to debug.

**File**: `test_tube_ula.cpp`
**Tags**: `[tube]`, with sub-tags `[fifo]`, `[flags]`, `[interrupt]`, `[reset]`, `[nmi]`,
`[stretch]`

The 23 test cases cover register access, FIFO semantics, control flags, interrupt
generation, reset behaviour, status register layout, bus stretching, boundary conditions,
and address mirroring. Key test areas include:

- **Register access and FIFO semantics** (7 cases): R1 H-to-P latch, R1 P-to-H 24-byte
  FIFO, R2 single-byte latches, R3 one-byte mode, R3 two-byte mode, R4 single-byte
  latches, R1 FIFO boundary conditions.
- **Control flags** (4 cases): flag set/clear with S bit, V flag R3 mode control, T flag
  soft reset, P flag parasite reset.
- **Interrupt generation** (3 cases): HIRQ (Q flag + R4 P-to-H data), PIRQ (I/J flags +
  R1/R4 H-to-P data), PNMI (M/V flags + R3 data, edge detection).
- **Reset** (1 case): initial state, dummy byte in R3 P-to-H, control flag clearing.
- **Status register layout** (1 case): bit 7/6 semantics from both host and parasite
  perspectives, all four registers.
- **Bus stretching** (6 cases): R1/R3/R4 stretching on full registers, R2 exemption,
  clearing on reset (hard and soft), non-interference (reading unrelated register does
  not clear stretch).
- **Address mirroring** (1 case): 3-bit decode (A0-A2) mirrored across the Sheila range.

### Tier 2: Shared memory and adapter tests

These tests verify the shared memory data structures and the port adapters that access
them. They cover `TubeShared` struct correctness, `TubeHostPort`, `TubeParasitePort`,
cross-thread visibility, OS-level shared memory, the `TubeSocket` dispatch pattern, and
end-to-end protocol scenarios.

**Files**:
- `test_tube_shared.cpp` -- `TubeShared` struct layout, field initialisation, reset state.
- `test_tube_host_port.cpp` -- `TubeHostPort` register access via shared memory atomics,
  including spin-wait bus stretching on R1/R3/R4 and R2 exemption.
- `test_tube_parasite_port.cpp` -- `TubeParasitePort` register access, interrupt
  computation (PIRQ, PNMI with edge detection), boot mode ROM paging.
- `test_tube_shared_threads.cpp` -- cross-thread atomic visibility using `std::thread`,
  verifying acquire/release ordering across the shared region.
- `test_tube_shared_memory.cpp` -- OS-level `shm_open` / `mmap` lifecycle, naming,
  stale segment cleanup.
- `test_tube_socket.cpp` -- `TubeSocket` enable/disable switching between
  `EmptyTubeBackend`, `TubeUla`, and `TubeHostPort`; bus value passthrough when empty.
- `test_tube_end_to_end.cpp` -- protocol-level scenarios: OSWRCH via R1, command
  exchange via R2, block transfers via R3/R4, reset during active transfer.

### Tier 3: Full-system integration tests (real processors, ROMs)

These tests run the complete system: a host `ModelB` machine with Tube enabled and a
`ParasiteRunner` with the real Boot ROM, interleaved on a single thread. They verify
end-to-end behaviour including the MOS Tube detection sequence, OSWRCH banner transfer,
and language loading.

**File**: `test_boot_tube.cpp`
**Tags**: `[boot][tube]`

The tests require MOS, BASIC, DNFS, and Tube Boot ROMs (they skip gracefully if
unavailable, following the pattern established by `test_boot.cpp`).

```
TEST_CASE("Model B with 65C02 second processor boots with Tube banner")
  -- Full interleaved boot: verifies "Acorn TUBE 6502 64K" on screen,
     no "BBC Computer 32K", BASIC prompt appears.

TEST_CASE("Model B with Tube shows 64K memory (not 32K)")
  -- Verifies the banner includes "64K" (parasite memory size).

TEST_CASE("Model B without Tube boots normally")
  -- Control case: no TubeSocket enabled, normal "BBC Computer 32K" boot.
```

The interleaved boot uses a 2:3 host:parasite instruction ratio to approximate the
2 MHz / 3 MHz clock speed difference. This ratio is a property of the single-threaded
test harness, not of the Tube protocol. In production, the host and parasite run on
separate threads and bus stretching (spin-waits in `TubeHostPort`) handles backpressure
regardless of clock ratio.

#### Future integration tests (planned)

Cross-process integration tests (launching `beebium-tube-65C02-3MHz` as a real subprocess
via the gRPC `TubeService.Connect` handshake) and end-to-end smoke tests (running BASIC
programs, disc loading, dual debugger sessions) are planned for future phases.

### Which tests run when

| Tier | Speed    | Requires ROMs | Requires shared memory | When to run          |
|------|----------|---------------|------------------------|----------------------|
| 1    | Fast     | No            | No                     | Always (CI + local)  |
| 2    | Fast     | No            | Varies by test         | Always (CI + local)  |
| 3    | Moderate | Yes           | No (heap-based)        | When ROMs available  |

Tier 1 and Tier 2 tests have no external dependencies and run in every CI build.
Tier 3 tests require ROM images and skip gracefully if unavailable, following the
pattern established by `test_boot.cpp`.

## References

- Acorn Tube Application Note 004 (`docs/datasheets/Tube_Application_Note_004.pdf`)
- Acorn Tube Software Protocol Specification (ref: SOtube8, included in Application Note)
- BBC Advanced User Guide, Chapter 27: The Tube
- BBC Master Advanced User Guide, Chapter 18: 2nd Proc/Tube
- BeebEm source: `Src/Tube.cpp`, `Src/Tube.h`
- B-Em source: `src/tube.h`, `src/tube.c`, `src/6502tube.c`
- B2 source: `src/beeb/include/beeb/tube.h`, `src/beeb/src/tube.cpp`
- jsbeeb source: `src/tube.js`
- Beebium design discussion: `docs/discussion/emulating-the-tube.md`
