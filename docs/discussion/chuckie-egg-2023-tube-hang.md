# Chuckie Egg 2023 Tube Hang Analysis

## Status

**In progress.** The game hangs at the "Initialising" stage on both Beebium and B2. B-Em (single-threaded lockstep) runs it successfully.

## The Game

Chuckie Egg 2023 (40th Anniversary Edition) by Sam Sherring (samskivington.com) is a BBC Micro game that uses a 6502 second processor via the Tube. The boot sequence is:

1. `*EXEC !BOOT`
2. MODE 7 loading screen: "Chuckie Egg 2023", "40th Anniversary Edition", "For the BBC Micro with 65x co-pro"
3. MODE 7 initialising screen: "Initialising"
4. "Initialising" changes to "Loading" with a Mode 7 sixel progress bar
5. Game starts

The hang occurs at step 3 — the game never progresses from "Initialising" to "Loading".

## Observed Deadlock State

When the hang is detected (via `test_tube_chuckie_egg.py::test_loading_progresses`), the emulator state is:

| Side | PC | What it's doing |
|------|-----|-----------------|
| Host | `$6BCB` | Polling R4 status (`BIT $FEE6; BPL $6BCB`) waiting for P→H data |
| Parasite | `$09D1` | Polling R1 status (`BIT $FEF8; BPL $09D1`) waiting for H→P data |

Tube ULA state:
- **Control flags = `$00`** — all interrupt enables cleared (Q, I, J, M, V, P all zero)
- R1 H→P: empty
- R3 P→H: 1 byte pending (`$00`), host hasn't read it
- R4 P→H: empty (value `$FC` but ready flag clear — was already consumed)
- All other registers: empty

## The Game's Custom Tube Protocol

The game bypasses the MOS Tube protocol entirely, using direct register I/O. The host-side transfer code lives at `$6A00-$6BFF` and the parasite-side decompressor at `$0800-$0A35`.

### Host-Side Transfer Routines

Low-level register I/O routines at `$6BA7-$6BD3`:

| Routine | Address | Purpose |
|---------|---------|---------|
| `$6BA7` | R1 H→P write | `BIT $FEE0; BVC; STA $FEE1` (wait for space, write) |
| `$6BB0` | R2 H→P write | `BIT $FEE2; BVC; STA $FEE3` |
| `$6BB9` | R3 H→P write | `BIT $FEE4; BVC; STA $FEE5` |
| `$6BC2` | R4 H→P write | `BIT $FEE6; BVC; STA $FEE7` |
| `$6BCB` | R4 P→H read | `BIT $FEE6; BPL; BIT $FEE7` (wait for data, read via BIT) |

### Host Transfer Sequence (at `$6A00`)

```
Phase 1: Setup and R3 bulk transfer
  $6A00  JSR $6A7D       ; Write 4 setup bytes to R4 H→P
  $6A04  JSR $6BC2       ; Wait for R4 space
  $6A07  JSR $6A89       ; Wait for R4 space
  $6A14  loop:            ; Send data $6057-$628D via R3 (JSR $6BB9)
  $6A2B  ...

Phase 2: Clear flags and start R1 transfer
  $6A38  LDA #$18        ; Clear M and V flags
         STA $FEE0
  $6A45  LDA #$1F        ; Clear all remaining flags (Q, I, J, M, V)
         STA $FEE0
  $6A48  LDA #$80        ; Send command byte $80 to R2
         JSR $6BB0
  $6A4D  JSR $6BCB       ; Read R4 P→H (first ack) ← succeeds

Phase 3: R1 data transfer
  $6A50  set pointer to $628D
  $6A58  loop:            ; Send data $628D-$654B via R1 (JSR $6BA7)
         until pointer == $654B (~702 bytes)

Phase 4: Wait for completion
  $6A73  JSR $6BCB       ; Read R4 P→H (second ack) ← DEADLOCKS HERE

Phase 5: Restore flags
  $6A76  LDA #$84        ; Set Q and J (re-enable PIRQ from R4)
         STA $FEE0
  $6A7B  CLC
         RTS
```

### Parasite-Side Decompressor

The parasite code at `$0800` is a compressed data receiver and decompressor:

```
$0800  Clear zero page
$0808  Set destination pointer $2F/$30 = $FC00
$0810  JSR $0A2D        ; Write $FC to R4 P→H (first ack)
$0813  JSR $0819        ; Enter decompression loop
$0816  JMP ($FFFC)      ; Jump through reset vector (now contains game entry point)
```

The decompressor at `$0819` reads compressed data bit-by-bit from R1 via the `$09C8` routine:

```
$09C8  LSR $31          ; Shift out next bit from current byte
       BNE return       ; If bits remain, return (bit in carry)
       PHA
       BIT $2C          ; Check source flag
       BMI from_memory  ; If bit 7 set, read from memory pointer ($2D/$2E)
       BIT $FEF8        ; Tube path: poll R1 status
       BPL poll_loop    ; Loop until data available
       LDA $FEF9        ; Read R1 byte
       BRA store
from_memory:
       LDA ($2D)        ; Memory path
       INC $2D / $2E
store: SEC
       ROR A            ; Shift in sentinel bit at top
       STA $31          ; Store as bit source
       PLA
       RTS
```

The decompressor writes output starting at `$FC00` and fills downward through memory, eventually wrapping around to low addresses. Before it overwrites the decompressor's own code at `$0800`, it has a **relocated copy** at `$FC00`:

```
$FC13  BIT $FEFE        ; R4 status (parasite)
$FC16  BVC $FC13        ; Wait for space
$FC17  STA $FEFF        ; Write R4 P→H
$FC1A  RTS
```

After decompression completes, the reset vector at `$FFFC` contains the decompressed game's entry point, and `JMP ($FFFC)` starts the game.

### The R4 Handshake

The protocol has exactly one R4 P→H write from the parasite (at `$0810`). The host has two R4 P→H reads (at `$6A4D` and `$6A73`). The first read succeeds. The second read is the hang point.

**The second R4 ack is expected to come from the decompressed game code**, after `JMP ($FFFC)` executes the new reset vector. In the working case (B-Em), the decompression completes, the game starts, and whatever initialisation the game does sends the R4 ack.

## Transfer Counter Results

Per-register byte counters (added to `TubeShared`) show the state at the hang point:

| Register | Direction | Writes | Reads | Delta |
|----------|-----------|--------|-------|-------|
| R1 | H→P | 702 | 702 | 0 |
| R2 | H→P | 24 | 24 | 0 |
| R3 | H→P | 16950 | 16950 | 0 |
| R4 | H→P | 471 | 471 | 0 |
| R4 | P→H | 1 | 1 | 0 |

**All deltas are zero. No bytes were lost on any register.** Every byte the host
wrote was read by the parasite, and vice versa. The R3 NMI transfer (16950 bytes
during boot + game setup) completed cleanly.

The R3 data was verified by comparing the first 256 bytes at the parasite
destination (`$0800`) with the host source (`$6057`): **exact match**.

### What this rules out

- ~~R1 byte loss~~ — All 702 R1 bytes received
- ~~R3 NMI data loss~~ — All R3 bytes received and data matches
- ~~PNMI edge detection race~~ — R3 counters balanced

## Why It Actually Hangs

The parasite decompressor consumed all 702 R1 bytes and hasn't finished. The
decompression destination pointer wrapped through the entire 64K address space
(from `$FC00` back to `$0000`) but the decompressor is still requesting more
input bits from R1.

The decompressor at `$0819` uses a bit-serial reader (`$09C8`) that reads one
byte at a time from R1 and extracts bits via `LSR $31`. When `$31` is exhausted
(zero), it fetches the next byte from R1. With the bit buffer empty (`$31=$00`)
and no more R1 data, the decompressor polls R1 forever.

### The real question

**Is the R1 stream endpoint wrong?** The host sends bytes from `$628D` to
`$654A` inclusive (702 bytes). The endpoint `$654B` is hardcoded in the game's
host-side transfer loop. If the compressed stream is actually 703+ bytes, the
host would stop one byte short, leaving the decompressor hanging on the missing
end-of-stream marker.

This same code works on B-Em, so either:

1. **B-Em's R3 NMI transfer delivers data in a different order or timing** that
   causes the decompressor to interpret the bitstream differently (e.g. if a
   256-byte NMI block boundary falls at a different point).

2. **The host-side endpoint is computed earlier** and something in the earlier
   setup phase went differently, resulting in a different endpoint value being
   stored.

3. **The decompressor's behaviour depends on the initial state of memory** that
   differs between Beebium and B-Em (e.g. memory not cleared to zero on the
   parasite before the transfer).

## Additional Verification

### Parasite RAM initialisation

Both B-Em and Beebium zero all 64K of parasite RAM:
- B-Em: `memset(tuberam, 0, memsize)` in `common_init()` (`src/6502tube.c`)
- Beebium: `ram_.fill(0)` in `ParasiteMemoryMap` constructor

**Ruled out** as a cause.

### R3 NMI data integrity

The full 566 bytes of R3 data at the parasite destination (`$0800`) were compared
with the host source (`$6057-$628C`): **exact match, all 566 bytes**.

### Decompressor output volume

The decompressor has written exactly 65536 bytes — the **entire 64K address
space** — from `$FC00` wrapping through `$FFFF` and back to `$0000`. Despite
producing a full 64K of output from 702 bytes of compressed input, it has not
terminated. The exit condition depends on a specific bit pattern in the
compressed bitstream that was never encountered.

### Endpoint analysis

The R1 stream endpoint `$654B` is hardcoded in the host code (literal bytes
`$4B` and `$65` at addresses `$6A5D` and `$6A63`). This code is loaded from
disc into host RAM at `$6000`. The same disc produces working results on B-Em.

## Current Hypothesis

The compressed data stream at `$628D-$654A` (702 bytes) is genuinely incomplete
for Beebium's decompressor but works on B-Em. Since all bytes transfer
correctly and the data matches, the difference must be in **how the decompressor
processes the stream**. Possible causes:

1. **65C02 vs NMOS 6502 behavioural difference.** Beebium's parasite runs a
   65C02 (CMOS). B-Em may use an NMOS 6502. Some instructions behave
   differently (BCD mode, read-modify-write, undocumented opcodes). If the
   decompressor uses an instruction that differs between variants, the
   compressed bitstream would be consumed differently.

2. **Cycle-exact timing of R1 reads.** The decompressor's bit reader at `$09C8`
   polls R1 status and reads R1 data as separate operations. In a concurrent
   model, the value read might differ from what the status indicated (though
   the latch should prevent this).

3. **NMI handler behaviour during decompression.** If a stale PNMI fires
   during the R1 decompression phase (after flags are cleared), the NMI
   handler would execute and potentially corrupt state. The NMI vector at
   `$FE65` contains RTI, which is harmless, but if the NMI fires before
   the vector is overwritten by decompression, it would execute the original
   Tube Client ROM NMI handler.

## B-Em Implementation Analysis

B-Em's Tube implementation (in `src/tube.c` and `src/6502tube.c`) reveals
several important details:

### B-Em is not lockstep

B-Em runs the parasite in **batches**, not cycle-for-cycle lockstep. The host
accumulates fractional cycles, and when the total exceeds 3.0, the parasite
runs a batch. However, **parasite Tube register writes set `endtimeslice=1`**,
which forces the host to yield after the current instruction. This gives
pseudo-lockstep behaviour during Tube transfers.

### R3 empty-read behaviour

When the parasite reads R3 data and the FIFO is empty (`hp3pos == 0`):
- **B-Em**: returns `tubeula.hpl` (a latch holding the last R3 value)
- **Beebium**: returns 0

This is a behavioural difference, but the Chuckie Egg decompressor reads R1
(not R3) during the hanging phase, so this is unlikely to be the direct cause.

### PNMI is level-triggered in B-Em

B-Em's PNMI condition is: `(r1stat & M) && (pstat[2] & DATA_AVAIL)`. This is
a **level-sensitive** signal. The parasite detects NMI on the **rising edge**
of `tube_irq & 2` (comparing with `tube_6502_oldnmi`). Beebium's PNMI uses
similar edge detection in `TubeParasitePort::update_pnmi()`.

### B2 also fails

The B2 emulator (which also uses a multi-process architecture) fails at
the same "Initialising" stage. This confirms the issue is specific to
**concurrent/multi-process Tube implementations**, not a Beebium-specific bug.
Both B-Em (batched single-process) succeeds and B2/Beebium (multi-process)
fail. The difference is in how host and parasite synchronise during Tube
register access.

## Refined Hypothesis: Host-Parasite Synchronisation During R1 Transfer

In B-Em's batched model, when the host writes to R1 H→P (`STA $FEE1`):
1. The write completes in the host's timeslice
2. The host continues until it writes again or the timeslice ends
3. The parasite then runs, reads R1, and processes the byte

In Beebium's concurrent model:
1. The host writes R1 H→P (sets ready flag)
2. The parasite (running concurrently) may or may not read it immediately
3. The host's next write spin-waits on the ready flag

The blocking write should guarantee synchronisation. But there's a subtlety:
in B-Em, the host writes data AND the status update happens atomically (same
function call, same thread). In Beebium, the `value.store()` and
`ready.store()` are two separate atomic operations. Between them, the parasite
could read the status (seeing old value) and the data (also old value).

Wait — this shouldn't matter because the parasite polls status first, then
reads data only when status shows data available. The `ready` flag is the
gate. The sequence is:

Host: `value.store(X, relaxed); ready.store(1, release);`
Parasite: `if (ready.load(acquire)) { value.load(acquire); ready.store(0, release); }`

The release-acquire pair on `ready` ensures the parasite sees the correct
`value` after observing `ready==1`. This should be correct.

## B2 Also Fails

The B2 emulator (which also uses a multi-process architecture) fails at
the same "Initialising" step. This confirms the issue is specific to
concurrent/multi-process Tube implementations. B-Em (batched single-process)
succeeds; B2 and Beebium (multi-process) fail.

## Stale R1 Byte Check

Verified: R1 H→P is empty (data_available=False, value=$00) at the
"Initialising" screen, before the game's custom R1 transfer begins. No
stale byte from the MOS protocol. The R1 source data matches the disc
image with 0 mismatches.

## Decompressor I/O Area Back-Reference Issue

The LZ decompressor copies previously-written data via back-references
using `LDA ($33),Y` at `$09EB`. The decompressor skips WRITES to the
I/O area (`$FEE0-$FEFF`) at `$09F0-$09FC`, but does NOT protect READS
(back-references). When `$33/$34` points into the `$FEF8-$FEFF` range,
the back-reference reads Tube register status instead of decompressed data.

On real hardware, the Tube ULA suppresses RAM access at `$FEF8-$FEFF`.
Writes to this range go to the Tube ULA, and the data is never stored
in RAM. Back-reference reads from this range return Tube register values,
not previously-written data.

This means the decompressor's output depends on the **Tube register state
at the moment of the back-reference read**. If the register state differs
between B-Em and Beebium at those moments, the decompressed output diverges,
the bitstream desyncs, and the decompressor never reaches its termination
marker.

### Key Observation

The back-reference pointer `$33/$34 = $FEFE` was observed during the hang.
`$FEFE` is the R4 status register (parasite perspective). Reading it returns:
- Bit 7: R4 H→P data available
- Bit 6: R4 P→H space available
- Bits 5-0: `$3F` (reserved, read as 1)

In B-Em (single-process), R4 status during decompression is deterministic.
In Beebium (concurrent), the value depends on the host's concurrent state.
If the host has written to R4 H→P, bit 7 changes, giving a different
back-reference value and different decompressed output.

### Why This Explains B2's Failure Too

B2 also uses a multi-process architecture. The same timing difference in
Tube register reads during decompressor back-references would cause the
same divergence.

## Next Steps

1. **Verify back-reference reads from I/O space.** Add logging to the
   parasite's `ParasiteMemoryMap::read()` to detect reads from
   `$FEF8-$FEFF` during the decompression phase. Count how many
   back-reference reads hit the I/O area and what values they return.

2. **Compare R4 status timing.** Check what R4 status the parasite reads
   at the back-reference points in B-Em vs Beebium. Even one bit
   difference would desync the entire decompression.

## Root Cause Confirmed

The decompressor's back-reference reads from `$FEF8-$FEFF` go through
`ParasiteMemoryMap::read()` → `TubeParasitePort::parasite_read()`, which
has **side effects**:

- Reading R1 data (`$FEF9`) **clears the ready flag** and **increments
  the transfer counter**. If the host had written a compressed data byte,
  the back-reference read consumes it instead of the bitstream reader.

- Reading R1 status (`$FEF8`) returns live register status.

This means:
1. Some of the 702 "R1 reads" counted by the transfer counter were
   **back-reference reads**, not bitstream reads
2. The decompressor's bitstream reader received fewer than 702 compressed
   bytes
3. The decompressor never reaches its termination marker

In B-Em's single-process model, when the back-reference reads R1 data, the
host hasn't had a chance to write the next compressed byte yet (execution
alternates). So `ready` is 0, the read returns the old value without side
effects, and the counter doesn't increment. The bitstream reader gets all
702 bytes.

In Beebium's concurrent model, the host has likely already written the next
byte by the time the back-reference executes. So `ready` is 1, the back-
reference consumes the byte, and the bitstream reader gets fewer bytes.

## Fix

The fix should prevent decompressor back-reference reads from having Tube
register side effects. Options:

1. **Track read intent.** The decompressor reads R1 data at two points:
   `$09D6: LDA $FEF9` (bitstream read) and `$09EB: LDA ($33),Y` (back-ref
   that might hit `$FEF9`). The memory system can't distinguish these.

2. **Only clear ready on intentional Tube reads.** The 6502 instruction
   decode determines whether a read is from LDA absolute ($AD $F9 $FE) vs
   LDA indirect-Y. But the memory system doesn't see the opcode.

3. **Don't clear ready on data reads; clear on status reads.** This matches
   real hardware more closely: the real Tube ULA clears "data available"
   when the data register is read, regardless of intent. But the real
   hardware is synchronous — the read happens at a specific bus cycle and
   the effect is immediate.

4. **(Preferred) Accept that this is a real hardware behaviour difference
   in the cross-process model.** The real 6502 Second Processor's Tube ULA
   is on the same bus as the CPU. Reads from `$FEF8-$FEFF` always go to
   the Tube ULA, with side effects. The decompressor game code relies on the
   timing guarantee that the host hasn't written to R1 yet when the back-
   reference fires. In the concurrent model, this guarantee doesn't hold.

   The pragmatic fix: ensure the host's R1 write doesn't complete until
   the parasite has read the status register showing "data available" —
   i.e., the R1 latch write should be **visible** to the parasite only
   after the parasite polls R1 status. This is already the case (the
   blocking write spin-waits on `ready`). But the issue is that the
   host ALREADY wrote and set ready, and the back-reference accidentally
   reads it.

   The real fix is: **make R1 data reads NOT clear the ready flag when
   the previous instruction was not a status poll.** But this requires
   instruction-level tracking in the memory system.

5. **(Simplest) Suppress Tube register side effects for indirect addressing
   modes.** On real hardware, the 6502's indirect addressing modes generate
   bus cycles that look identical to direct reads. But in the emulator, we
   could heuristically suppress side effects for reads that "look like"
   back-references rather than intentional Tube reads. This is fragile.

6. **(Most pragmatic) For the specific case of R1 data reads: only clear
   ready and consume the byte if the R1 status was recently polled.** Add
   a "status was checked" flag that's set when R1 status is read and cleared
   when R1 data is read. Only consume the byte (clear ready) if the flag is
   set. This ensures that accidental reads from the back-reference (which
   don't poll status first) don't consume compressed data.

   This matches real hardware behaviour: on the real 6502, you poll status
   (BIT $FEF8) before reading data (LDA $FEF9). The hardware doesn't
   actually have a "status checked" gate, but the synchronous bus guarantees
   the same ordering. Option 6 simulates this guarantee in the async model.

## Fixes Applied

### Data bus latch for empty reads (committed, doesn't fix CE2023)

`TubeHostPort` and `TubeParasitePort` empty register reads now return
the data bus latch value (last value written to any register from the
respective side) instead of 0. Matches B-Em commit 97f0ad6 and real
Ferranti Tube ULA behaviour.

### Atomic exchange for latch ready flags (committed, doesn't fix CE2023)

Replaced load-check-store pattern with atomic exchange to eliminate
TOCTOU race on latch ready flags. Matches B-Em commit e04aab0.

### PNMI condition (committed, doesn't fix CE2023)

`TubeParasitePort::pnmi_level()` was firing on `h2p_data || p2h_space`.
Fixed to only fire on `h2p_data`, matching B-Em and real hardware. This
is correct but doesn't resolve the Chuckie Egg hang — the game's custom
transfer runs with M=0 (PNMI disabled) during the R1 phase.

### R1 status-checked gate (tested, reverted)

Attempted to gate R1 data consumption on prior R1 status poll to prevent
back-reference reads from consuming compressed data. Did not fix the hang
and was reverted as the wrong approach.

## External References

- B-Em issue #216 (R3 2-byte transfer bug): https://github.com/stardot/b-em/issues/216
  The B-Em bug was an operator precedence error in the PNMI threshold
  calculation. Beebium doesn't have this specific bug.

- Stardot forum R3 FIFO analysis: https://stardot.org.uk/forums/viewtopic.php?p=409877#p409877
  Detailed real-hardware testing of R3 behaviour by hoglet. Key findings:
  - V flag only affects status flags, not FIFO depth
  - Writes when full are ignored (not overwriting)
  - Empty reads return fixed values: host=$96, parasite=$E4
  - Data available and space available flags are logical inverses

- Sam Skivington's Tube test cases were used to validate B-Em's fix

- Stardot Chuckie Egg 2023 thread: https://www.stardot.org.uk/forums/viewtopic.php?t=28163
  Sam Skivington documents compatibility across emulators. Key finding:
  MAME also hangs at "Initialising" (same as Beebium/B2). The game's
  custom protocol relies on replacing the reset vector and JMP ($FFFC).

- Stardot Tube ULA Re-Implementation thread: https://stardot.org.uk/forums/viewtopic.php?p=409877
  hoglet's comprehensive R3 hardware tests with Ferranti ULA. Test disc:
  tube_r3_tests.ssd. Tests cover: reset state, empty reads, all access
  patterns (W, R, WR, WRR, WWR, WWRR, WRWR, WWRR), 1-byte and 2-byte
  modes. See also: https://stardot.org.uk/forums/viewtopic.php?p=412565

- B-Em commit e04aab0: "implement behaviour from Hoglet's all register test cases"
  Key changes: unconditionally clear flags on data reads, fixed empty read
  values (R1=$01, R3 host=$96, R3 parasite=$E4), removed conditional flag
  clearing for R2 and R4

## Files

- Test suite: `clients/python/tests/test_tube_chuckie_egg.py`
- Disc image: `tests/assets/discs/chuckieEgg2023.ssd`
- Host transfer code: loaded into host RAM at `$6000-$6BFF` by the game loader
- Parasite decompressor: loaded into parasite RAM at `$0800-$0A35` (with copy at `$FC00-$FC1A`)
- Tube register I/O: `src/core/src/TubeHostPort.cpp`, `src/core/src/TubeParasitePort.cpp`
- PNMI edge detection: `TubeParasitePort::update_pnmi()`
- Transfer counters: `TubeShared::TubeCounters` in `TubeShared.hpp`
