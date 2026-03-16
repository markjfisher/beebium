# Chuckie Egg 2023 Tube Hang Analysis

## Status

**Blocked on differential testing.** The game hangs at the "Initialising"
stage on Beebium, B2, and MAME. It works on B-Em, jsbeeb, and real hardware.
Nine hypotheses tested and disproven. Six Tube implementation improvements
committed. The root cause requires instruction-level comparison with jsbeeb.

A first-class TypeScript client for Beebium is being developed to enable
differential testing via the jsbeeb oracle framework.

## The Game

Chuckie Egg 2023 (40th Anniversary Edition) by Sam Sherring
(samskivington.com) is a BBC Micro game that uses a 6502 second processor
via the Tube. The boot sequence is:

1. `*EXEC !BOOT`
2. MODE 7 loading screen: "Chuckie Egg 2023", "40th Anniversary Edition",
   "For the BBC Micro with 65x co-pro"
3. MODE 7 initialising screen: "Initialising"
4. "Initialising" changes to "Loading" with a Mode 7 sixel progress bar
5. Game starts

The hang occurs between steps 3 and 4 -- the game never progresses from
"Initialising" to "Loading".

## Emulator Compatibility

| Emulator | Architecture | Result |
|----------|-------------|--------|
| Real hardware | Single-bus synchronous | Works |
| B-Em | Batched single-process | Works (after R3 fix) |
| jsbeeb | Single-process JS event loop | Works |
| Beebium | Multi-process (shared memory) | **Hangs** |
| B2 | Multi-process | **Hangs** |
| MAME | Single-process | **Hangs** |

The pattern is not simply "multi-process fails" since MAME (single-process)
also hangs. The common factor among working emulators is: their Tube ULA
implementation is tightly coupled with the host/parasite CPU execution in a
way that gives deterministic register state at every instruction boundary.

## The Game's Custom Tube Protocol

The game bypasses the MOS Tube protocol entirely, using direct register I/O.
The host-side transfer code lives at `$6A00-$6BFF` (loaded from the `$.Boot`
file on the disc) and the parasite-side decompressor at `$0800-$0A35` (loaded
via R3 NMI transfer).

### Host Transfer Sequence

```
Phase 1: R3 NMI bulk transfer (decompressor code to parasite)
  $6A00  Send 4 R4 setup bytes (type-6 transfer address $0800)
  $6A14  Send 566 bytes via R3 ($6057-$628C -> parasite $0800)
         (delivered by Tube Client ROM NMI handler on parasite)

Phase 2: Clear flags and start compressed data transfer
  $6A38  Clear M and V flags (disable PNMI, 1-byte R3 mode)
  $6A45  Clear all remaining flags (Q, I, J)
  $6A48  Write $80 to R2 (command byte)
  $6A4D  Read R4 P->H (first ack from parasite -- succeeds)

Phase 3: R1 polled transfer (compressed data)
  $6A58  Send 702 bytes via R1 ($628D-$654A, blocking writes)

Phase 4: Wait for decompression completion
  $6A73  Read R4 P->H (second ack) -- DEADLOCKS HERE
  $6A76  Restore Q and J flags, return
```

### Parasite Decompressor

```
$0800  Clear zero page
$0810  Write $FC to R4 P->H (first ack to host)
$0813  JSR $0819 (LZ decompression loop)
         - Reads R1 bit-by-bit via $09C8 routine
         - Writes output from $FC00, wrapping through 64K
         - Has relocated I/O helpers at $FC00-$FC1A
$0816  JMP ($FFFC) (start decompressed game via reset vector)
```

The second R4 ack comes from the decompressed game code, after `JMP ($FFFC)`.
The decompression must complete and the game must start before the host
receives this ack.

### The Bit-Serial Reader ($09C8)

```
$09C8  LSR $31          ; Shift out bit from current byte
       BNE return       ; If bits remain, return (bit in carry)
       PHA
       BIT $2C          ; Check source: Tube or memory?
       BMI from_memory
       BIT $FEF8        ; Poll R1 status (parasite side)
       BPL poll_loop    ; Loop until H->P data available
       LDA $FEF9        ; Read R1 byte
       BRA store
from_memory:
       LDA ($2D)        ; Read from memory pointer
store: SEC
       ROR A            ; Shift in sentinel bit at MSB
       STA $31          ; Store as bit source
       PLA
       RTS
```

## Observed Deadlock State

At the hang point:

| Side | PC | Action |
|------|-----|--------|
| Host | `$6BCB` | Polling R4 status (BIT $FEE6; BPL) waiting for P->H data |
| Parasite | `$09D1` | Polling R1 status (BIT $FEF8; BPL) waiting for H->P data |

Tube register state: all control flags cleared ($00), R1 empty, R4 P->H
empty, R3 P->H has 1 pending dummy byte.

The decompressor has consumed all 702 R1 bytes and produced exactly 65536
bytes of output (the entire 64K address space). The bit buffer ($31) is
empty ($00). The decompressor is requesting byte 703 which will never arrive.

## Transfer Counter Evidence

Per-register byte counters added to `TubeShared`:

| Register | Direction | Writes | Reads | Delta |
|----------|-----------|--------|-------|-------|
| R1 | H->P | 702 | 702 | 0 |
| R2 | H->P | 24 | 24 | 0 |
| R3 | H->P | 16950 | 16950 | 0 |
| R4 | H->P | 471 | 471 | 0 |
| R4 | P->H | 1 | 1 | 0 |

**All deltas zero.** Every byte written was read. No data loss on any
register in either direction. The R3 data (566 bytes) was verified
matching between host source ($6057) and parasite destination ($0800).

## Disproven Hypotheses

1. **R1 byte loss** -- Transfer counters balanced (702 = 702)
2. **R3 NMI data loss** -- All R3 bytes verified matching (566/566)
3. **RAM initialisation** -- Both B-Em and Beebium zero parasite RAM
4. **Stale R1 byte** -- R1 empty before custom transfer starts
5. **I/O back-reference** -- Transfer counters prove no spurious R1 reads
6. **PNMI threshold bug** -- Beebium's threshold calculation is correct
7. **Data bus latch** -- Empty read values now match real hardware
8. **TOCTOU latch race** -- Atomic exchange eliminates the race
9. **R3 1-byte buffer overflow** -- Bus stretching now uses V-flag threshold

## Tube Implementation Improvements Made

All six improvements are correct regardless of CE2023 and have been committed:

### 1. PNMI condition (matches jsbeeb and Tube Application Note)

PNMI fires when M=1 AND (R3 H->P count >= threshold OR R3 P->H count <
threshold). This matches jsbeeb `tube.js` line 109 and the Tube Application
Note. B-Em only checks H->P data, but jsbeeb's broader condition is the one
documented in the Application Note.

### 2. Atomic exchange for latch ready flags

Replaced load-check-store with `ready.exchange(0)` to eliminate TOCTOU race
where the writer could set ready between our load and store. Matches B-Em
commit e04aab0's unconditional flag clearing.

### 3. Data bus latch for empty register reads

Host and parasite data bus latches in `TubeShared` track the last value
written to any register from each side. Empty R1 FIFO and R3 FIFO reads
return the appropriate latch value instead of 0. Matches B-Em commit
97f0ad6 and hoglet's Ferranti Tube ULA tests.

### 4. R3 H->P bus stretching uses V-flag threshold

Host R3 write spin-waits on `count >= threshold` instead of fixed `count >=
2`. In 1-byte mode (V=0, threshold=1), the host blocks after writing 1
byte, preventing accumulation of 2 bytes. Matches B-Em's space-available
flag behaviour.

### 5. Per-register transfer counters

`TubeCounters` struct in `TubeShared` with 16 atomic uint64 counters:
writes and reads for each register direction (R1-R4, H->P and P->H).
Single-writer principle: host increments write counters for H->P and read
counters for P->H; parasite does the opposite. Both sides can read all
counters. Exposed via `TubeTransferCounters` in the debugger proto.

### 6. Hardware-validated R3 register tests

14 new Catch2 test cases based on hoglet's Ferranti Tube ULA tests:
R3 1-byte and 2-byte modes, write-read patterns, status flag behaviour,
data available/space available, PNMI conditions.

## Key Insight: Emulator Architecture Matters

The working emulators (B-Em, jsbeeb, real hardware) all have a property
that the failing emulators (Beebium, B2, MAME) lack: **deterministic Tube
register state at every parasite instruction boundary**.

In B-Em and jsbeeb, the host and parasite share a single execution context.
When the parasite reads a Tube register, the host's state is frozen at a
known point. The register values are a deterministic function of the
execution history.

In Beebium and B2, the host and parasite run concurrently. When the parasite
reads a register, the host may have just written to it, or may not have yet.
The register values depend on OS scheduling and relative execution speed.

For the standard MOS Tube protocol (which uses polling loops), this doesn't
matter -- the protocol is designed for asynchronous operation. But CE2023's
custom decompressor reads 702 bytes via polled R1 transfers. If any
register read (including status polls) returns a different value due to
timing, the bit-serial decompression could diverge.

The decompressor's I/O write protection at `$09F0-$09FC` skips writes to
`$FEE0-$FEFF` but does NOT skip reads (back-references via `LDA ($33),Y`).
When the output destination wraps through the Tube register area, the back-
reference reads return live register status instead of decompressed data.
These values are timing-dependent in the concurrent model.

However, transfer counters showed no R1 reads from the I/O area, so the
back-reference may not actually pass through `$FEF8-$FEFF` in practice.
The exact divergence point remains unknown.

## jsbeeb R3 Behavioural Differences

jsbeeb's R3 write in 1-byte mode overwrites position [0] and resets count
to 1 (latch semantics), rather than enqueuing into a 2-slot FIFO. This
differs from both real hardware (which has a 2-byte FIFO per hoglet's
tests) and Beebium. However, jsbeeb works with CE2023 while Beebium
doesn't, suggesting the R3 write mode is not the root cause.

B-Em's R3 write in 1-byte mode does use a FIFO with accumulation, but
clears the space-available flag after the first write (threshold=1),
effectively preventing a second write until the parasite reads. Beebium
now matches this behaviour after the bus stretching threshold fix.

## Next Steps: Differential Testing with jsbeeb Oracle

The Beebium oracle at `oracle/` already supports host-side differential
testing between jsbeeb and Beebium. Extending it for Tube:

### jsbeeb Tube Architecture

- `Tube` ULA class in `src/tube.js` (349 lines)
- `Tube6502` parasite CPU class in `src/6502.js` extending `Base6502`
- `Tube65C02` model in `src/models.js` with ROM `tube/6502Tube.rom`
- Host and parasite share one JS event loop (single-process)
- CE2023 works correctly

### Required Oracle Extensions

1. **TypeScript Beebium client** -- being developed as a first-class client
   alongside the Python client. Will support Tube gRPC services.

2. **JsbeebOracle Tube support** -- configure model with `tube` property,
   extract parasite CPU state and Tube ULA register state.

3. **Targeted CE2023 test** -- boot both emulators, synchronise at
   parasite decompressor entry ($0819), compare R1 read values byte-
   by-byte. The first divergence reveals the root cause.

## Differential Testing Progress

### jsbeeb Tube6502 debug hook (committed to jsbeeb)

Added `_debugInstruction` callback to `Tube6502.execute()` in jsbeeb's
`src/6502.js`. Fires at each parasite instruction with current PC.
Return true to stop. Enables precise parasite breakpoints from oracle.

### Oracle results so far

- jsbeeb boots CE2023 to "Loading" screen in ~5s emulated
- jsbeeb parasite breakpoint at $0810 works: A=$FC, SP=$F8
- jsbeeb parasite breakpoint at $0819 works (decompression entry)
- Beebium boots to game loading screen via TypeScript client
- `runForEmulatedSeconds()` and `runUntilOrTimeout()` work on both
  TypeScript and Python clients
- R1 byte comparison test structurally complete; Beebium boot/connect
  sequence still needs optimisation (takes ~5min vs jsbeeb's ~3s)

### Timing reference (from manual jsbeeb testing)

- Boot to "Loading" screen: ~5 seconds emulated
- "Loading" to "A game of skill" screen: ~34 seconds emulated
- "Initialising" text appears very briefly (too fast to see when working)

## External References

- B-Em issue #216 (R3 2-byte transfer bug):
  https://github.com/stardot/b-em/issues/216

- Stardot R3 FIFO hardware analysis by hoglet:
  https://stardot.org.uk/forums/viewtopic.php?p=409877#p409877
  Key findings: V flag only affects status, FIFO depth always 2, writes
  when full are ignored, empty reads return bus latch value, data_available
  and space_available are logical inverses.

- Stardot hoglet's all-register test cases:
  https://stardot.org.uk/forums/viewtopic.php?p=412565

- Stardot Chuckie Egg 2023 compatibility thread:
  https://www.stardot.org.uk/forums/viewtopic.php?t=28163
  MAME also hangs at "Initialising". Sam Skivington documents emulator
  compatibility matrix.

- B-Em commit e04aab0: "implement behaviour from Hoglet's all register
  test cases" -- unconditional flag clearing, fixed empty read values.

- B-Em commit 97f0ad6: "further correct to empty read behaviour" --
  replaced fixed values with data bus latch (hpl/phl).

## Files

| File | Purpose |
|------|---------|
| `clients/python/tests/test_tube_chuckie_egg.py` | Test suite (3 pass, 1 fail) |
| `tests/assets/discs/chuckieEgg2023.ssd` | Disc image |
| `clients/python/tests/tube_test_helpers.py` | Shared Tube test utilities |
| `tests/test_tube_parasite_port.cpp` | C++ Tube register tests (60 cases) |
| `tests/test_tube_host_port.cpp` | C++ Tube register tests (32 cases) |
| `src/core/include/beebium/tube/TubeShared.hpp` | Shared memory layout + counters |
| `src/core/src/TubeHostPort.cpp` | Host-side Tube register I/O |
| `src/core/src/TubeParasitePort.cpp` | Parasite-side Tube register I/O |
| `docs/discussion/chuckie-egg-2023-tube-hang.md` | This document |
