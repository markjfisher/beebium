# Tube Subsystem Design

## Overview

The Tube is Acorn's coprocessor interface: a custom ULA chip providing bidirectional FIFO-based
communication between two independent processor systems. The host (BBC Micro) handles I/O while
the parasite (second processor) runs application code. The two processors have completely
independent clock domains and communicate exclusively through the Tube's register interface.

Beebium's Tube implementation takes a fundamentally different approach from other emulators.
Rather than lockstep single-threaded execution, each second processor runs as an **independent
OS process** communicating with the host over a Unix domain socket. This mirrors the real
hardware's fully asynchronous nature: two independent computers linked only by a narrow FIFO
channel.

### Design principles

1. **Process isolation** -- the parasite is a separate executable with its own event loop,
   clock pacing, and gRPC debug server
2. **Asynchronous fidelity** -- no global clock, no lockstep; each CPU runs at its own rate,
   just as the real hardware did
3. **FIFO-authoritative model** -- the emulated Tube FIFO (not OS socket buffering) governs
   backpressure and interrupt generation
4. **Socket as transport only** -- the Unix domain socket carries framed messages; it does not
   define timing or buffering semantics

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
beebium-model-b (host)              beebium-tube-6502 (parasite)
┌────────────────────────┐          ┌────────────────────────┐
│  6502 CPU (2 MHz)      │          │  65C02 CPU (3 MHz)     │
│  System VIA, User VIA  │          │  64K RAM + Boot ROM    │
│  CRTC, Video ULA, etc  │          │                        │
│                        │          │                        │
│  ┌──────────────────┐  │          │  ┌──────────────────┐  │
│  │ TubeHost         │  │          │  │ TubeParasite     │  │
│  │ (Tube ULA model) │  │          │  │ (Tube ULA model) │  │
│  │                  │  │          │  │                  │  │
│  │  FIFO state      │  │          │  │  FIFO state      │  │
│  │  IRQ/NMI logic   │  │          │  │  IRQ/NMI logic   │  │
│  └────────┬─────────┘  │          │  └────────┬─────────┘  │
│           │            │          │           │            │
│  ┌────────┴─────────┐  │          │  ┌────────┴─────────┐  │
│  │ SPSC staging     │  │          │  │ SPSC staging     │  │
│  │ queue (outbound) │  │          │  │ queue (outbound) │  │
│  │ queue (inbound)  │  │          │  │ queue (inbound)  │  │
│  └────────┬─────────┘  │          │  └────────┬─────────┘  │
│           │            │          │           │            │
│  ┌────────┴─────────┐  │          │  ┌────────┴─────────┐  │
│  │ IO thread        │  │          │  │ IO thread        │  │
│  │ (socket R/W)     │  │          │  │ (socket R/W)     │  │
│  └────────┬─────────┘  │          │  └────────┬─────────┘  │
│           │            │          │           │            │
│  gRPC: Video, Audio,   │          │  gRPC: Debugger,       │
│  Keyboard, Disc, etc   │          │  System (parasite)     │
└───────────┼────────────┘          └───────────┼────────────┘
            │    Unix domain socket              │
            └────────────────────────────────────┘
```

Each side maintains its own **authoritative Tube FIFO state**. The IO thread handles the
socket transport. The CPU thread drives the Tube peripheral and never blocks on the socket.

### Layered separation

The architecture has three distinct layers:

**Layer 1: Tube ULA model** (the emulated hardware)
- Owns the FIFO buffers with correct depths (R1: 24 bytes P-to-H, 1 byte H-to-P;
  R2: 1 byte each; R3: 2 bytes each; R4: 1 byte each).
- Implements status flag logic (data available, not full).
- Computes interrupt outputs (HIRQ, PIRQ, PNMI) on every state change.
- The host side's ULA model manages the host-writable FIFOs (H-to-P direction).
  The parasite side's model manages the parasite-writable FIFOs (P-to-H direction).

**Layer 2: Staging queues** (transport buffer)
- SPSC lock-free ring buffers, one per direction.
- Absorb timing jitter between socket IO and CPU execution.
- Much larger than Tube FIFO depth (e.g. 4 KB) to absorb bursts.
- The CPU thread dequeues inbound bytes and feeds them into the Tube ULA model.
- The CPU thread enqueues outbound bytes when the Tube ULA model accepts a write.

**Layer 3: Socket transport** (IO thread)
- Reads/writes framed messages on the Unix domain socket.
- Never touches the Tube ULA model or interrupt state directly.
- Batches bytes into frames for efficiency (never one syscall per Tube byte).

### Key invariant

The Tube ULA model is the sole authority for:
- FIFO depth and fullness
- Interrupt assertion/deassertion
- Status flag values
- Backpressure (writer polls status before writing)

The socket and staging queues are **not** the FIFO. They are transport infrastructure.

### Asymmetric FIFO ownership

Each side of the Tube owns the FIFOs that *its* processor writes to:

| FIFO direction      | Depth    | Owner process   | Remote process  |
|---------------------|----------|-----------------|-----------------|
| R1 Host to Parasite | 1 byte   | Host            | Parasite reads  |
| R1 Parasite to Host | 24 bytes | Parasite        | Host reads      |
| R2 Host to Parasite | 1 byte   | Host            | Parasite reads  |
| R2 Parasite to Host | 1 byte   | Parasite        | Host reads      |
| R3 Host to Parasite | 2 bytes  | Host            | Parasite reads  |
| R3 Parasite to Host | 2 bytes  | Parasite        | Host reads      |
| R4 Host to Parasite | 1 byte   | Host            | Parasite reads  |
| R4 Parasite to Host | 1 byte   | Parasite        | Host reads      |

When the host CPU writes to R1DATA (H-to-P), the host's Tube ULA model:
1. Places the byte in the local H-to-P latch.
2. Updates local status flags.
3. Enqueues a `WRITE_BYTE(register=1, value=X)` message to the outbound staging queue.
4. Recomputes interrupt state.

When the parasite's IO thread receives this message, it is dequeued by the parasite CPU
thread and applied to the parasite's Tube ULA model, which updates its own status flags
and interrupt state.

The same logic applies in reverse for parasite-to-host writes.

### Status flag synchronisation

Both sides must agree on status flags for correct operation. The writer's local model
updates immediately; the reader's model updates when the transport message arrives.

This introduces a small window where the two sides disagree about status. In practice this
does not cause problems because:
- The writer only cares about "not full" (can it write another byte?).
- The reader only cares about "data available" (is there a byte to read?).
- These flags are owned by opposite sides.
- The real hardware had propagation delay across the Tube ULA too.

However, backpressure must be enforced on the **writer's** side. When the writer's local
model shows "full", the emulated CPU must poll/wait -- it must not write further bytes into
the staging queue. The staging queue may contain bytes in flight, but the FIFO depth limit
is enforced by the writer's local model, not by queue capacity.

### Interrupt proxying

When data arrives at the reader's side via the staging queue:
1. The CPU thread dequeues it into the local Tube ULA model.
2. The Tube ULA model updates status flags and recomputes interrupt state.
3. If HIRQ/PIRQ/PNMI changes, the interrupt line is asserted/deasserted on the local CPU.

This means interrupts are generated locally based on local FIFO state, not signalled
explicitly across the socket. This matches the hardware: the Tube ULA generates interrupts
based on its internal register state, not on external signals.

### When to drain the staging queue

The CPU thread must periodically drain inbound bytes from the staging queue into the Tube
ULA model. Options:

1. **On every Tube register read** -- when the CPU reads a status or data register, first
   drain any pending inbound bytes. This is the most natural point because it is when the
   CPU actually observes Tube state.

2. **On every instruction boundary** -- check once per instruction. More frequent, ensures
   interrupts are noticed promptly.

3. **On every N-cycle boundary** -- periodic check, tuneable.

Option 1 is recommended as the primary mechanism, with option 2 as a supplement to ensure
interrupt responsiveness. The real hardware had a propagation delay of 1-2 microseconds
across the Tube; draining at instruction boundaries is well within this tolerance.

## Transport Protocol

### Socket type

Unix domain socket (SOCK_STREAM), or TCP for remote parasite debugging.

The host creates the socket and listens. The parasite connects on startup.

### Frame format

All messages are length-prefixed binary frames, little-endian:

```
┌──────────┬──────────┬───────────────────┐
│ type: u8 │ len: u8  │ payload: [u8; len]│
└──────────┴──────────┴───────────────────┘
```

Maximum payload is 255 bytes. This is sufficient for all Tube operations (the largest
meaningful payload is a batch of R1 FIFO bytes, max 24).

### Message types

**Data messages** (hot path):

| Type | Name          | Payload                    | Direction |
|------|---------------|----------------------------|-----------|
| 0x01 | WRITE_BYTE    | register: u8, value: u8    | Both      |
| 0x02 | WRITE_BATCH   | register: u8, count: u8, data: [u8] | Both |
| 0x03 | STATUS_UPDATE | flags: u8                  | Both      |

WRITE_BYTE is the common case. WRITE_BATCH is an optimisation for R1 FIFO fills and R3
two-byte writes -- batching reduces syscalls and context switches.

STATUS_UPDATE carries the writer's view of status flags for the registers it owns, allowing
the reader to synchronise its "not full" view. This is sent when status transitions occur,
not on every byte.

**Lifecycle messages** (cold path):

| Type | Name          | Payload                           | Direction    |
|------|---------------|-----------------------------------|--------------|
| 0x10 | HELLO         | version: u8, parasite_type: u8    | P to H       |
| 0x11 | HELLO_ACK     | version: u8                       | H to P       |
| 0x12 | RESET         | reason: u8 (cold/warm/soft)       | H to P       |
| 0x13 | RESET_ACK     | --                                | P to H       |
| 0x14 | SHUTDOWN      | --                                | Both         |

**Debug/state messages** (cold path):

| Type | Name          | Payload                           | Direction    |
|------|---------------|-----------------------------------|--------------|
| 0x20 | FREEZE        | --                                | H to P       |
| 0x21 | FREEZE_ACK    | --                                | P to H       |
| 0x22 | STATE_DUMP    | size: u16, data: [u8]             | Both         |
| 0x23 | STATE_RESTORE | size: u16, data: [u8]             | H to P       |

### Batching strategy

The IO thread should batch outbound messages and flush periodically (e.g. every 1 ms or
when a batch reaches a size threshold). This amortises socket syscall overhead.

For inbound messages, the IO thread should read as many bytes as available (non-blocking
after initial blocking wait) and enqueue complete frames into the staging queue.

## Integration with Beebium

### Host-side integration

The host process needs a new `TubeSocket` peripheral following the pattern established by
`DiscControllerSocket` and `EconetSocket`:

```
TubeSocket
├── implements MemoryMappedDevice (read/write at &FEE0-&FEE7)
├── contains TubeHostModel (FIFO state, interrupt logic)
├── contains SPSC queues (inbound/outbound staging)
├── optional: IO thread handle
└── methods: enable(), disable(), reset()
```

**Memory map registration** in all three hardware policies:
```cpp
make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(tube_socket)
```

Note: the Tube uses 3 address bits (A0-A2) to select registers, so the 8-byte register
window is mirrored across the 32-byte Sheila range &FEE0-&FEFF.

**Bus timing**: the Tube is classified as a "fast" device in `BusStretching.hpp` (no 1MHz
stretching). This is already correct in the existing code.

**Interrupt routing**: HIRQ connects to the host CPU's IRQ line. A new device mask is needed
in the IRQ aggregator (alongside the existing VIA masks).

**NMI**: the host does not receive NMI from the Tube. The Tube's NMI outputs (PNMI) go to
the parasite only.

**Clock**: the TubeSocket needs to drain the inbound staging queue. This can be done:
- At 2 MHz (every cycle) via ClockBinding -- but this may be unnecessarily frequent.
- On-demand when the CPU accesses Tube registers -- preferred.
- Supplemented by a periodic poll (e.g. once per scanline) to ensure interrupts are timely.

**Tube presence detection**: OSBYTE &EA with X=0, Y=&FF returns X=&FF if a Tube is present.
The host-side Tube ROM code handles this; no special emulator support is needed beyond
making the Tube registers respond.

### Parasite-side integration

The parasite process is a new executable (e.g. `beebium-tube-6502`) containing:

```
beebium-tube-6502
├── 65C02 CPU emulator (3 MHz, CMOS instruction set)
├── 64 KB RAM
├── Boot ROM (2 KB at &F800-&FFFF, paged out after first Tube register access)
├── TubeParasiteModel (FIFO state, interrupt logic)
├── SPSC queues (inbound/outbound staging)
├── IO thread (socket to host)
├── Clock pacing loop (independent, targets 3 MHz)
├── gRPC server (DebuggerService, SystemService)
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

### Parasite Boot ROM

The 6502 second processor uses `6502Tube.rom` (also called the Tube OS or Hi-BASIC ROM in
some contexts). This is a small ROM (~2 KB) that:
1. Prints a startup banner via OSWRCH (R1).
2. Enters the main idle loop polling R1STAT and R2STAT.
3. Handles incoming OS call results.
4. Manages R3/R4 transfer protocols with NMI handlers.
5. Implements the parasite side of all Tube software protocols.

This ROM is loaded into the parasite process at startup. It is not part of Beebium's source
code; it is a binary image from the original Acorn hardware.

## Tube ULA Model

### Data structures

Each side (host and parasite) holds a `TubeUla` struct representing the complete register
state:

```
TubeUla
├── control_flags: {q, i, j, m, v, p, t, s}
│
├── Register 1
│   ├── h2p: single byte latch + status {data_available, not_full}
│   └── p2h: circular buffer [24] + head, tail, count + status
│
├── Register 2
│   ├── h2p: single byte latch + status
│   └── p2h: single byte latch + status
│
├── Register 3
│   ├── h2p: two byte buffer + count + status
│   └── p2h: two byte buffer + count + status
│
├── Register 4
│   ├── h2p: single byte latch + status
│   └── p2h: single byte latch + status
│
├── Interrupt outputs
│   ├── hirq: bool
│   ├── pirq: bool
│   └── pnmi: bool (with edge detection state)
│
└── last read latch (for reads when FIFO empty)
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

1. Host process starts normally with Tube support enabled (configuration option).
2. Host creates a Unix domain socket and begins listening.
3. Host launches the parasite process, passing the socket path as a command-line argument.
4. Parasite connects to the socket and sends HELLO (protocol version, processor type).
5. Host sends HELLO_ACK.
6. Host asserts PRST then deasserts it (or simply starts with Tube in reset state).
7. Parasite CPU begins executing from Boot ROM reset vector.
8. Boot ROM prints startup banner, enters idle loop.
9. Host filing system detects Tube, loads language ROM across R3/R4, issues Execute.
10. Normal operation begins.

### Shutdown sequence

1. Host sends SHUTDOWN message.
2. Parasite acknowledges, flushes gRPC streams, exits.
3. Host cleans up socket.

Or: parasite process crashes/exits unexpectedly. Host detects socket close, disables Tube,
continues operating without second processor (graceful degradation, mirroring the real
hardware where you could disconnect the Tube cable).

### Save state coordination

1. Host sends FREEZE to parasite.
2. Parasite stops CPU execution, drains staging queues, sends FREEZE_ACK.
3. Both sides serialise their Tube ULA state.
4. Parasite sends STATE_DUMP (CPU state + RAM + Tube state).
5. Host combines its own state with parasite state into unified save file.

Restore is the reverse: host sends STATE_RESTORE to parasite after reconnection.

## 6502 Second Processor Specifics

### Hardware specification

| Property          | Value                                    |
|-------------------|------------------------------------------|
| CPU               | 65C02 (CMOS, Rockwell or WDC)           |
| Clock speed       | 3 MHz                                   |
| RAM               | 64 KB                                   |
| Boot ROM          | 2 KB (4 KB in some variants)            |
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
&F000-&F7FF   RAM (or Boot ROM when paged in)
&F800-&FEFF   RAM (or Boot ROM when paged in)
  &FEF8-&FEFF Tube registers (always active)
&FF00-&FFFF   RAM (or Boot ROM; vectors here after boot)
```

### Boot ROM paging

The Boot ROM occupies the top of the address space on reset. It is deselected (paged out,
exposing RAM) when the parasite first reads R1STAT (address &FEF8, register offset 0).
The Boot ROM's reset handler copies itself into RAM before this happens, so execution
continues seamlessly.

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

## Future Second Processors

The process-separated architecture makes it straightforward to support additional second
processor types. Each would be a new executable:

| Processor   | Executable              | Clock   | RAM    |
|-------------|-------------------------|---------|--------|
| 65C02       | `beebium-tube-6502`     | 3 MHz   | 64 KB  |
| Z80         | `beebium-tube-z80`      | 6 MHz   | 64 KB  |
| NS32016     | `beebium-tube-32016`    | 6 MHz   | 1 MB   |
| ARM2        | `beebium-tube-arm`      | 8 MHz   | 4 MB   |
| 80186       | `beebium-tube-80186`    | 8 MHz   | 1 MB   |

The Tube transport protocol is processor-agnostic. Only the HELLO message identifies the
parasite type. All register semantics and software protocols are the same regardless of
processor architecture.

Different processors map the Tube registers at different addresses:
- 6502: &FEF8-&FEFF
- Z80: I/O ports or memory-mapped (varies by implementation)
- ARM: &01000000-&0100001C (shifted right by 2)
- 32016: memory-mapped in 32016 address space

But the register semantics are identical. The TubeParasiteModel is the same; only the
address decoding differs.

## Implementation Phases

### Phase 1: Host-side Tube peripheral

- Implement TubeSocket as a MemoryMappedDevice.
- Implement TubeUla struct with full register/FIFO/interrupt logic.
- Wire into all three hardware policies.
- Add HIRQ to host IRQ aggregator.
- Unit tests for all register operations, FIFO behaviour, interrupt conditions.
- No socket, no parasite process yet -- just the host-side hardware model.

### Phase 2: Transport layer

- Define the binary frame protocol.
- Implement framed socket reader/writer.
- Implement SPSC staging queues.
- Implement IO thread (both sides).
- Integration tests with a mock parasite that exercises the protocol.

### Phase 3: 6502 parasite process

- Create `beebium-tube-6502` executable.
- 65C02 CPU with 64 KB RAM and Boot ROM.
- TubeParasiteModel with Tube register handling.
- Independent clock pacing loop.
- Socket connection to host.
- Boot ROM loading and paging logic.
- End-to-end test: host launches parasite, parasite boots, runs BASIC.

### Phase 4: Parasite gRPC services

- DebuggerService for the parasite (breakpoints, memory inspection, step).
- SystemService for parasite (pause/resume/reset).
- Service discovery so the macOS frontend can attach to both host and parasite.

### Phase 5: Save state

- FREEZE/FREEZE_ACK protocol.
- Parasite state serialisation (CPU + RAM + Tube).
- Coordinated save/restore between host and parasite.

## Testing Strategy

### Unit tests (Phase 1)

- Register read/write for all 8 host-side and 8 parasite-side addresses.
- R1 FIFO: fill to 24 bytes, verify full flag, drain, verify empty flag.
- R3 one-byte and two-byte modes.
- Control flag set/clear via S bit.
- Interrupt conditions: HIRQ, PIRQ, PNMI with all enable combinations.
- NMI edge detection.
- Reset state including R3 dummy byte.

### Protocol tests (Phase 2)

- Frame encoding/decoding round-trip.
- WRITE_BYTE delivery across socket.
- WRITE_BATCH delivery.
- STATUS_UPDATE synchronisation.
- HELLO/HELLO_ACK handshake.
- RESET/RESET_ACK sequence.
- Connection drop handling.

### Integration tests (Phase 3)

- Parasite boots and prints startup banner.
- Host detects Tube via OSBYTE &EA.
- Language ROM loads across Tube.
- BASIC starts and executes simple program.
- OSWRCH output appears on host display.
- File loading from disc to parasite memory.
- Escape handling across Tube.

### Regression tests

- BeebEm, B-Em test cases for known Tube-sensitive software.
- Tube timing-sensitive demos (if any exist).

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
