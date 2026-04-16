# Tube R3 Correctness and Pacing Investigation

Date: 2026-04-06. Branch: `l3fs-econet-and-tube-r3-fix` (preserved).

## Two Distinct Problems

### Problem 1: R3 P→H byte-doubling (correctness)

BASIC SAVE over DFS with the 65C02 Tube active doubles bytes. Discovered
while saving WFSINIT from ADFS to DFS during L3FS setup work.

**Reproduction**: `integration_tests/tube-save/` — the `test_save_with_tube`
test fails with the original code, passes with the full fix (including the
sleep-wait that causes the pacing regression).

**Root cause confirmed**: two separate issues combine:

1. **Status register hysteresis** — the cross-process `TubeShared` R3
   implementation re-derives status flags from `count >= threshold` on
   every status register read. Reference emulators (B-em, BeebEm, B2)
   instead maintain sticky status bits set/cleared as side-effects of
   FIFO operations. In V=1 (two-byte) mode, after the host reads byte 1
   of a pair (count drops from 2 to 1), `count < threshold(2)` falsely
   indicates "no data" and "space available", corrupting the transfer.

2. **Cross-process timing** — the host process can read R3 data faster
   than the parasite process can write, because they run as separate OS
   processes with no cycle-level synchronisation. When the host reads an
   empty FIFO, `dequeue_r3_p2h` returns the stale latch value, inserting
   a duplicate byte. Single-process emulators avoid this because host and
   parasite share a thread with deterministic interleaving.

### Problem 2: Debug-build pacing regression (performance)

The Tube-equipped emulator can't maintain 2 MHz in debug builds. It runs
at ~1.8 MHz (90%) with 99.9% CPU utilisation. This predates the R3 fix
work — it's present at master tip and also at commit `9dc1e77` (which is
before the Windows pacing rework). The regression may go back to the
pacing system rewrite (~45 commits between `801237a` and master).

In release builds, the target is met at ~18% CPU. The debug build is too
slow to execute the emulation loop fast enough within the pacing quantum.

---

## What Was Tried on `l3fs-econet-and-tube-r3-fix`

### Attempt 1: Pending flag (atomic, separate from count)

Added `std::atomic<uint8_t> pending` to `TubeReg3`. Set by producer
when count reaches threshold, cleared by consumer when count reaches 0.

**Status reads**: `pending || count >= threshold` for data-available,
`pending == 0 && count < threshold` for space-available.

**Result**: Boot hang. Race condition between `count.fetch_add` and
`pending.store` — the parasite checks between these two operations and
sees inconsistent state. The AND condition on space-available was too
restrictive: after consumer clears pending but before the window closes,
the producer can't see space.

**Lesson**: Two separate atomics can't be updated atomically together.
The window between updating count and updating the flag is a fundamental
problem in the cross-process model.

### Attempt 2: Deferred count decrement (read_phase tracking)

Don't decrement count until both bytes of a V=1 pair are consumed.
Track read_phase (0 or 1) locally in the consumer. Decrement count by 2
when read_phase wraps to 0.

**Result**: Partially worked (first ~9 bytes correct) but read_phase got
out of sync with the transfer protocol. Adding read_phase reset on V flag
changes or R4 reads helped but didn't fully solve it. Also caused CE2023
boot hangs due to interaction with the V-flag change during transfer setup.

**Lesson**: The read_phase approach is fragile because V can change
between transfers, and the phase must be precisely synchronised with the
protocol state machine.

### Attempt 3: Sticky `data_available` flag (final approach)

Added `std::atomic<uint8_t> data_available` to `TubeReg3`. This matches
the reference emulators' pattern exactly: a sticky flag that's SET by the
producer when count reaches threshold, and CLEARED by the consumer when
count reaches 0.

**Status reads**: `data_available != 0` for data-available (bit 7).
For space-available (bit 6): `count < threshold` for H→P direction
(host writes), `data_available == 0` for P→H direction (parasite writes
in status/PNMI).

**PNMI**: `count >= threshold` for h2p_data (unchanged), `count == 0`
for p2h_space (changed from `count < threshold`, matching BeebEm's
`UpdateR3Interrupt` which uses `ph3pos == 0`).

**Result**: Status register semantics are correct. CE2023 boots. But the
SAVE test still fails because of Problem 2 (cross-process timing) — the
host reads R3 data faster than the parasite writes.

### Attempt 4: io_pending wakeup on R3 reads

Set `io_pending_parasite` after host reads R3 P→H data. Set
`io_pending_host` after parasite reads R3 H→P data. This wakes the other
process's PacingClock from sleep so it can refill/drain the FIFO.

**Result**: Improved (first 5-6 bytes correct vs 3 originally) but not
sufficient alone. The PacingClock polls `io_pending` every 100µs which
is too slow for the ~10-24µs per-byte transfer timing. When set
unconditionally (on every R3 read including empty reads), it caused the
pacing regression by preventing the other process from ever sleeping.
When set conditionally (only when data was actually consumed), it wasn't
sufficient to fix the byte-doubling.

### Attempt 5: Sleep-wait in dequeue_r3_p2h

When the P→H FIFO is empty and M is set (transfer active), sleep briefly
to let the parasite process catch up. Tried various forms:

- `yield()` x 1000: fixed SAVE but 99.7% CPU, 1.7 MHz (unusable)
- `yield()` x 50: not enough yields, SAVE still fails
- `sleep_for(5µs)` x 10: fixed SAVE but still 99.9% CPU, 1.8 MHz
- Single `yield()`: not enough, SAVE still fails

**Result**: The only approach that fixed the SAVE test, but all variants
cause unacceptable pacing regression because M stays set during idle
operation. The host's MOS Tube polling loop reads R3 status/data even
when no transfer is active, hitting the sleep path thousands of times
per second.

**Lesson**: Any delay in the dequeue path fires during idle polling, not
just during active transfers. M is not a reliable indicator of "transfer
in progress" — it stays set between transfers.

### Combined fix (Attempt 3 + 4 + 5)

The version committed as `2782d2f` on the branch combines the sticky
flag, conditional io_pending, and sleep-wait. It fixes SAVE correctness
and CE2023 boots, but has the pacing regression. The branch was then
reverted to master C++ baseline and the commit soft-reset.

---

## Key Insights

### Why the reference emulators don't have this problem

B-em, BeebEm, and B2 run both host and parasite in the same thread.
Each host instruction is followed by the corresponding parasite
instructions. When the host reads R3 P→H, the parasite has already
executed its NMI handler and written the byte. There's no timing gap.

### What's needed for a correct cross-process fix

The fix must ensure the parasite has time to write before the host reads,
WITHOUT adding latency to the idle R3 polling path. Options to explore:

1. **Bus-stretch R3 P→H reads during active transfers** — the host spins
   on a flag that the parasite sets after writing. Similar to existing
   R3 H→P write bus-stretching. But real hardware doesn't bus-stretch R3
   reads — the host gets whatever is there.

2. **Smarter transfer-active detection** — instead of checking M (which
   stays set), track whether a P→H transfer is actually in progress
   (e.g., the host has sent a type 0/2/6 command via R4 and the parasite
   hasn't sent the release via R4). This requires protocol-level state
   tracking.

3. **Pacing system integration** — modify the PacingClock to synchronise
   host and parasite more tightly during R3 transfers. When the host
   reads R3 and the FIFO is empty, yield the host's pacing quantum to
   the parasite. This keeps the synchronisation in the pacing layer
   rather than polluting the register access path.

4. **Reduce pacing quantum during Tube operation** — a smaller quantum
   means tighter interleaving, reducing the chance of the host outpacing
   the parasite. But this increases pacing overhead.

5. **Cross-process cycle-level synchronisation** — the nuclear option.
   Use a shared cycle counter to ensure the host doesn't advance past
   the parasite's expected response time. This is what the in-process
   TubeUla does implicitly by sharing a thread.

### The debug-build pacing regression is separate

The ~1.8 MHz ceiling in debug builds is NOT caused by the R3 fix. It's
present at master tip and at `9dc1e77`. The emulation loop is simply too
slow in debug to sustain 2 MHz within the pacing quantum. This needs
separate investigation — likely profiling the hot loop to find debug-only
overhead (assertions, iterator debugging, etc.).

---

## Files on the Branch

| File | Status |
|------|--------|
| `src/core/include/beebium/tube/TubeShared.hpp` | Reverted to master (data_available field removed) |
| `src/core/src/TubeHostPort.cpp` | Reverted to master |
| `src/core/src/TubeParasitePort.cpp` | Reverted to master |
| `integration_tests/tube-save/` | Working test suite (reproduces bug) |
| `scripts/wfsinit/` | Clean WFSINIT extraction |
| `docs/level-3-file-server-setup.md` | L3FS setup guide |
| `docs/discussion/l3fs-next-steps.md` | WFSINIT analysis handoff |
| `scripts/adfs-disc-tools/` | Blank ADFS image tool |

The C++ fix code is in the git reflog and in the commit messages on the
branch. The approach from Attempt 3 (sticky `data_available` flag) is
correct for the status register semantics. The remaining problem is
purely the cross-process timing gap during active P→H transfers.

## Reference Emulator Code

The definitive reference for R3 semantics:

**BeebEm** (`/Users/rjs/Code/beebem-mac/Src/Tube.cpp`):
- `ReadTubeFromHostSide` case 5: byte-by-byte consumption, clear
  status when `R3PHPtr == 0`
- `WriteTubeFromParasiteSide` case 5: set status when
  `R3PHPtr >= threshold`
- `UpdateR3Interrupt`: V=0 NMI on `(hp3pos > 0) || (ph3pos == 0)`,
  V=1 NMI on `(hp3pos > 1) || (ph3pos == 0)`

**B-em** (`/Users/rjs/Code/b-em/src/tube.c`):
- Same pattern, uses `ph3pos`/`hp3pos` counters
- Status flags as explicit variables, not recomputed

**B2** (`/Users/rjs/Code/b2/src/beeb/src/tube.cpp`):
- `WriteFIFO3`/`ReadFIFO3` with `UpdatePNMI`
- Same byte-by-byte consumption with status set/clear on threshold/empty
