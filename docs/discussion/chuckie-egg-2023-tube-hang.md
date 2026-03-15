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

## Why It Hangs

The parasite decompressor is still waiting for R1 data at `$09D1` when the host has already finished sending all 702 bytes (pointer reached `$654B`). The decompression destination pointer is at `$0000`, far from completion.

This means the **decompressor consumed all 702 R1 bytes but hasn't finished decompressing**. The decompressor expects more input data than the host sent.

### Hypotheses

1. **R1 byte loss in cross-process model.** The R1 H→P latch uses a `ready` flag with atomic acquire/release. If a byte is lost (host writes but parasite doesn't see the ready flag), the compressed bitstream would become misaligned. The decompressor would consume bytes at wrong boundaries and potentially loop forever or request more data than exists.

2. **R3 NMI data loss in Phase 1.** The first phase transfers data via R3 using NMI. If PNMI edge detection has a race condition in the cross-process model, the parasite might miss R3 data. This data is written into parasite memory (not via the decompressor), so missing bytes would corrupt the lookup tables the decompressor uses, causing it to decompress more slowly (requesting more R1 bytes per output byte).

3. **Timing-dependent protocol.** The Phase 1 R3 transfer happens with M and V flags still active (enabling PNMI). The game clears M and V at `$6A38`, then clears all flags at `$6A45`. If the parasite hasn't finished processing R3 NMI data before the flags are cleared, it would lose pending NMI deliveries. In lockstep (B-Em) this can't happen because the parasite processes each NMI before the host can clear the flags.

### Hypothesis 2 seems most likely because:

- The R1 H→P blocking write (`BVC` loop) guarantees synchronisation for R1 — the host can't write faster than the parasite reads
- R3 uses NMI-driven transfer which depends on PNMI edge detection
- The PNMI edge detection in the cross-process model has had bugs before (the `pending` field race that was fixed earlier in this branch)
- The R3 P→H still has pending data (`count=1/1, [00]`) that the host hasn't read, suggesting the R3 protocol didn't complete cleanly

## Next Steps

1. **Instrument R1 byte counting.** Add counters to TubeHostPort R1 H→P write and TubeParasitePort R1 H→P read to verify the byte count matches on both sides.

2. **Instrument R3 transfer.** Count R3 H→P writes (host) and reads (parasite) during Phase 1 to check for lost bytes.

3. **Check PNMI edge detection during flag transitions.** When the host clears M at `$6A38`, any pending PNMI edge should still be delivered. Verify this works correctly in the cross-process model.

4. **Compare R3 data.** Dump the parasite memory region that was filled by R3 NMI transfer and compare with the host source data (`$6057-$628D`) to check for corruption.

## Files

- Test suite: `clients/python/tests/test_tube_chuckie_egg.py`
- Disc image: `tests/assets/discs/chuckieEgg2023.ssd`
- Host transfer code: loaded into host RAM at `$6000-$6BFF` by the game loader
- Parasite decompressor: loaded into parasite RAM at `$0800-$0A35` (with copy at `$FC00-$FC1A`)
- Tube register I/O: `src/core/src/TubeHostPort.cpp`, `src/core/src/TubeParasitePort.cpp`
- PNMI edge detection: `TubeParasitePort::update_pnmi()`
