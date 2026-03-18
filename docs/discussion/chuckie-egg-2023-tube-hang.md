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

## Differential Testing Infrastructure

### jsbeeb Oracle

- `Tube` ULA class in `src/tube.js` (349 lines)
- `Tube6502` parasite CPU class in `src/6502.js` extending `Base6502`
- `Tube65C02` model in `src/models.js` with ROM `tube/6502Tube.rom`
- Host and parasite share one JS event loop (single-process)
- CE2023 works correctly
- `_debugInstruction` callback added to `Tube6502.execute()` for
  parasite breakpoints from the oracle framework

### WatchExecutionState streaming RPC

`WatchExecutionState` server-streaming RPC on `DebuggerControl`. Server
pushes events on state transitions (breakpoint hit, manual stop, run
resumed). `waitForStop()` waits for a running-to-stopped transition,
skipping any initial "already stopped" state. Eliminates gRPC polling.

### Coupled stepping for Tube

`run_until_or_timeout(predicate, seconds, coupled=True)` connects to
the Tube counterpart (discovered via `connectParasite()`) and ensures
both host and parasite are running during the polling loop. This avoids
pacing asymmetry where one side advances faster than the other.

Key constraints discovered during implementation:

- **stepCycles deadlocks with Tube**: Synchronous `stepCycles` on the
  host blocks on Tube bus stretching (spin-wait on shared memory) until
  the parasite reads. If the parasite is paced independently, the host
  step call blocks for as long as the parasite takes to catch up.
  Concurrent `stepCycles` on both sides via ThreadPoolExecutor also
  deadlocks: when the host step blocks on a Tube write, the parasite
  step may have already returned, and neither side can make progress.

- **Bus stretch cancel**: Added `bus_stretch_cancel` flag to `TubeShared`.
  Machine::pause() sets it via callback; Machine::resume() and
  `prepare_for_step()` clear it. The Tube bus stretching spin loops in
  `TubeHostPort` check this flag, allowing debugger pause to break out
  of an otherwise infinite spin.

- **Server startup race**: The host server briefly appears "running"
  (cycle 0) before `handle_wait_mode` pauses it at cycle 7. The
  `runUntilOrTimeout` startup retry loop handles this by retrying
  `run()` until cycles advance past 10.

### Previous "R1 byte 0 divergence" was a test artefact

The earlier finding ("jsbeeb=$C5, Beebium=$02") was caused by bugs in
the differential test: the parasite was stopped for breakpoint setup but
never resumed, and the host was never properly started due to the
WaitMode::Api startup race. The "divergence" compared jsbeeb's
decompressor state against a Beebium parasite still in the Tube Client
ROM idle loop.

## Confirmed Findings

With the boot sequence fixed (using `runUntilOrTimeout` with coupled
mode, matching the Python test fixture pattern):

- **Game loads successfully**: Boots to "Initialising", decompressor
  consumes all 702 R1 bytes, parasite reaches `$09D6`. Transfer
  counters: R1 H2P 702/702, R3 H2P 16950/16950, R4 P2H 1/1.

- **Hang is after decompression**: The decompressor writes 64K of
  output, then tries to read byte 703 which never arrives. The
  bit-serial reader at `$09C8` has exhausted its byte buffer (`$31`
  = `$00`) and is polling R1 for the next byte.

- **The decompressed output must differ from jsbeeb's**: The
  decompressor consumes the same 702 bytes on both emulators, but
  the LZ bit-stream interpretation produces different output. On
  jsbeeb the decompression terminates after 64K; on Beebium it
  does not. This means some decompressed byte(s) differ, causing
  different LZ control flow that consumes more bits.

## Differential Memory Comparison Results

Comparing parasite memory when jsbeeb exits the decompressor (`$0816`)
vs Beebium hangs in R1 poll (`$09D6`). Only 1225/65280 bytes differ.

Differing pages: `$0000, $0100, $0B00, $0C00, $0D00, $FC00, $FD00, $FF00`

Per-page first divergence:

| Address | jsbeeb | Beebium | Notes |
|---------|--------|---------|-------|
| `$0030` | `$00`  | `$FC`   | Dest pointer low byte (decompressor state) |
| `$01EE` | `$C1`  | `$41`   | Stack area (bit 7 differs) |
| `$0BD2` | `$0A`  | `$00`   | First decompressed data divergence |
| `$0C00` | `$0A`  | `$00`   | Continuation |
| `$0D00` | `$E8`  | `$4A`   | Continuation |
| `$FC00` | `$2C`  | `$20`   | First output byte (decompressor writes from $FC00) |
| `$FD00` | `$85`  | `$BD`   | Second output page |
| `$FF00` | `$20`  | `$FF`   | Near end of first output pass |

The decompressor writes from `$FC00`, wrapping through the full 64K
address space. In output order, `$FC00` is byte 0 of the output. The
divergence at `$FC00` (`$2C` vs `$20`) means the **very first output
byte** differs.

### Disproven hypothesis: parasite RAM initialisation

The LZ decompressor uses back-references to previously written output.
If the parasite RAM at `$FC00+` had different initial values before
decompression starts, back-references into uninitialised memory would
produce different output even with identical R1 input data.

**Disproven** (March 2026): full 64K parasite memory comparison at
`$0810` (first R4 ack, before any decompressor output) shows only 1
byte different out of 65280 -- a pushed P register on the stack at
`$01F8` with a V flag difference. The `$FC00-$FFFF` output area is
byte-for-byte identical. See "Instruction-Level Divergence Finding"
below.

## Instruction-Level Divergence Finding (March 2026)

Using the TypeScript differential testing framework (jsbeeb oracle vs Beebium),
both parasites were synchronised at $0810 (first R4 ack) and stepped
instruction-by-instruction.

### Pre-decompression memory: identical

Full 64K parasite memory comparison at $0810 shows **1 byte different**
out of 65280 (excluding I/O page):

- `$01F8` (stack): jsbeeb=`$63`, Beebium=`$61` (V flag in pushed P register)
- `$FC00-$FFFF` (decompressor output area): **byte-for-byte identical**

The RAM initialisation hypothesis is disproven. Pre-decompression state
matches.

### First divergence: R1 status poll (instruction #19)

Both parasites are at `$09D4` (`BPL $09D1`, the R1 status poll loop).
The `BIT $FEF8` instruction that preceded it returns different status:

| | P register | N flag | V flag | R1 status |
|---|---|---|---|---|
| jsbeeb | `$42` | 0 (no data) | 1 | bit 7 clear |
| Beebium | `$C0` | 1 (data ready) | 1 | bit 7 set |

In Beebium's concurrent model, the host has already written the first
R1 byte before the parasite polls. In jsbeeb's single-threaded model,
the host hasn't run at this point, so R1 is empty.

This timing difference causes the parasites to execute different numbers
of poll-loop iterations, but the DATA byte read via `LDA $FEF9` should
be identical. The divergence in instruction count makes simple
instruction-by-instruction comparison impractical -- a higher-level
synchronisation point is needed (e.g., comparing A register at `$09D9`
after each R1 data byte is read).

### R1 data byte comparison: data corruption confirmed

Capturing A register at `$09D9` (after `LDA $FEF9`) for all 702 R1 bytes:

| | Byte 0 | Byte 1 | Byte 2 | Byte 3 | Byte 4 | ... |
|---|---|---|---|---|---|---|
| jsbeeb | `$C5` | `$11` | `$03` | `$90` | `$2C` | (702 distinct) |
| Beebium | `$C5` | `$C5` | `$C5` | `$C5` | `$C5` | (same byte repeated) |

Beebium reads the **same first byte** (`$C5`) 702+ times. The R1 FIFO
data is not being dequeued -- each `LDA $FEF9` returns the same value.

### Root cause: bus_stretch_cancel silently drops host writes

The host writes R1 bytes via `TubeHostPort::host_write(1, value)`. This
spin-waits until the latch is empty (`ready == 0`). When `bus_stretch_cancel`
is set (by debugger pause/resume), the spin loop exits via `return`
**without writing the byte**, silently dropping it.

```cpp
while (shared_->r1_h2p.ready.load(...) != 0) {
    if (shared_->bus_stretch_cancel.load(...)) return;  // BYTE DROPPED
}
shared_->r1_h2p.value.store(value, ...);  // never reached
```

The same bug affects R3 and R4 host writes: all three use the identical
`bus_stretch_cancel → return` pattern with no indication to the caller
that the write failed.

In the CE2023 scenario, the debugger's CoupledSystem uses
`stop_counterpart` breakpoints that set `bus_stretch_cancel`. Even
without the debugger, the server's pacing system or pause/resume
cycles can trigger `bus_stretch_cancel` during a Tube transfer,
corrupting the data stream.

Three C++ tests added to `test_tube_parasite_port.cpp` expose this bug
on R1, R3, and R4.

### R1 data comparison methodology note

The R1 data comparison test using breakpoints at `$09D9` was flawed:
only the first R1 byte was captured correctly. After that, the
decompressor's output wraps through 64K and overwrites its own code at
`$09D6`, changing the `LDA $FEF9` instruction to something else.
Subsequent breakpoint hits at `$09D9` are executing corrupted code, not
real R1 reads.

This confirms that Beebium's decompressor produces **different output
from byte 0**, which eventually corrupts the decompressor code and causes
the hang. The `bus_stretch_cancel` fix (pending write deferral) was
correct but is not the root cause of CE2023 -- the timing difference in
R1 status (`BIT $FEF8` returning bit 7 set on Beebium vs clear on
jsbeeb) causes the first bit-serial byte to be consumed differently.

### Decompressor output comparison

Capturing A register and output pointer ($2F/$30) at the output write
instruction `STA ($2F)` at `$0A00`:

| Byte # | jsbeeb A | jsbeeb addr | Beebium A | Beebium addr |
|--------|----------|-------------|-----------|--------------|
| 0 | `$2C` | `$FC00` | `$2C` | `$FC00` |
| 1 | `$F8` | `$FC01` | `$2C` | `$FC00` |
| 2 | `$FE` | `$FC02` | `$2C` | `$FC00` |
| 3 | `$10` | `$FC03` | `$2C` | `$FC00` |
| ... | (advances) | (advances) | `$2C` | `$FC00` |

The first output byte matches (`$2C` at `$FC00`). After that, Beebium
writes `$2C` to `$FC00` repeatedly -- the output pointer never advances.
The decompressor main loop is stuck in a cycle that produces the same
output byte at the same address.

The output routine at `$0A00-$0A0C` increments `$2F` (the pointer low
byte) after each `STA ($2F)`. If the pointer isn't advancing, either
the increment isn't being reached, or something resets `$2F` before
each call.

### Next investigation step

Set a breakpoint at `$0A03` (`INC $2F`) on Beebium's parasite and
verify it is reached after each `STA ($2F)`. If it IS reached, something
else resets the pointer. If NOT, the code path from `$0A00` to `$0A03`
is being diverted -- likely by the I/O write protection at `$09F0-$09FC`
which skips both the write AND the pointer increment when the output
address is in `$FEE0-$FEFF`.

The I/O protection check:
```
$09EF  PHA
$09F0  LDA $30        ; high byte of output pointer
$09F2  CMP #$FE       ; is it $FE?
$09F4  BNE $09FF      ; no, normal write
$09F6  LDA $2F        ; low byte
$09F8  CMP #$E0       ; >= $E0?
$09FA  BCC $09FF      ; no, normal write
$09FC  PLA            ; SKIP write and pointer increment
$09FD  BRA $0A02      ; branch to $0A02 (past STA but BEFORE INC)
$09FF  PLA
$0A00  STA ($2F)      ; write output byte
$0A02  PHA            ; <-- $09FD branches here
$0A03  INC $2F        ; increment pointer
```

Wait -- `$09FD: BRA $0A02` branches to `$0A02`, which is `PHA`. Then
`$0A03: INC $2F` increments the pointer. So even the I/O-skip path
still increments the pointer! The I/O protection only skips the `STA`,
not the `INC`. This means the pointer should always advance.

Unless the BRA target is wrong. `$09FD: BRA $0A02` -- the offset byte
is `$03` (BRA is `$80 $03`). From `$09FF` (PC after fetching the 2-byte
instruction at `$09FD`), adding `$03` gives `$0A02`. So it branches to
`$0A02`. If `$0A02` is `PHA` ($48), then `$0A03` is `INC $2F` ($E6 $2F).
The pointer increments on both paths. So the pointer MUST be advancing.

If the pointer advances but the output shows the same address, the main
decompressor loop must be resetting $2F/$30 between output calls. This
would happen in the back-reference copy loop at `$09E9-$090C` which
uses `$33/$34` as the source pointer and `$2F` as the Y offset.

### Instruction stepping produces correct output

When the parasite is stepped instruction-by-instruction (with the host
running freely), the decompressor produces output bytes that match
jsbeeb EXACTLY for all 20 bytes tested. The stepping ensures the host
is always far ahead, with all R1 data already in the latch.

### Free-running transfer counter analysis

When running freely (both at full speed), the hang occurs with:

- R1 H->P: **0 writes, 0 reads** -- the host never sent ANY R1 data
- R3 H->P: 16384 writes/reads -- NMI transfer completed
- R4 H->P: 457 writes/reads -- setup bytes transferred
- R4 P->H: 0 writes -- **the parasite never sent the first R4 ack**

This means the parasite never reached `$0810` (`JSR $0A2D`, write
`$FC` to R4 P->H). The decompressor code was loaded via R3 NMI
transfer (16384 bytes), but the parasite never started executing it.
The host is stuck waiting for the R4 ack at `$6A4D`.

The root cause is NOT in the decompressor or R1 transfer -- it's
earlier, in the transition from the R3 NMI transfer to the custom
protocol. The parasite receives the decompressor code but never
begins executing it, even though the Tube Client ROM's R2 command
dispatch should jump to `$0800` after receiving the `$80` command byte.

### Corrected deadlock state (March 2026, session 2)

With longer emulation time (30s) and full diagnostic capture:

| Side | PC | Action |
|------|-----|--------|
| Host | `$6BCE` | `BPL $6BCB`: polling R4 status, waiting for second P->H ack |
| Parasite | `$09D1` | `BIT $FEF8`: polling R1 status, waiting for byte #703 |

Transfer counters: R1 H->P w=702 r=702, R4 P->H w=1 r=1. All 702
bytes were transferred and consumed. The decompressor code at `$0800`
is correctly loaded (matches expected bytes). The decompressor DID run
but consumed all 702 bytes without completing the decompression --
the output diverged from jsbeeb's, causing the LZ algorithm to consume
more bits than intended.

**The earlier finding of R1=0 and R4 P->H=0 was due to capturing state
too early** (before the game's *EXEC !BOOT command had been fully
processed by the host). With 30s of emulation, the full transfer
completes and the deadlock is at the same point as the original
investigation.

**Instruction stepping anomaly**: when the parasite is stepped one
instruction at a time (with host running freely ahead), the
decompressor output matches jsbeeb exactly. This proves the
decompressor code itself is correct AND the R1 data is correct. The
divergence only occurs during free-running concurrent execution, where
the relative timing of host writes and parasite reads affects something
in the decompressor's behaviour.

### The remaining mystery

The decompressor reads 702 bytes via R1, the same bytes as jsbeeb.
It uses a bit-serial reader that extracts bits one at a time via LSR/ROR.
Stepping produces correct output. Free-running produces different
output. The only difference is timing.

Possible causes:
1. **Tube register read side-effects during polling**: `BIT $FEF8`
   (R1 status poll) calls `parasite_read(0)` which updates PNMI.
   In the concurrent model, the R1 status value differs between polls
   (sometimes data ready, sometimes not), causing different numbers of
   poll iterations. If any side-effect of reading R1 status depends on
   the DATA register state (not just the status), the extra/fewer polls
   could affect the decompressor.

2. **PNMI firing during decompression**: even though M is cleared,
   `update_pnmi()` is called on every register access. If there's a
   window where M is still set (between host sending the R2 command
   and clearing M), a PNMI could fire and corrupt decompressor state.

3. **R1 read timing vs host write timing**: the `BIT $FEF8` instruction
   reads R1 status. If the host writes an R1 byte between the status
   read and the data read (`LDA $FEF9`), the parasite gets the "wrong"
   byte (one that was written AFTER the status was checked). On real
   hardware this can't happen because the host is bus-stretched during
   the write. In Beebium's model, the host runs freely between
   parasite cycles.

### R1 data confirmed correct during free-running

Instrumented `TubeParasitePort::parasite_read` case 1 to log the first
10 R1 data bytes. The values match jsbeeb exactly: `C5 11 03 90 2C 47
B4 77 7A 76`. The R1 latch mechanism delivers the correct data.

### No spurious interrupts

- 566 NMIs logged (exactly matching the R3 NMI transfer). All fire at
  `$F975`/`$F978` (Tube Client ROM). None in the decompressor range.
- 68 IRQs logged, all at `$F975`/`$F978`. None in the decompressor.
- PNMI is correctly disabled when M flag is cleared.

### Bit-serial reader ($31) traces match!

Capturing every write to `$31` (the bit-serial buffer) on both emulators
during free-running: the first 700+ entries match exactly (case-
insensitive). The bit-serial reader processes the same bits in the same
order on both emulators.

### Output pointer ($2F) traces match!

Capturing every write to `$2F` (output pointer low byte) on both
emulators: the first 200 entries match exactly. The decompressor
advances the output pointer identically on both emulators (at least
for the first 200 bytes).

jsbeeb produces exactly 1024 `$2F` changes (256 increments per page x 4
pages, tracking non-zero changes) and stops at `$0816` (decompressor
exit). The Beebium trace was limited to 200 entries.

### The paradox

The R1 data is correct. The bit-serial reader produces identical `$31`
values. The output pointer advances identically. Yet the decompressor
hangs on Beebium after consuming all 702 R1 bytes, while jsbeeb
completes successfully.

The traces captured coarse-grained state (value changes, not every
instruction). The divergence must be in a finer-grained aspect of the
decompressor's execution -- possibly the value of A when `STA ($2F)`
writes output, or the back-reference source pointer `$33/$34`.

### Post-decompression memory comparison (with fixes)

With the `bus_stretch_cancel` pending write fix and the ARM memory
ordering fix applied, the post-decompression comparison shows:

- **494 bytes** differ out of 65280 (previously 1225 -- the fixes
  improved things significantly)
- Divergent pages: `$0000`, `$0100`, `$FD00`, `$FF00`
- First OUTPUT data divergence: `$FD14` (output byte #276)
  - jsbeeb: `$17`
  - Beebium: `$58`

The decompressor produces **276 correct output bytes** (addresses
`$FC00-$FD13`), then diverges at byte 277. This means roughly half the
R1 data is consumed correctly before the bit-serial interpretation
diverges.

### Output divergence is NON-DETERMINISTIC

Further testing revealed that the divergence point varies between runs.
In one run, `$FD14` contained the correct value (`$17`), while in
another it contained `$58`. All 5 repeated runs failed to reach
"Loading", but the specific divergence point changed each time.

The decompressor output is deterministic when stepped (single
instruction at a time, host running ahead), but non-deterministic
when free-running. This confirms the root cause is a **concurrency
race** in the Tube register access between host and parasite.

### Root cause analysis

The decompressor reads R1 data via a tight poll-read sequence:
```
$09D1  BIT $FEF8    ; poll R1 status (parasite_read offset 0)
$09D4  BPL $09D1    ; loop until data available
$09D6  LDA $FEF9    ; read R1 data (parasite_read offset 1)
```

Each `BIT $FEF8` and `LDA $FEF9` calls `parasite_read()` which also
calls `update_pnmi()`. `update_pnmi()` reads R3 state from shared
memory. The number of R1 status polls before data is available varies
with concurrent timing. Each extra poll calls `update_pnmi()` which
reads R3 shared state.

Since `update_pnmi()` modifies `pnmi_edge_` and `prev_pnmi_` based on
R3 shared state, and R3 state can change between polls (the host may
write/read R3 concurrently for the NMI transfer), the PNMI edge
detection state might differ between runs. If PNMI fires at different
points, the NMI handler corrupts the decompressor's state.

However, the NMI count (566) matches the expected R3 transfer size,
and all NMIs fire in the Tube Client ROM, not the decompressor. So PNMI
is not the direct cause.

The remaining hypothesis: **the R1 data read returns incorrect data
due to a read-after-write race in shared memory**. Even with the ARM
memory ordering fix (acquire before value), there might be a window
where the host's value store is not yet visible to the parasite's value
load. The `ready.exchange(0, acq_rel)` should synchronise, but the
exchange ALSO clears ready, allowing the host to write the next byte.
If the host writes and the parasite re-reads the latch (e.g., via a
second `parasite_read(1)` call) before the host's value store is
visible, the parasite sees the new byte instead of the expected one.

### R1 data is byte-for-byte identical (702/702 match)

All 702 R1 data bytes match jsbeeb exactly. All reads have `was_ready=1`
(no stale reads). The R1 data path is correct.

### ROOT CAUSE FOUND: back-reference reads hit live Tube registers

The LZ decompressor copies previously-written output via back-references
using `LDA ($33),Y` at `$09EB`. When the back-reference pointer sweeps
through `$FEF8-$FEFF`, `ParasiteMemoryMap::read()` routes these reads
to Tube register `parasite_read()` instead of returning the RAM value.

Instrumented reads from Tube registers (offset >= 2, excluding the R1
status/data polls) show **200+ reads from `$FEFA` (R2 status)** during
decompression. These are back-reference reads hitting the Tube I/O area.

On real hardware, reads from `$FEF8-$FEFF` DO go to the Tube ULA (CAS
is suppressed, the CPU reads from the ULA). jsbeeb behaves the same way
(`readmem` routes these addresses to `parasiteRead`). So both Beebium
and jsbeeb read Tube registers for back-references.

The difference: in jsbeeb's single-threaded model, the Tube register
values are **deterministic** at each parasite instruction (the host
doesn't advance between parasite instructions). In Beebium's concurrent
model, the host runs freely between parasite cycles, changing the Tube
register values. When the decompressor reads `$FEFA` (R2 status) as a
back-reference, it gets a timing-dependent value that varies with
host-parasite scheduling.

This is the fundamental cause of the non-deterministic divergence.

### Back-reference Tube reads: disproven

jsbeeb readmem intercept during the full decompression shows:
- `$FEF8` (R1 status): 3133 reads (polling loop)
- `$FEF9` (R1 data): 702 reads (all legitimate)
- `$FEFE` (R4 status): 1 read (incidental)

**Zero back-reference reads from Tube registers.** The decompressor
never reads from `$FEFA`, `$FEFB`, `$FEFC`, `$FEFD`, or `$FEFF`.
The LZ encoding avoids back-references into the Tube register area.
The 200+ reads from `$FEFA` seen earlier on Beebium were from the
Tube Client ROM's boot-time R2 polling, not from the decompressor.

### R1 data confirmed correct (full 702 bytes)

All 702 R1 reads have `was_ready=1` (no stale reads). The byte
values match jsbeeb exactly. The R1 data path is fully correct.

### Free-running without debugger: still fails

Running both host and parasite freely for 15 seconds (no CoupledSystem
chunks, no breakpoints, no `bus_stretch_cancel` triggers) produces
the same "Initialising" hang. This eliminates the `bus_stretch_cancel`
mechanism as the cause -- the bug is a pure concurrent execution issue.

### Remaining hypotheses

The R1 data is correct. No back-references hit Tube registers. No
spurious interrupts. The `$31` bit buffer and `$2F` output pointer
traces match for 200+ entries. Yet the decompressor diverges at a
non-deterministic point.

### R1 latch protocol proven correct under thread stress

Cross-thread stress tests (702 bytes x 100 iterations = 70,200
transfers) using `TubeHostPort::host_write` and
`TubeParasitePort::parasite_read` with two threads running flat out
all pass. The R1 shared-memory latch protocol is correct.

The CE2023 divergence is NOT caused by the R1 latch mechanism itself.

### BUG REPRODUCED: 6502 R1 polled read produces corrupted data

A narrow C++ test (`test_tube_r1_6502.cpp`) plants the same 6502
poll-read loop used by the CE2023 decompressor:

```
$0400: LDX #$00
$0402: BIT $FEF8        ; poll R1 status
$0405: BPL $0402        ; loop until data available
$0407: LDA $FEF9        ; read R1 data
$040A: STA $0500,X      ; store in results buffer
$040D: INX
$040E: CPX #NUM_BYTES
$0410: BNE $0402
$0412: BRA *            ; halt
```

A host thread writes bytes through `TubeHostPort` concurrently. The
6502 runs via `ParasiteCpu::tick()` on the main thread. Single-byte
and 200-byte tests pass, but the 50-iteration stress test **fails**:

```
Iteration 5, byte 59: expected $72 got $71
```

The off-by-one (`$72` → `$71`, previous byte instead of current)
proves the bug is a TOCTOU race within the 6502's multi-cycle `LDA
$FEF9` instruction. The same R1 protocol that works correctly when
calling `parasite_read()` directly fails when the 6502 mediates the
access through `ParasiteCpu::tick()`.

The `LDA $FEF9` (absolute addressing) takes 4 CPU cycles. The actual
memory read of address `$FEF9` (triggering `parasite_read(1)` which
clears the ready flag and reads the value) happens on one specific
cycle. On the other cycles, `pirq()` and `pnmi_level()` read from
shared memory. There is a window where the host writes a new byte
and the parasite reads the old one, or vice versa.

This is the root cause of the CE2023 hang and explains all observed
behaviour:
- Stepping works (host far ahead, R1 always has stable data)
- Free-running fails (host writes concurrently with 6502 execution)
- The divergence is non-deterministic (depends on thread scheduling)
- R1 data appears correct when logged (logging captures the value
  after `parasite_read()` returns, but the 6502 may have seen a
  different value on the data bus during the instruction)

### 6502 R1 test bug: WRONG BRANCH OFFSET (not a Tube race)

The test_tube_r1_6502 stress test that appeared to prove a Tube TOCTOU
race had a **bug in the test code**: the BNE offset was `$F1` (-15)
instead of `$F0` (-16), causing the 6502 to branch into the MIDDLE of
the `BIT $FEF8` instruction. This executed garbled opcodes that
produced non-deterministic off-by-one errors mimicking a latch race.

With the correct offset, ALL tests pass consistently (50 iterations of
200 bytes = 10,000 transfers, 3 consecutive runs, zero failures).

**The R1 latch protocol is correct.** There is no TOCTOU race.
The CE2023 hang has a different root cause that remains to be found.

### Root cause found (March 2026, session 3)

**A single LZ back-reference read sweeps through the Tube I/O area and
consumes one R1 byte from the latch, shifting the remaining bit stream
by one byte.**

#### The mechanism

The LZ decompressor copies previously-written output via `LDA ($33),Y`
at `$09EB`. The output wraps through the full 64K address space starting
at `$FC00`. When the back-reference source pointer plus Y offset wraps
through the Tube register area `$FEF8-$FEFF`, the read goes to
`ParasiteMemoryMap::read()` which routes it to
`TubeParasitePort::parasite_read()`. This has a side effect: reading
from R1 data (offset 1) unconditionally clears the `ready` flag,
consuming whatever byte is in the R1 latch.

The specific occurrence:
- Instruction: `LDA ($33),Y` at `$09EB`
- Pointer `($33)` = `$FEFE`, Y = `$FB`
- Effective address: `$FEFE + $FB = $FFF9` (correct, after page fix-up)
- Page-cross intermediate address: `$FEF9` (high byte not yet corrected)
- The intermediate read from `$FEF9` goes through `parasite_read(1)`,
  clearing the R1 ready flag

On Beebium, the R1 latch contains byte `$FB` (the host has written ahead
eagerly). The intermediate read consumes this byte. The bit reader's next
legitimate `LDA $FEF9` at `$09D6` gets the FOLLOWING byte (`$D8`) instead.
This shifts the R1 data stream by one byte from read 697 onwards. The
decompressor processes 701 bytes instead of 702, runs out of data, and
hangs at the R1 poll loop (`$09D1`) waiting for byte 703.

On jsbeeb, the same `LDA ($33),Y` with Y=`$FB` executes at the same point
in the decompression. But jsbeeb returns the RAM value (`$FE`) rather than
the Tube register value. The R1 latch is not consumed, and the
decompression completes normally with 702 bytes.

#### Evidence

Instruction trace comparison (500,000 jsbeeb instructions vs 10M Beebium):

- Both traces execute `LDA ($33),Y` at `$09EB` exactly 381 times
- Both hit Y=`$FB` exactly twice, at corresponding points
- First Y=`$FB` occurrence: both read A=`$FF` (identical)
- Second Y=`$FB` occurrence: jsbeeb reads A=`$FE`, Beebium reads A=`$FB`
  - jsbeeb returns the RAM value at `$FEF9` (or `$FFF9`)
  - Beebium returns the R1 latch data (`$FB`)
- Bit streams are identical for the first 5577 of 5615 bits (99.3%)
- R1 byte values match for reads 0-696, then shift by exactly -1
- Cumulative bit extractions (`LSR $31`) match at every R1 byte boundary
- jsbeeb exits the decompressor at `$0844` (RTS); Beebium never reaches it

The standalone test (no host, no concurrency, snapshot + R1 data file)
reproduces the exact same divergence in under 1 second, confirming this
is not a timing or concurrency issue.

#### The page-cross intermediate read

The 65C02's `LDA (zp),Y` addressing mode with page crossing reads from
an intermediate address before correcting the high byte:

1. Read pointer low byte from ZP
2. Read pointer high byte from ZP+1
3. Add Y to pointer low byte. If carry (page cross), read from
   uncorrected address (wrong high byte)
4. Read from correct address (high byte fixed)

With pointer=`$FEFE` and Y=`$FB`: low byte `$FE + $FB = $F9` with carry.
Step 3 reads from `$FEF9` (uncorrected). Step 4 reads from `$FFF9`
(correct). On real hardware, step 3's read goes to the Tube ULA at
`$FEF9`. Whether this has side effects depends on the hardware timing
and whether the Tube ULA latches the access.

#### Open question: real hardware behaviour

On real hardware, the page-cross intermediate read from `$FEF9` does
hit the Tube ULA (CAS is suppressed for the `$FExx` range, the ULA
responds). Whether this clears the R1 ready flag depends on the ULA's
internal timing. The game works on real hardware, which implies either:

1. The R1 latch is empty at this point on real hardware (the host
   hasn't written the next byte yet because of natural 2 MHz timing)
2. The Tube ULA doesn't fully latch the intermediate read (it may
   require a full clock cycle commitment, and the page-cross read is
   only half a cycle)
3. The 65C02 suppresses the intermediate read in some way not
   documented in standard references

jsbeeb appears to handle this by returning RAM instead of the Tube
register for this read, which avoids the side effect entirely. Whether
this matches real hardware or is an incidental jsbeeb simplification
is not yet known.

#### Implications for the fix

The fix must ensure that the R1 latch byte is not consumed by a
page-cross intermediate read. Options:

1. **Fix the memory map**: distinguish intermediate reads from data
   reads (requires the 6502 library to signal which type of read is
   occurring -- the `read` field already has `M6502ReadType_Data` vs
   `M6502ReadType_Uninteresting`)
2. **Use ReadType filtering**: only call `parasite_read()` (with side
   effects) when `cpu_.read == M6502ReadType_Data`, and call
   `parasite_peek()` (no side effects) for other read types
3. **Match jsbeeb's behaviour**: return RAM for Tube register reads
   that occur during non-data cycles

Option 2 is the most principled: the 6502 library already classifies
reads as `Data` vs `Uninteresting`. Side-effecting I/O should only
respond to `Data` reads.

#### Corrected eliminated hypotheses

Previous investigation incorrectly eliminated:
- "Back-reference reads hitting Tube registers (jsbeeb confirms zero)"
  -- jsbeeb DOES hit Tube registers via back-references (2 occurrences
  with Y=$FB), but returns RAM values instead of Tube register values,
  masking the side effect.

Previous conclusion "non-deterministic divergence" was incorrect for
the current codebase. After the NMI nesting fix, the divergence is
fully deterministic: same byte lost, same output, same hang point on
every run. The earlier non-determinism was likely from the unfixed NMI
nesting bug corrupting the R3 transfer.

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
| `tests/test_tube_ce2023_standalone.cpp` | Standalone decompressor test (root cause confirmation) |
| `tests/test_tube_ce2023_trace.cpp` | Full-boot trace test with auto-boot |
| `oracle/tests/ce2023-trace-dump.test.ts` | jsbeeb instruction trace dump |
| `src/core/include/beebium/tube/TubeShared.hpp` | Shared memory layout + counters |
| `src/core/include/beebium/tube/InstructionTrace.hpp` | Instruction trace ring buffer |
| `src/core/include/beebium/tube/RegisterTrace.hpp` | Tube register access trace |
| `src/core/src/TubeHostPort.cpp` | Host-side Tube register I/O |
| `src/core/src/TubeParasitePort.cpp` | Parasite-side Tube register I/O |
| `src/core/src/ParasiteCpu.cpp` | Parasite CPU with trace + watchpoints |
| `src/service/proto/debugger.proto` | WatchExecutionState RPC definition |
| `src/service/include/beebium/service/DebuggerService.hpp` | Streaming implementation |
| `oracle/tests/tube-chuckie-egg.test.ts` | TypeScript differential test |
| `docs/discussion/chuckie-egg-2023-tube-hang.md` | This document |
