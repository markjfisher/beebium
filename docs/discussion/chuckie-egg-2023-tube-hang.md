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

## Next Steps

1. **Check B-Em's 6502 variant.** Does B-Em use a CMOS or NMOS 6502 for the
   Tube parasite? If NMOS, check if any decompressor instructions differ.

2. **Instrument B-Em.** Add a counter to B-Em's R1 H→P read path to capture
   the exact byte count when the decompressor terminates.

3. **Check for spurious NMI.** Add logging to Beebium's PNMI path to detect
   if any NMI fires during the R1 decompression phase (when M=0).

## Files

- Test suite: `clients/python/tests/test_tube_chuckie_egg.py`
- Disc image: `tests/assets/discs/chuckieEgg2023.ssd`
- Host transfer code: loaded into host RAM at `$6000-$6BFF` by the game loader
- Parasite decompressor: loaded into parasite RAM at `$0800-$0A35` (with copy at `$FC00-$FC1A`)
- Tube register I/O: `src/core/src/TubeHostPort.cpp`, `src/core/src/TubeParasitePort.cpp`
- PNMI edge detection: `TubeParasitePort::update_pnmi()`
