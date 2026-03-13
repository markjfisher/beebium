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
│  │ TubeHost         │  │          │  │ TubeParasite     │  │
│  │ (local wrapper)  │  │          │  │ (local wrapper)  │  │
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
               │  │ TubeUla (lock-free)    │  │
               │  │                        │  │
               │  │  FIFO data + counts    │  │
               │  │  Status flags          │  │
               │  │  Control flags         │  │
               │  └────────────────────────┘  │
               │                              │
               │  Lifecycle mailbox (cold)    │
               └──────────────────────────────┘
              Setup via Unix domain socket
              (handshake only, then closed)
```

The Tube ULA state lives in a **shared memory region** mapped into both processes. Both CPU
threads access the FIFOs and status flags directly through lock-free atomic operations. No
IO threads, no staging queues, no syscalls on the data path.

A Unix domain socket is used only for the initial handshake: the host creates the shared
memory segment, the parasite connects, the host passes the shared memory file descriptor
(via `SCM_RIGHTS` on Unix, or a named shared memory path), and then the socket can be
closed. Lifecycle messages (reset, freeze, shutdown) use a small mailbox within the shared
memory itself.

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
TubeShared (cache-line aligned, ~256 bytes)
├── Header
│   ├── magic: u32              // validation
│   └── version: u32            // protocol version
│
├── Control (written by host, read by both)
│   └── flags: atomic<u8>      // Q, I, J, M, V, P, T
│
├── H-to-P registers (written by host, read by parasite)
│   ├── r1_h2p: { value: atomic<u8>, ready: atomic<bool> }
│   ├── r2_h2p: { value: atomic<u8>, ready: atomic<bool> }
│   ├── r3_h2p: { data: [atomic<u8>; 2], count: atomic<u8> }
│   └── r4_h2p: { value: atomic<u8>, ready: atomic<bool> }
│
├── P-to-H registers (written by parasite, read by host)
│   ├── r1_p2h: { data: [atomic<u8>; 24], head: atomic<u8>,
│   │              tail: atomic<u8>, count: atomic<u8> }
│   ├── r2_p2h: { value: atomic<u8>, ready: atomic<bool> }
│   ├── r3_p2h: { data: [atomic<u8>; 2], count: atomic<u8> }
│   └── r4_p2h: { value: atomic<u8>, ready: atomic<bool> }
│
├── Lifecycle mailbox (for reset/freeze/shutdown)
│   ├── host_command: atomic<u8>
│   └── parasite_ack: atomic<u8>
│
└── Padding to fill cache lines
```

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
a power-of-two-friendly size, this is a standard lock-free SPSC ring buffer.

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
5. Assert/deassert PIRQ and PNMI (with edge detection for NMI).

No explicit interrupt signalling crosses the process boundary. Each side derives interrupt
state from the shared register data, exactly as the real Tube ULA does internally.

### Backpressure

When the host writes to a register that is already full (the parasite hasn't read the
previous value), the host sees `not_full = false` by reading the shared state. The emulated
6502 polls in a tight loop, just as real hardware did. The shared memory read completes in
nanoseconds, so this polling is cheap.

Similarly, when the parasite reads from an empty register, it sees `data_available = false`
and polls. No special flow control protocol is needed -- the FIFO structure itself provides
backpressure, as it did in hardware.

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
├── pointer to TubeShared (in shared memory region, nullptr when empty)
├── local interrupt state (HIRQ computation)
└── methods: enable(TubeShared*), disable(), reset(), hirq_pending()
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
beebium-tube-65C02-3MHz
├── 65C02 CPU emulator (3 MHz, CMOS instruction set)
├── 64 KB RAM
├── Boot ROM (2 KB at &F800-&FFFF, paged out after first Tube register access)
├── Pointer to TubeShared (in shared memory, mapped on startup)
├── Local interrupt state (PIRQ, PNMI computation with edge detection)
├── Clock pacing loop (independent, targets 3 MHz)
├── gRPC connection to host (for TubeService.Connect handshake)
├── gRPC server (DebuggerService, SystemService for parasite)
└── Service discovery (Bonjour advertisement)
```

**Memory map**:
- `&0000-&FEF7`: RAM
- `&FEF8-&FEFF`: Tube registers (TubeParasiteModel)
- `&FF00-&FFFF`: RAM (or Boot ROM when paged in)

**Boot mode**: on reset, a 2 KB (or 4 KB) Boot ROM is paged in at the top of the address
space. The first access to any Tube register (specifically reading R1STAT at &FEF8) pages
the ROM out, exposing RAM underneath. This matches the hardware boot sequence.

**Interrupt routing**:
- PIRQ connects to the 65C02 IRQ input.
- PNMI connects to the 65C02 NMI input.
- NMI requires edge detection: only trigger on 0-to-1 transitions of the PNMI signal.

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
│   ├── h2p: { data: [u8; 2], count: atomic<u8> }            // host writes
│   └── p2h: { data: [u8; 2], count: atomic<u8> }            // parasite writes
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
├── prev_pnmi: bool             // for NMI edge detection (parasite only)
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

NMI is edge-triggered on the parasite: only fires on 0-to-1 transitions. The model must
track `prev_pnmi` to detect edges.

### Reset state

On reset:
- All control flags cleared to zero.
- All register latches and FIFOs cleared.
- All status flags set to "not full" (writer can write), "data not available" (reader empty).
- R3 parasite-to-host initialised with one dummy byte (count=1) to prevent spurious PNMI.
- `prev_pnmi` set to false.

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
- **ParasiteHardware policy**: a new hardware policy analogous to `ModelBHardware`, but
  vastly simpler. Contains only `Ram<65536>`, `Rom<2048>`, a Tube peripheral reference,
  and a `boot_mode` flag. Implements `read()`, `write()`, `reset()`. No VIAs, no CRTC,
  no Video ULA, no sound chip, no disc controller, no Econet.
- **ParasiteMachine template**: the existing `Machine` template is tightly coupled to the
  BBC Micro's specific tick ordering (VIA ticking, VideoBinding, bus stretching, sound
  chip, Econet NMI). The parasite needs none of this. Rather than adding conditionals
  to `Machine`, a dedicated `ParasiteMachine` template provides a clean, minimal
  execution loop:
  1. Execute CPU instruction.
  2. Compute PIRQ and PNMI from shared Tube state.
  3. Assert/deassert interrupt lines on CPU.
  4. Check lifecycle mailbox (every N instructions).
  5. Advance cycle count.

  No VIA ticking, no video, no sound, no bus stretching. This is far simpler than
  `Machine` -- perhaps 100 lines vs 500+.
- **Clock pacing**: the same wall-clock pacing strategy as the host server (accumulate
  emulated cycles, compare to real time, sleep if ahead), but targeting 3 MHz.
- **gRPC service layer**: the parasite reuses the same `Cpu6502Debugger` and
  `SystemService` proto definitions and C++ implementations as the host. See the
  "gRPC Service Sharing" section for details.

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
`ParasiteMachine<ParasiteHardware>` satisfy this. `DeviceInspectionImpl` requires the
device-specific accessors and is only instantiated for host machine types.

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

### Phase 1: Tube ULA model and host peripheral

- Define `TubeShared` struct (can be tested in-process initially, without shared memory).
- Implement all register read/write logic with correct FIFO semantics.
- Implement interrupt computation (HIRQ, PIRQ, PNMI with edge detection).
- Implement TubeSocket as a MemoryMappedDevice wrapping the host side.
- Wire into all three hardware policies.
- Add HIRQ to host IRQ aggregator.
- **All Tier 1 tests pass** (register access, FIFO semantics, flags, interrupts, reset).
- No shared memory or parasite process yet -- test with `TubeShared` on the heap.

### Phase 2: Shared memory and connection

- Implement shared memory creation/mapping (`shm_open`/`mmap`/platform abstraction).
- Define `TubeService` proto and implement in gRPC service layer.
- Implement `TubeService.Connect` RPC (returns shared memory name).
- Implement lifecycle mailbox (reset/freeze/shutdown).
- **All Tier 2 tests pass** (shared memory lifecycle, cross-thread visibility, stress,
  lifecycle mailbox, TubeService handshake).

### Phase 3: 6502 parasite process

- Create `beebium-tube-65C02-3MHz` executable.
- 65C02 CPU with 64 KB RAM and Boot ROM.
- gRPC client for `TubeService.Connect`.
- Map shared memory, wire Tube registers to parasite address space.
- Independent clock pacing loop.
- Boot ROM loading and paging logic.
- **Tier 3 in-process tests pass** (boot sequence, language transfer, OSWRCH/OSRDCH,
  block transfers, reset, Escape handling).
- **Tier 3 cross-process tests pass** (subprocess launch, boot, shutdown, crash recovery).

### Phase 4: Parasite gRPC services and debugger refactoring

- Split `debugger.proto` into `Cpu6502Debugger` (generic) and `DeviceInspection` (host-only).
- Split `DebuggerServiceImpl` into `Cpu6502DebuggerImpl` and `DeviceInspectionImpl`.
- Update host server registration to use both new services.
- Update macOS frontend and Python client for new service names.
- Instantiate `Cpu6502DebuggerImpl` and `SystemServiceImpl` for the parasite.
- Parasite gRPC server with its own port, identity, and mDNS advertisement.
- Service discovery with `role=parasite` and `host_uuid` TXT fields.
- **Tier 3 cross-process debugger and SystemService tests pass**.
- **End-to-end smoke tests pass** (BASIC program, disc load, dual debug session).

### Phase 5: Save state

- FREEZE/FREEZE_ACK protocol.
- Parasite state serialisation (CPU + RAM + Tube).
- Coordinated save/restore between host and parasite.

## Testing Strategy

Testing is organised into three tiers. The first two tiers use Catch2 C++ tests following
the same patterns as the rest of the test suite (component instantiation, `TEST_CASE` with
tags, `SECTION` nesting). The third tier adds process-level integration tests.

### Tier 1: In-process Tube ULA tests (no shared memory, no processors)

These tests instantiate `TubeShared` on the stack or heap within a single test process.
Host-side and parasite-side access is simulated by calling the appropriate read/write
functions directly -- there are no emulated processors, no shared memory mapping, and no
gRPC. This makes the tests fast, deterministic, and easy to debug.

**File**: `test_tube_ula.cpp`
**Tags**: `[tube]`, with sub-tags `[fifo]`, `[flags]`, `[interrupt]`, `[reset]`, `[nmi]`

#### Register access and FIFO semantics

```
TEST_CASE("R1 host-to-parasite latch", "[tube][fifo][r1]")
  SECTION("Initial state: not-full on host side, empty on parasite side")
  SECTION("Host write followed by parasite read returns same byte")
  SECTION("Host write sets parasite data-available flag")
  SECTION("Parasite read clears data-available flag")
  SECTION("Second host write before parasite read overwrites (1-byte latch)")

TEST_CASE("R1 parasite-to-host 24-byte FIFO", "[tube][fifo][r1]")
  SECTION("Initial state: empty on host side, not-full on parasite side")
  SECTION("Single byte round-trip: parasite write, host read")
  SECTION("FIFO ordering: 3 bytes written are read in same order")
  SECTION("Fill to 24 bytes: not-full flag clears on 24th write")
  SECTION("Write when full: byte is dropped, FIFO contents unchanged")
  SECTION("Read when empty: returns last byte read, flags unchanged")
  SECTION("Drain: each host read advances FIFO, empty flag sets on last")
  SECTION("Fill and drain cycle: repeatable without state leakage")
  SECTION("Partial fill/drain interleaving")

TEST_CASE("R2 single-byte latches", "[tube][fifo][r2]")
  SECTION("Host-to-parasite: write then read")
  SECTION("Parasite-to-host: write then read")
  SECTION("Overwrite before read replaces value")
  SECTION("Read without prior write returns undefined/zero")

TEST_CASE("R3 one-byte mode", "[tube][fifo][r3]")
  SECTION("Default after reset is one-byte mode (V flag clear)")
  SECTION("Host-to-parasite single byte")
  SECTION("Parasite-to-host single byte")

TEST_CASE("R3 two-byte mode", "[tube][fifo][r3]")
  SECTION("Set V flag to enable two-byte mode")
  SECTION("Host-to-parasite: two bytes required before data-available set")
  SECTION("First byte alone does not set data-available")
  SECTION("Second byte sets data-available; parasite reads both in order")
  SECTION("Parasite-to-host: symmetric two-byte behaviour")
  SECTION("Switch from two-byte to one-byte mode mid-transfer")

TEST_CASE("R4 single-byte latches", "[tube][fifo][r4]")
  SECTION("Host-to-parasite: write then read")
  SECTION("Parasite-to-host: write then read")
```

#### Control flags

```
TEST_CASE("Control register flag manipulation", "[tube][flags]")
  SECTION("S=0 clears bits specified by D0-D6")
  SECTION("S=1 sets bits specified by D0-D6")
  SECTION("Individual flag set/clear: Q, I, J, M, V, P, T")
  SECTION("Multiple flags set in one write")
  SECTION("Multiple flags cleared in one write")
  SECTION("Setting and clearing in sequence")

TEST_CASE("V flag controls R3 FIFO mode", "[tube][flags][r3]")
  SECTION("V=0: R3 operates as 1-byte latch")
  SECTION("V=1: R3 operates as 2-byte FIFO")
  SECTION("Changing V resets R3 FIFO state")

TEST_CASE("T flag controls HIRQ from R4", "[tube][flags][interrupt]")
  SECTION("T=1 enables HIRQ when R4 parasite-to-host has data")
  SECTION("T=0 disables HIRQ from R4 regardless of R4 state")

TEST_CASE("P flag triggers parasite reset", "[tube][flags][reset]")
  SECTION("Setting P clears all FIFOs")
  SECTION("Setting P places dummy byte in R3 parasite-to-host")
  SECTION("Setting P resets all status flags to initial state")
```

#### Interrupt generation

```
TEST_CASE("HIRQ generation", "[tube][interrupt][hirq]")
  SECTION("No HIRQ when no flags enabled and no data pending")
  SECTION("HIRQ asserted when T=1 and R4 parasite-to-host has data")
  SECTION("HIRQ deasserted when R4 parasite-to-host data read by host")
  SECTION("HIRQ deasserted when T cleared")
  SECTION("HIRQ with Q flag (R1 parasite-to-host not empty)")
  SECTION("HIRQ not affected by R2 or R3 state")
  SECTION("Multiple simultaneous HIRQ sources")

TEST_CASE("PIRQ generation", "[tube][interrupt][pirq]")
  SECTION("No PIRQ when no flags enabled")
  SECTION("PIRQ when I=1 and R1 host-to-parasite has data")
  SECTION("PIRQ when J=1 and R4 host-to-parasite has data")
  SECTION("PIRQ deasserted when R1 data read by parasite")
  SECTION("PIRQ deasserted when R4 data read by parasite")
  SECTION("Both I and J active simultaneously")

TEST_CASE("PNMI generation", "[tube][interrupt][pnmi]")
  SECTION("No PNMI when M=0")
  SECTION("PNMI when M=1 and R3 host-to-parasite has data")
  SECTION("PNMI is edge-sensitive: only triggers on 0-to-1 transition")
  SECTION("No re-trigger if PNMI already high when new data arrives")
  SECTION("PNMI clears when R3 data read by parasite")
  SECTION("New PNMI edge after clear and fresh data arrival")
  SECTION("V flag interaction: in two-byte mode, PNMI after second byte only")
```

#### Reset behaviour

```
TEST_CASE("Tube reset state", "[tube][reset]")
  SECTION("All FIFOs empty after reset")
  SECTION("All control flags clear after reset")
  SECTION("R3 parasite-to-host contains one dummy byte after reset")
  SECTION("Dummy byte prevents spurious PNMI on first R3 host-to-parasite write")
  SECTION("No HIRQ or PIRQ asserted after reset")
  SECTION("Reset during active transfer clears mid-transfer state")
```

#### Status register read format

```
TEST_CASE("Status register bit layout", "[tube][status]")
  SECTION("R1STAT bit 7: parasite-to-host FIFO not empty (host perspective)")
  SECTION("R1STAT bit 6: host-to-parasite latch not full (host perspective)")
  SECTION("Parasite R1STAT bit 7: host-to-parasite data available")
  SECTION("Parasite R1STAT bit 6: parasite-to-host FIFO not full")
  SECTION("Status bits update immediately after data write")
  SECTION("Status bits update immediately after data read")
  SECTION("All four register status bytes report correct empty/full state")
```

#### Stress and boundary conditions

```
TEST_CASE("R1 FIFO boundary conditions", "[tube][fifo][boundary]")
  SECTION("Rapid alternating read/write at FIFO depth boundary")
  SECTION("Fill to exactly 23, verify not-full, add 24th, verify full")
  SECTION("Read one from full FIFO, verify not-full, write one, verify full again")
  SECTION("Interleaved host reads and parasite writes at varying rates")

TEST_CASE("Register access ordering", "[tube][ordering]")
  SECTION("Status read followed by data read is atomic (no race)")
  SECTION("Status shows data-available, data read returns that data")
  SECTION("Write to data register, immediate status read reflects new state")
```

### Tier 2: Shared memory tests (cross-process, no emulated processors)

These tests verify that the shared memory transport works correctly. `TubeShared` is
placed in a real shared memory region and accessed from two threads (or optionally two
processes via a test helper). No emulated CPUs run -- the tests call read/write functions
directly, as in Tier 1, but through the shared memory mapping.

**File**: `test_tube_shared_memory.cpp`
**Tags**: `[tube][shm]`

#### Shared memory lifecycle

```
TEST_CASE("Shared memory creation and mapping", "[tube][shm][lifecycle]")
  SECTION("Create shared memory region with expected size")
  SECTION("Map into second mapping, both see same data")
  SECTION("Header magic number and version are correct")
  SECTION("Alignment: H-to-P and P-to-H regions on separate cache lines")
  SECTION("Unmap and destroy: no leaks, region no longer accessible")

TEST_CASE("Shared memory naming and cleanup", "[tube][shm][lifecycle]")
  SECTION("Name includes host machine UUID for uniqueness")
  SECTION("Stale region from crashed process can be unlinked and recreated")
  SECTION("Two simultaneous Tube connections use distinct region names")
```

#### Cross-thread data visibility

These use `std::thread` to verify that atomic operations on the shared region provide
the expected ordering guarantees.

```
TEST_CASE("Cross-thread register writes are visible", "[tube][shm][atomics]")
  SECTION("Host writes R1 data, parasite thread reads R1 data")
  SECTION("Parasite writes R1 data, host thread reads R1 data")
  SECTION("Flag updates visible across threads after data write")
  SECTION("R1 24-byte FIFO: producer thread fills, consumer thread drains")

TEST_CASE("Single-writer principle holds", "[tube][shm][atomics]")
  SECTION("Only host writes to H-to-P fields; parasite reads are consistent")
  SECTION("Only parasite writes to P-to-H fields; host reads are consistent")

TEST_CASE("Cross-thread R3 two-byte mode", "[tube][shm][atomics]")
  SECTION("Two bytes written by host thread, both read by parasite thread")
  SECTION("PNMI flag only set after second byte visible to parasite thread")
```

#### Throughput stress tests

```
TEST_CASE("Sustained throughput via R1 FIFO", "[tube][shm][stress]")
  SECTION("10000 bytes through R1: producer and consumer threads, all received in order")
  SECTION("No lost or duplicated bytes under contention")

TEST_CASE("Sustained throughput via R3 two-byte mode", "[tube][shm][stress]")
  SECTION("Block transfer: 256 two-byte pairs, all received correctly")
```

#### Lifecycle mailbox

```
TEST_CASE("Lifecycle mailbox commands", "[tube][shm][lifecycle]")
  SECTION("RESET command: host sets, parasite acknowledges")
  SECTION("FREEZE command: host sets, parasite acknowledges with state snapshot")
  SECTION("SHUTDOWN command: host sets, parasite acknowledges and prepares to exit")
  SECTION("Commands are sequenced: no command lost under rapid succession")
  SECTION("Parasite detects command within bounded poll interval")
```

#### Platform-specific tests

```
TEST_CASE("Shared memory portability", "[tube][shm][platform]")
  SECTION("Region survives across fork (POSIX) or process creation (Windows)")
  SECTION("std::atomic_ref operations on mapped memory have correct semantics")
```

### Tier 3: Integration tests (full system with processors, shared memory, gRPC)

These tests run the complete system: a host machine (with Tube enabled), shared memory,
the gRPC TubeService handshake, and a parasite process (or an in-process parasite for
determinism). They verify end-to-end behaviour including the Tube software protocols.

Two sub-tiers exist: **in-process integration** (parasite runs on a separate thread
within the test process, for determinism and debuggability) and **cross-process
integration** (parasite runs as a real `beebium-tube-65C02-3MHz` subprocess, as in production).

#### In-process integration tests

**File**: `test_tube_integration.cpp`
**Tags**: `[tube][integration]`

These instantiate both a host `Machine<ModelBHardware>` and a `ParasiteMachine` in the
same process, connected via `TubeShared` on the heap (no shared memory mapping needed).
A test driver alternates stepping both machines, simulating asynchronous execution.

```
TEST_CASE("Tube presence detection", "[tube][integration]")
  SECTION("Host MOS OSBYTE &EA reports Tube present")
  SECTION("Tube registers at &FEE0-&FEE7 respond to host reads/writes")

TEST_CASE("Parasite boot sequence", "[tube][integration][boot]")
  SECTION("Parasite executes Boot ROM reset vector")
  SECTION("Boot ROM sends startup banner via R1 (OSWRCH)")
  SECTION("Host receives banner characters from R1")
  SECTION("First R1STAT read pages out Boot ROM, RAM visible underneath")
  SECTION("Parasite sends zero byte to signal ready for language")

TEST_CASE("Language transfer", "[tube][integration][boot]")
  SECTION("Host sends language ROM bytes via R3 block transfer")
  SECTION("PNMI fires for each byte/pair, parasite NMI handler stores to RAM")
  SECTION("Transfer completes: parasite has language ROM in RAM")
  SECTION("Parasite executes language entry point")

TEST_CASE("OSWRCH across Tube", "[tube][integration][protocol]")
  SECTION("Parasite writes character to R1 data")
  SECTION("Host-side Tube code polls R1STAT, reads character")
  SECTION("Character appears in host screen memory")
  SECTION("Multiple characters form readable text")

TEST_CASE("OSRDCH across Tube", "[tube][integration][protocol]")
  SECTION("Parasite requests character via R2")
  SECTION("Host reads keyboard, sends character back via R2")
  SECTION("Parasite receives character")

TEST_CASE("OSCLI across Tube", "[tube][integration][protocol]")
  SECTION("Parasite sends command string via R2")
  SECTION("Host executes command (e.g., *CAT)")
  SECTION("Result communicated back to parasite")

TEST_CASE("OSBYTE/OSWORD across Tube", "[tube][integration][protocol]")
  SECTION("OSBYTE call: parasite sends via R2, host executes, result returned")
  SECTION("OSWORD call: parameter block transferred via R3")

TEST_CASE("R3 block transfer (NMI-driven)", "[tube][integration][transfer]")
  SECTION("Type 0: 256-byte block transfer parasite-to-host")
  SECTION("Type 1: 256-byte block transfer host-to-parasite")
  SECTION("Each byte triggers PNMI on parasite for NMI handler")
  SECTION("Transfer completes with correct data in destination memory")

TEST_CASE("R4 single-byte transfer", "[tube][integration][transfer]")
  SECTION("Single byte host-to-parasite via R4 with PIRQ")
  SECTION("Single byte parasite-to-host via R4 with HIRQ")

TEST_CASE("Tube reset during operation", "[tube][integration][reset]")
  SECTION("Host asserts P flag: parasite re-enters boot sequence")
  SECTION("Mid-transfer reset: no stale data in FIFOs after reset")
  SECTION("Parasite re-boots and re-requests language")

TEST_CASE("Escape handling across Tube", "[tube][integration][protocol]")
  SECTION("Host sets Escape condition, parasite receives via R1/R4")
  SECTION("Parasite acknowledges Escape")

TEST_CASE("Interrupt behaviour under load", "[tube][integration][interrupt]")
  SECTION("HIRQ fires promptly when parasite writes to R4 with T=1")
  SECTION("PIRQ fires when host writes to R1 with I=1")
  SECTION("PNMI edge detection correct during rapid R3 transfers")
  SECTION("No spurious interrupts during idle periods")
```

#### Cross-process integration tests

**File**: `test_tube_cross_process.cpp`
**Tags**: `[tube][integration][process]`

These launch `beebium-tube-65C02-3MHz` as a real subprocess via the gRPC `TubeService.Connect`
handshake. They are slower and less deterministic than the in-process tests, but verify
the production code path.

```
TEST_CASE("TubeService.Connect handshake", "[tube][integration][process][grpc]")
  SECTION("Host gRPC server accepts Connect request from parasite client")
  SECTION("Response contains shared memory region name")
  SECTION("Parasite maps shared memory successfully")
  SECTION("Both sides can exchange data through mapped TubeShared")

TEST_CASE("Parasite process launch and boot", "[tube][integration][process]")
  SECTION("beebium-tube-65C02-3MHz starts and connects to host gRPC server")
  SECTION("Parasite boot banner appears in host OSWRCH output")
  SECTION("Parasite's own gRPC server starts and is reachable")
  SECTION("Parasite advertises itself via mDNS with role=parasite")

TEST_CASE("Parasite debugger via gRPC", "[tube][integration][process][grpc]")
  SECTION("Connect to parasite's Cpu6502Debugger service")
  SECTION("GetState reports parasite is running")
  SECTION("Stop pauses parasite execution")
  SECTION("ReadMemory returns parasite RAM contents")
  SECTION("Get6502State returns parasite CPU registers")
  SECTION("StepInstruction advances parasite by one instruction")
  SECTION("AddBreakpoint + Run: parasite stops at breakpoint address")
  SECTION("Same debugger client code works against host and parasite")

TEST_CASE("Parasite SystemService via gRPC", "[tube][integration][process][grpc]")
  SECTION("GetSystemInfo returns parasite identity (model=6502Tube)")
  SECTION("SetMachineName updates parasite name")
  SECTION("WatchServerStatus stream connects and receives READY")

TEST_CASE("Host-initiated parasite shutdown", "[tube][integration][process]")
  SECTION("Host sends SHUTDOWN via lifecycle mailbox")
  SECTION("Parasite acknowledges and terminates cleanly")
  SECTION("Shared memory region is cleaned up")
  SECTION("Parasite's gRPC server stops")
  SECTION("Parasite's mDNS advertisement is withdrawn")

TEST_CASE("Parasite crash recovery", "[tube][integration][process]")
  SECTION("Parasite process killed: host detects stale shared memory")
  SECTION("Host cleans up shared memory region")
  SECTION("Host can accept a new TubeService.Connect for a fresh parasite")

TEST_CASE("Multiple simultaneous parasites", "[tube][integration][process]")
  SECTION("Two TubeService.Connect calls get distinct shared memory regions")
  SECTION("Each parasite boots independently")
  SECTION("Host manages both Tube register sets correctly")
```

#### End-to-end smoke tests

These are high-level scenario tests that verify real-world use cases.

```
TEST_CASE("Run BASIC program on second processor", "[tube][integration][e2e]")
  SECTION("Boot host + parasite, BASIC starts on parasite")
  SECTION("Type and RUN a simple program (e.g., PRINT 2+2)")
  SECTION("Output appears on host display")

TEST_CASE("Load and run program from disc on second processor", "[tube][integration][e2e]")
  SECTION("Mount disc image containing a Tube-compatible program")
  SECTION("*RUN transfers program to parasite via Tube")
  SECTION("Program executes on parasite CPU")

TEST_CASE("Debug session across host and parasite", "[tube][integration][e2e]")
  SECTION("Connect debugger to host, set breakpoint in Tube polling loop")
  SECTION("Connect debugger to parasite, set breakpoint in user code")
  SECTION("Both breakpoints fire independently")
  SECTION("Inspect memory on both sides simultaneously")
```

### Test helpers

**File**: `test_tube_helpers.hpp`
**Tags**: N/A (header only)

Shared utilities for Tube tests across all three tiers:

```cpp
// Create a TubeShared on the heap with reset state applied.
// Suitable for Tier 1 and in-process Tier 3 tests.
std::unique_ptr<TubeShared> make_tube();

// Host-side and parasite-side register access wrappers.
// These mirror what TubeSocket and ParasiteTubeDevice do internally,
// but are free functions for test convenience.
uint8_t host_read(TubeShared& tube, uint8_t reg);
void host_write(TubeShared& tube, uint8_t reg, uint8_t value);
uint8_t parasite_read(TubeShared& tube, uint8_t reg);
void parasite_write(TubeShared& tube, uint8_t reg, uint8_t value);

// Interrupt state queries.
bool hirq_asserted(const TubeShared& tube);
bool pirq_asserted(const TubeShared& tube);
bool pnmi_asserted(const TubeShared& tube);

// Convenience: write a string byte-by-byte through R1 (parasite-to-host).
void send_string_via_r1(TubeShared& tube, std::string_view s);

// Convenience: read bytes from R1 (host side) until empty, return as string.
std::string drain_r1_to_host(TubeShared& tube);

// For Tier 3 in-process tests: step both machines in alternation.
// Runs host_steps cycles on the host, then parasite_steps on the parasite,
// repeated for the given number of rounds.
template <typename HostMachine, typename ParasiteMachine>
void interleave_step(HostMachine& host, ParasiteMachine& parasite,
                     uint64_t host_steps, uint64_t parasite_steps,
                     uint64_t rounds);
```

### Test build targets

```cmake
# Tier 1: Tube ULA logic (no shared memory, no machine)
add_executable(test_tube_ula test_tube_ula.cpp)
target_link_libraries(test_tube_ula PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_ula)

# Tier 2: Shared memory transport
add_executable(test_tube_shared_memory test_tube_shared_memory.cpp)
target_link_libraries(test_tube_shared_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_shared_memory)

# Tier 3 (in-process): Full integration without subprocess
add_executable(test_tube_integration test_tube_integration.cpp)
target_link_libraries(test_tube_integration PRIVATE
    beebium_core beebium_service Catch2::Catch2WithMain)
catch_discover_tests(test_tube_integration)

# Tier 3 (cross-process): Real subprocess + gRPC
add_executable(test_tube_cross_process test_tube_cross_process.cpp)
target_link_libraries(test_tube_cross_process PRIVATE
    beebium_core beebium_service Catch2::Catch2WithMain)
catch_discover_tests(test_tube_cross_process)
```

### Which tests run when

| Tier | Speed    | Requires ROMs | Requires shared memory | Requires subprocess | When to run          |
|------|----------|---------------|------------------------|---------------------|----------------------|
| 1    | Fast     | No            | No                     | No                  | Always (CI + local)  |
| 2    | Fast     | No            | Yes (in-process)       | No                  | Always (CI + local)  |
| 3 in | Moderate | Yes           | No (heap)              | No                  | When ROMs available  |
| 3 cp | Slow     | Yes           | Yes (OS-level)         | Yes                 | Integration CI only  |

Tier 1 and Tier 2 tests have no external dependencies and should run in every CI build.
Tier 3 in-process tests require ROM images (they skip gracefully if unavailable, following
the pattern established by `test_boot.cpp` and `test_mode7_helpers.hpp`). Tier 3
cross-process tests additionally require that the `beebium-tube-65C02-3MHz` executable has been
built, and are gated behind a CMake option or CTest label.

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
