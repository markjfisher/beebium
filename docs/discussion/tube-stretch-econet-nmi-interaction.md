# Tube Stretch / Econet NMI Interaction

Investigation into why `*I AM SYST` fails between two Beebium instances
connected via AUN, even after fixing the R3 read-side clock stretching.

## Status: RESOLVED (2026-04-14)

Two bugs identified and fixed.

- **Bug 1 (commit `4362fff`):** R3 read-side clock stretching burned
  host cycles in a tight parasite-tick loop without advancing
  peripherals. Fixed by deferring the read stretch through
  `Machine::step()`.
- **Bug 2 (commit `2c96d73`):** Incorrect stretch branch in
  `TubeUla::host_read` case 5 caused a deadlock with ANFS's
  `tube_transfer_setup`. Fixed by removing the stretch; matches real
  hardware and all four reference emulators (B2, BeebEm, jsbeeb, B-Em).

**Verification:**

- All 100 Tube unit tests pass.
- All 88 Econet/MC6854 unit tests pass (except intentional
  documentation test #21).
- Integration test `test_l3fs_floppy_client_login` passes.
- Interactive confirmation: `*I AM SYST`, `*.`, `*LCAT`, `*DATE` all
  work against a Beebium-hosted L3FS server loaded from the
  `L3FS-KL.adl` floppy image.

Commit history on `econet-investigation` branch:

```
2c96d73  Fix Tube R3 P-to-H deadlock: remove incorrect host-read stretch  <- Bug 2 fix
aaa04bc  WIP: R3 P-to-H initial state = 2 dummy bytes (superseded wrong path, kept in history)
53b0d8c  Add diagnostic instrumentation for Tube/Econet NMI investigation
4362fff  Fix Tube R3 read-side clock stretching to tick peripherals         <- Bug 1 fix
b02a465  Econet investigation: identify R3 read stretch as root cause
```

## Original Problem Statement

(Left for historical context — the investigation below tracks the
journey from first symptom to root cause.)

## Bug 1: R3 Read-Side Clock Stretching (Fixed)

**Commit:** `4362fff` on branch `econet-investigation`

### Symptom

The server's effective emulation rate dropped from 2 MHz to 0.17 MHz
during L3FS disc I/O. The `read_stretch_parasite_ticks` counter reached
753 million.

### Root Cause

`TubeSocket::read()` had an inline tight `while(backend->stretched())`
loop that ticked only the parasite CPU without incrementing
`cycle_count` or ticking any peripherals (VIAs, video, sound, Econet
ADLC). During Tube R3 P-to-H read stretches, the parasite was
consuming massive numbers of ticks inside a single host CPU cycle.

### Fix

Replaced the inline tight loop with the same deferred-stretch mechanism
used for write stretches. The 6502 library's cycle-accurate model has a
key property: `tfn` sets up `abus`/`read`, the caller does the memory
transfer, and the CPU doesn't consume `dbus` until the next `tfn` call.
During a read stretch, `Machine::step()` enters the stretch path
(ticking parasite + all peripherals + incrementing `cycle_count`), and
when the stretch clears, patches `cpu.dbus` with the actual value
before the CPU's next cycle consumes it.

### Verification

- All 97 Tube and Econet unit tests pass.
- Server emulation rate restored to 2.00 MHz (was 0.17 MHz).
- `read_stretch_parasite_ticks` drops to 0 (was 754 million).


## Bug 2: Econet NMI Not Serviced During Long Stretch Periods

### Symptom

After the R3 read stretch fix, the server runs at full 2 MHz but still
fails to reply to the client's `*I AM SYST`. The client receives
"No reply from station 254" after the 22-second timeout.

The server's FourWayHandshake enters `ScoutSent` for the reply, the
scout ack timer fires, the fake scout ack is generated into the ADLC's
rx_queue_. But the NFS ROM's NMI handler never runs (`send_stage_log`
never shows a 6th entry beyond `Idle,ImmRcvd,ScoutRcvd,DataRcvd,Idle`),
and after 500,000 ticks the watchdog resets the handshake.

### Diagnostic Evidence Chain

Each step was verified with targeted `fprintf(stderr, ...)` logging
in the server process:

1. **FourWayHandshake generates the fake scout ack on schedule:**
   ```
   [FWH] Scout ack generated, rx_queue size=1
   ```

2. **ADLC fetches the frame and pushes byte 0 into the RX FIFO:**
   ```
   [ADLC] RX frame #4 received, 4 bytes, cr1=0x82, rie=1, rda_before=0
   ```

3. **ADLC correctly asserts IRQ (RDA set, RIE enabled):**
   ```
   [ADLC] IRQ 0->1 (frame4+), rx_cause=1, rda=1, fv=0, ovrn=0, cr1=0x82, fifo=[1,0,0]
   ```

4. **EconetSocket caches IRQ, `nmi_pending()` returns true, and this
   all happens INSIDE a stretch tick:**
   ```
   [ECONET] tick_rising: irq 0->1, ff=1, pending=1, tick=61147617, IN_STRETCH=1
   ```

5. **The CPU is halted in the Tube stretch and cannot service the NMI.
   Subsequent byte timer ticks during the stretch push bytes 1-2 into
   the FIFO (filling all 3 slots), then byte 3 overruns:**
   ```
   [ADLC] RX FIFO FULL, overrun at byte 3/4, cr1=0x82, irq=1
   ```

6. **FV (Frame Valid) is never set** because the last byte never enters
   the FIFO. The NFS ROM's NMI handler never processes the scout ack
   as a complete frame. The handshake times out.

### Root Cause Hypothesis

The ADLC byte trickle timer advances during `tick_stretch_cycle()` at
the same rate as during normal execution. With the 3-byte MC6854 RX
FIFO and a 4-byte scout ack frame, if the CPU is halted for 4 byte
periods (~256 cycles at default byte_period=128), all 3 FIFO slots
fill before the CPU can drain any, and the 4th byte overruns.

On real hardware this cannot happen in the same way because the
MC6854's E clock is derived from the system clock -- when the Tube ULA
halts the CPU, the ADLC halts with it, and no bytes are pushed to the
FIFO during stretch.

### ANFS ROM Behaviour (from disassembly analysis)

The ANFS NMI handler chain was analysed to confirm the ROM's
INTON/INTOFF usage is correct:

- Every NMI handler entry calls INTOFF (`BIT &FE18`).
- Every NMI handler exit goes through `nmi_rti` which calls INTON
  (`BIT &FE20`) before RTI.
- After the incoming RX handshake completes, the final NMI exits with
  INTON active. The ADLC is in RX listen mode (`CR1=&82`).
- When the scout ack arrives, the ADLC asserts IRQ. With INTON already
  active, the NMI pin goes low (falling edge). This would trigger the
  NMI in normal circumstances.

## Candidate Fixes Tried

### Attempt 1: Stop ticking the ADLC during stretch (tick_handshake_only)

**Approach:** Added `EconetSocket::tick_handshake_only()` that advances
only the FourWayHandshake timers. `tick_stretch_cycle()` called this
instead of the full `tick_rising()`/`tick_falling()`.

**Result:** The ADLC never received frame 4 at all
(`rx_frames_received` stayed at 3). During the continuous Tube stretch
period covering the disc I/O, the ADLC's byte timer never advanced,
so the rx_queue_ frame was never fetched into the ADLC. The watchdog
still fired.

**Lesson:** The ADLC byte timer must keep advancing during stretch or
frames can never be processed at all, because the CPU spends the
majority of its time in stretch cycles during L3FS disc I/O.

### Attempt 2: Stop ticking Econet entirely during stretch

**Approach:** Removed all Econet ticking from `tick_stretch_cycle()`,
under the assumption that on real hardware the entire Econet subsystem
halts with the CPU.

**Result:** Server emulation dropped back to 8.7% of nominal (same
symptom as Bug 1 but for a different reason). The FourWayHandshake
timers fell hopelessly behind the cycle count. Scout ack was never
generated (`scout_ack_generated: 0`).

**Lesson:** The FourWayHandshake timers must advance during stretch
because the L3FS disc I/O causes the CPU to spend most of its time in
stretch -- if the handshake pauses with the CPU, the simulated AUN
network timing collapses and the protocol never completes.

### Attempt 3: Stall the FIFO instead of discarding (OVRN suppression)

**Approach:** In `Mc6854::rx_process_byte()`, when the FIFO is full,
return without setting OVRN and without advancing `rx_buffer_index_`.
On the next byte trickle tick, retry pushing the byte.

**Result:** The FIFO overflow diagnostic no longer fires. The ADLC
correctly receives frame 4 (`rx_frames_received: 4`). But the NMI
handler still does not run (`send_stage_log` unchanged, watchdog
still fires).

**Current puzzle:** With the FIFO stall, the ADLC holds 3 bytes and
the 4th byte waits in `rx_frame_buffer_`. `nmi_flags` should be set
from the initial RDA edge. When the CPU resumes from stretch, the NMI
should fire. But it apparently doesn't. Something is preventing the
NMI handler from executing during the 250 ms watchdog window.

## Hypothesis 1 (Lost NMI Edge): REFUTED

### Unit Test

Added `tests/test_6502_nmi_concurrent_sources.cpp` which directly
exercises `M6502_SetDeviceNMI` with concurrent disc + Econet sources.
The critical test case DOES fail:

```
NMI edge detection: Econet edge lost when disc NMI handler runs first
                    and Econet source remains asserted
  nmi_flags=0x0
  device_nmi_flags=0x2
  CHECK( (cpu.nmi_flags & kEconetMask) != 0 )   [FAILED]
```

This confirms the 6502 library's edge detection CAN lose edges in
principle. However, this is actually **hardware-accurate**: on real
BBC Micros, NMI is wired-OR on an open-collector line. If both disc
and Econet drive the line low and only one releases, the pin stays
low with no new edge -- matching the emulator behaviour.

Real BBC Micros work because the ANFS NMI handler at &0D00 ALWAYS
calls INTOFF first (`BIT &FE18`), releasing the Econet driver
immediately. It then checks ADLC SR1 to decide whether to handle
Econet or chain to the disc handler. INTON at nmi_rti restores the
Econet gate if the Econet path ran.

Integration test instrumentation added to Machine::step() in both the
normal path and `tick_stretch_cycle()`, logging NMI state at intervals
after frame 4 (the scout ack) arrives. Ran the integration test:

```
[STRETCH+0]      nmi_pending=1, ff=1, irq=1, dev_nmi=0x00, nmi_flags=0x00, rx_fifo_empty=0
[STRETCH+1000]   nmi_pending=1, ff=1, irq=1, dev_nmi=0x02, nmi_flags=0x02, rx_fifo_empty=0
[STRETCH+10000]  nmi_pending=1, ff=1, irq=1, dev_nmi=0x02, nmi_flags=0x02, rx_fifo_empty=0
[STRETCH+100000] nmi_pending=1, ff=1, irq=1, dev_nmi=0x02, nmi_flags=0x02, rx_fifo_empty=0
[STRETCH+400000] nmi_pending=1, ff=1, irq=1, dev_nmi=0x02, nmi_flags=0x02, rx_fifo_empty=0
```

Observations:

- `nmi_flags = 0x02` from tick 1000 onward -- **the NMI edge IS
  detected and latched**. The 6502 has a pending NMI for the entire
  400,000+ ticks of the watchdog window.
- `rx_fifo_empty = 0` -- the ADLC has bytes waiting.
- `ff = 1`, `irq = 1` -- INTON flip-flop is on, ADLC IRQ asserted.
- All samples prefixed `[STRETCH+]` -- they come from
  `tick_stretch_cycle()`. **The normal step path NEVER runs after
  frame 4 arrives.**

The 6502 never gets to check `nmi_flags` because it is continuously
halted by a Tube stretch. The NMI handler cannot execute because
the CPU cannot execute any instructions.

## Hypothesis 2 (Revised): Tube Protocol Deadlock -- CONFIRMED

### Evidence

Added `pending_offset()` and `pending_is_read()` accessors to
`TubeUla`, plus a `diag_pc()` virtual method to `ParasiteTickable`
overridden in `ParasiteRunner`. Instrumented Machine::step() to log
the stretch register and parasite PC at intervals after frame 4.

Output:

```
[STRETCH-INFO+0]      tube_ula offset=5, is_read=1, parasite_pc=0xFDBE
[STRETCH-INFO+1000]   tube_ula offset=5, is_read=1, parasite_pc=0xFDBE
[STRETCH-INFO+10000]  tube_ula offset=5, is_read=1, parasite_pc=0xFDC1
[STRETCH-INFO+100000] tube_ula offset=5, is_read=1, parasite_pc=0xFDC1
[STRETCH-INFO+400000] tube_ula offset=5, is_read=1, parasite_pc=0xFDBE
```

**The deadlock:**

- **Host:** stuck in a Tube READ stretch on register offset 5 (R3
  P-to-H data) for the entire 400,000-tick watchdog window. The host
  is reading from an empty R3, waiting for the parasite to write data
  there.
- **Parasite:** stuck in a tight polling loop oscillating between PCs
  `0xFDBE` and `0xFDC1`. This is a 2-3 instruction loop in the
  parasite's MOS ROM (the second-processor MOS image at
  `0xF800-0xFFFF`). The parasite is polling for some condition that
  the host can only satisfy after processing the scout-ack NMI.

Both sides are spinning forever. Neither makes progress.

### Why the Watchdog Fires

The FourWayHandshake watchdog timer keeps counting during the stretch
(the handshake is ticked in `tick_stretch_cycle`). After 500,000
ticks it fires `on_watchdog_timeout()` which resets the handshake to
Idle. The reply scout's handshake state is destroyed and the client
gets "No reply".

### What the Parasite is Polling

The PC range `0xFDBE-0xFDC1` is in the parasite's MOS ROM. The
parasite MOS image is at `tests/assets/.../acorn-tube-6502_1_10.rom`
or similar. The MOS routines around `0xFDxx` typically include the
host-communication primitives (TUBE_R1, TUBE_R2, TUBE_R3 send/recv
stubs). The parasite is presumably polling a Tube status register
(e.g., R3 status at `0xFEE4`) waiting for the host to drain a
register before it sends more data.

But the host can't drain anything because it's stuck reading from R3
trying to receive data the parasite hasn't sent yet. The two sides
have apparently disagreed about who should send next.

### Plausible Root Cause

The L3FS reply data flow (parasite supplies bytes -> host reads from
R3 P-to-H -> host writes to ADLC TX FIFO) requires the parasite to
write the FIRST byte before the host begins reading. If the host
begins reading too eagerly (before the parasite has written), it
stalls, and the parasite then has no way to know it should write
(because the parasite's polling loop expects something else first).

This may be a sequencing bug in either:

1. The ANFS ROM TX setup -- reading R3 P-to-H before the parasite
   has been notified to start writing.
2. The L3FS parasite code -- not knowing it should start writing
   reply bytes after issuing OSWORD &10.
3. Beebium's Tube ULA model -- some asymmetric initialisation that
   doesn't match real hardware behaviour for this transfer mode.

This deadlock would presumably not occur on real hardware (or on B2,
which the integration test successfully ran against). The next
investigation step is to:

(a) Disassemble the parasite MOS code at `0xFDBE-0xFDC1` to identify
    the exact polling loop and what status it expects.

(b) Compare with the host's tx_prepare flow (from the ANFS agent's
    earlier disassembly) to find the protocol mismatch.

(c) Determine whether the host's `tx_prepare` should be reading R3
    P-to-H at all at this point, or whether it should be waiting for
    a parasite-driven event first.

### Updated Bug Summary

The original Bug 2 description ("Econet NMI not serviced during long
stretch periods") was correct in its observation but wrong in its
root cause attribution.

- The original FIFO overflow was a SECONDARY symptom of the host
  being unable to drain the FIFO during the persistent stretch.
- The "lost NMI edge" hypothesis was REFUTED -- `nmi_flags` is
  correctly set and stays set throughout the stretch.
- The TRUE root cause is a Tube protocol deadlock between the
  host (in tx_prepare) and the parasite (in a polling loop in MOS).

## Previous Hypothesis 1 Details (Kept for History)

Diagnostic data with Attempt 3 applied:

- Server at 2.00 MHz (100% nominal), 0.5000 ticks/cycle ratio
- `rx_frames_received: 4` -- ADLC receives the scout ack frame
- `scout_ack_generated: 1` -- FourWayHandshake timer fires correctly
- `watchdog_timeouts: 1` -- watchdog fires at 500,000 ticks (250ms)
- `send_stage_log: Idle,ImmRcvd,ScoutRcvd,DataRcvd,Idle` -- no 6th entry

The server has full 250 ms of emulated time after the scout ack
arrives. The NMI handler takes ~60 cycles (~30us). There should be
ample time for the handler to run. But it doesn't.

### Suspected Mechanism

Hypothesis: concurrent disc NMI firings during the long stretch
period cause the Econet NMI to be "lost" through the M6502's edge
detection logic.

The 6502 library's NMI edge detection:

```c
void M6502_SetDeviceNMI(M6502 *s, ..., int wants_nmi) {
    if (wants_nmi) {
        s->nmi_flags |= ~s->device_nmi_flags & mask;
        s->device_nmi_flags |= mask;
    } else {
        s->device_nmi_flags &= ~mask;
    }
}
```

An edge is only recorded in `nmi_flags` on the 0->1 transition of
`device_nmi_flags`. `T4_Interrupt` clears ALL of `nmi_flags` when the
NMI is entered, but does NOT clear `device_nmi_flags`.

If disc NMI (bit 0) and Econet NMI (bit 1) both have their
`device_nmi_flags` bits set at the same time, and T4_Interrupt runs
for one of them, the other bit is "stuck" set in `device_nmi_flags`
with no corresponding bit in `nmi_flags`. No further edge will be
detected for that device until something causes `device_nmi_flags` to
transition back to 0 and then to 1.

For the Econet path, `nmi_pending()` returning false (via INTOFF in
the handler) should clear the Econet `device_nmi_flags` bit, and INTON
should re-assert it. So the Econet edge should be recoverable -- if
the Econet NMI handler actually runs.

But what if the DISC NMI handler runs instead? The disc handler does
not call INTOFF (that's an Econet mechanism). So `device_nmi_flags`
bit 1 (Econet) stays set. After RTI, `nmi_flags` is 0 and
`device_nmi_flags` has the Econet bit -- no new Econet edge will fire
until something toggles `nmi_enable_ff_`, which only happens via the
NFS ROM's INTOFF/INTON pattern, which only runs inside the NMI
handler, which needs a new NMI edge to fire ...

This is a plausible lockout scenario that would explain the observed
behaviour, but it needs verification. The next diagnostic step is to
add logging inside the 6502 interrupt entry sequence or at the NMI
vector to confirm whether the NMI handler fires at all during the
500,000-tick watchdog window.

## Files Modified (in working tree, not committed)

- `src/core/include/beebium/econet/Mc6854.hpp` -- FIFO stall change
  (return without setting OVRN when FIFO is full)
- `src/core/include/beebium/econet/EconetSocket.hpp` -- added unused
  `tick_handshake_only()` method (kept for future experimentation)
- `src/core/include/beebium/Machine.hpp` -- unchanged from Bug 1 fix
- All temporary `fprintf` diagnostics removed

## How to Verify the Fix (when identified)

1. Existing Tube tests must pass: `ctest -R tube --output-on-failure`
2. Existing Econet tests must pass: `ctest -R econet --output-on-failure`
3. The unit test `test_econet_tx_with_tube` must pass
4. The integration test must pass:
   ```
   cd integration_tests/l3fs
   uv run pytest -m slow tests/test_l3fs_floppy_econet.py -v -s --timeout=300
   ```
   - Client should log in successfully without "No reply"
   - `send_stage_log` should show the reply TX stages (ScoutAckRcvd,
     DataSent)
   - `watchdog_timeouts` should be 0
   - Both server and client should maintain ~2 MHz emulation rate

## Next Steps

1. Instrument the 6502 library (or Machine::step) to detect when NMI
   entry actually occurs versus when `nmi_flags` is set. This will
   confirm or refute the "lost NMI edge" hypothesis.

2. If the lost-edge hypothesis is confirmed, the fix is likely to
   either:
   - Clear `device_nmi_flags` in `T4_Interrupt` (matching real
     hardware where the NMI pin is sampled once on entry)
   - Or explicitly re-assert Econet NMI after the NMI handler exits
     if `nmi_pending()` is still true

3. Investigate whether a smaller-scope fix might work: only clear the
   Econet device bit when the Econet handler enters (not the disc
   handler). This needs a way for the NFS ROM to signal "I am handling
   the Econet NMI" -- which it does via INTOFF.

4. Consider whether the FIFO stall change should stay regardless of
   whether it fully fixes the problem: it prevents OVRN from setting
   in scenarios where the AUN model delivers bytes faster than the
   real Econet wire would. OVRN would cause the NFS ROM to
   retransmit, which is undesirable for locally-generated fake frames.

---

## Root Cause of Bug 2 (Identified via Parasite Agent Disassembly)

Cross-referencing the ANFS ROM disassembly (agent) and the Acorn 6502
Tube Client ROM v1.10 disassembly (agent) revealed the exact protocol
mismatch that causes the deadlock.

### The Tube Transfer Setup Protocol

When ANFS handles `OSWORD &10` (Econet Transmit) on a Tube system with
a parasite-side TX buffer, it invokes `tube_transfer_setup` at `&BF39`
(relocated `&0435`). The host-side R4 write sequence for transfer
types 0 and 2 (both parasite→host R3 transfers) is:

1. Write `type` to R4
2. Write `tube_claimed_id` to R4 (the Econet-specific "called ID" byte)
3. Write address byte 3 to R4 (big-endian MSB)
4. Write address byte 2 to R4
5. Write address byte 1 to R4
6. Write address byte 0 to R4 (big-endian LSB)
7. Write `&18` then the type's ctrl byte to Tube control register (&FEE0);
   for types 0 and 2 this SETS the V (two-byte R3) flag
8. (If bit 2 of ctrl-byte = 1: types 0 and 2) do **two `BIT &FEE5`
   reads** to flush R3 P-to-H (drain stale bytes)
9. Write trigger/sync byte to R4 (the 7th R4 write)
10. Poll R4 status for parasite ack

The Acorn 6502 Tube Client ROM v1.10 `data_transfer_setup` handler at
`&FD65` reads, in order, from R4:

1. Type byte (at `tube_r4_interrupt`, `&FD3F` — which falls through
   into `data_transfer_setup`)
2. Called-ID byte (`&FD88`, discarded by the handler)
3. Address byte 4 (`&FD98`, discarded)
4. Address byte 3 (`&FDA0`, discarded)
5. Address byte 2 (`&FDA8`, stored)
6. Address byte 1 (`&FDB3`, stored)

Then does **two `BIT &FEFD` reads** at `&FDB8`/`&FDBB` which are
claimed by the ROM comments to be "dummy reads" but which consume two
bytes from the R3 H-to-P FIFO.

Then enters the `transfer_wait_sync` polling loop (`&FDBE`/`&FDC1`),
waiting for bit 7 of R4 status to go high, which fires when the host
makes its 7th R4 write (the sync byte).

### The Symmetric Asymmetric Flush

Both sides do a 2-byte R3 flush, **but in opposite directions of R3:**

- **Parasite flush (2 × `BIT &FEFD`):** reads R3 H-to-P — the
  direction in which the host writes to R3 and the parasite reads.
- **Host flush (2 × `BIT &FEE5`):** reads R3 P-to-H — the direction
  in which the parasite writes to R3 and the host reads.

They are NOT reading the same data. Each side is draining its own
receive side of R3.

### The Deadlock

In Beebium's `TubeUla::host_read` case 5 (R3 P-to-H data), a read
from an empty FIFO **stretches** the host CPU if either the M or V
flag is set in the Tube control register. The comment explicitly
notes: *"Without either flag, reads from empty R3 return 0 without
stretching (used for dummy BIT reads that drain stale data during
setup)"* — acknowledging the flush use case.

But for transfer types 0 and 2, ANFS **sets V to 1 immediately before
the flush reads**. So the flush reads happen with V=1, which means
they stretch when R3 P-to-H is empty.

Beebium's `TubeUla::soft_reset` initialises R3 P-to-H with a single
dummy byte (`count = 1`, `tail = 1`). The first host flush read
consumes the dummy and the FIFO becomes empty. The second flush read
hits the empty FIFO with V=1 and stretches.

The parasite has already completed its own (non-stretching) 2-byte R3
H-to-P flush and has reached `transfer_wait_sync`. It is waiting for
the 7th R4 byte. The host cannot make that write because it is
halted in the R3 read stretch.

**Classic deadlock: host stuck reading empty R3, parasite stuck
waiting for R4 sync byte.**

### Why This Works on Real Hardware

On real BBC Micros (and on BeebEm, per the original handover), this
transaction works correctly. So either:

1. Real Tube ULA hardware initialises R3 P-to-H with **two** dummy
   bytes, not one; or
2. Real Tube ULA hardware does not stretch on empty R3 reads under
   the specific timing/control-flag conditions that exist during the
   flush; or
3. BeebEm's Tube model differs from ours in some other way (perhaps
   it doesn't model stretching at all for R3 P-to-H); or
4. Some other asymmetric initial state (e.g. `pending` set vs clear)
   changes how the stretch is evaluated in real hardware.

We have not yet determined which of these is correct.

---

## Candidate Fix (Committed as WIP in `aaa04bc`)

### What the fix does

In `TubeUla::soft_reset`, initialise R3 P-to-H with **two** dummy
bytes (`count = 2`, `tail = 0`, `pending = true`) instead of one. This
satisfies ANFS's 2-byte flush: both reads consume a dummy byte
without stretching, the host proceeds to write the sync byte, the
parasite dispatches, and the actual data transfer succeeds.

### Evidence the fix works

L3FS integration test passes for the first time:

```
Server rx_frames_received: 5      (was 4)
Server scout_ack_generated: 1
Server watchdog_timeouts: 0       (was 1)
Server send_stage_log: Idle,ImmRcvd,ScoutRcvd,DataRcvd,Idle,ScoutAckRcvd
  (6th stage = reply data TX sent)
Client send_stage_log: Idle,Idle,ScoutAckRcvd,ScoutRcvd,DataRcvd
  (client received reply)
```

No "No reply" message. Server maintains 2 MHz emulation throughout.

### Evidence the fix may be wrong

**Eight unit tests fail** with this change. They fall into two
categories:

**Tests encoding the old count=1 invariant directly:**

- `test_tube_ula.cpp:269` — "R3 one-byte mode": expects
  `host_read(5) == 0x88`. Presumably the test wrote 0x88 to R3 P-to-H
  via the parasite and expected the host to see it on the first
  read. With count=2, the host reads the dummy byte first.
- `test_tube_ula.cpp:330` — "R3 two-byte mode": DATA_AVAILABLE flag
  check after reset is now inverted (dummy bytes count as available).
- `test_tube_ula.cpp:356` — SPACE_AVAILABLE flag check.
- `test_tube_ula.cpp:758, 766` — "PNMI generation": probably checks
  the PNMI transitions as R3 fills/drains. The initial count=2 may
  change which transitions fire spurious PNMIs.
- `test_tube_ula.cpp:957, 979` — Status register layout, probably
  checking specific initial state.
- `test_tube_r3_p2h_6502.cpp:106, 139` — End-to-end R3 P-to-H
  transfer tests driven by a 6502 program. The program expects to
  receive data starting at byte 0; with count=2 it receives 2 dummy
  bytes first.
- `test_mc6854.cpp:2995` — "NFS Error Recovery: DISCONTINUE after
  overrun": actually unrelated to R3 -- was broken by an earlier
  FIFO-stall change that has since been reverted in the WIP commit.
  Should now pass; needs verification.

**Interpretation:** if the tests encode the *designed* initial state
of the Tube model (count=1), and that design matches real hardware,
then our fix is simply compensating for a different bug -- perhaps in
the stretch logic, perhaps in the V-flag semantics, perhaps somewhere
else entirely. The failing tests then become important evidence that
count=2 is the wrong fix.

If the tests were written to match whatever state the emulator
happened to be in at the time (not a deliberate hardware-fidelity
choice), then count=2 may be correct and the tests need semantic
updates to match the true hardware model.

### What we still do not know

1. What is the initial R3 P-to-H state on **real** Tube ULA hardware?
   Is it count=1, count=2, empty, or something else (e.g. the "pending"
   flag is interpreted differently)?
2. Does real hardware stretch R3 P-to-H reads on empty FIFO when V=1
   but no transfer is yet active? (Our model says yes; maybe the real
   ULA says no.)
3. Could the same deadlock be broken by a different change, e.g.:
   - Making empty-R3 reads non-stretching regardless of flags (reverting
     the stretch behaviour that was added later)?
   - Making the stretch conditional on something other than "V or M
     flag is set" -- for example, "V or M flag set AND a transfer is
     currently active (control register was recently written with a
     type bit pattern)"?
   - Changing how `pending` is interpreted so that one dummy byte
     satisfies the 2-byte flush when `pending` is true?

### Alternative hypotheses worth considering

Before concluding that count=2 is the correct fix, we should
investigate:

A. **The stretch behaviour itself may be wrong.** The comment in
   `TubeUla::host_read` case 5 hints that dummy reads were meant to
   be non-stretching ("dummy BIT reads that drain stale data during
   setup"), but the stretch enablement condition `(M or V set)` is
   too broad and catches the setup flush. Perhaps the original
   intent was to stretch only during a truly-active transfer, and
   the V/M gate is an approximation that was correct in some cases
   but wrong here.

B. **`pending` may need different semantics.** Currently `pending`
   is true when `count >= threshold`. A more literal hardware model
   might have `pending` as an independent latch that can be true
   while `count = 0`, causing the stretch to be skipped for the
   first few reads after reset. (This is speculative.)

C. **The initial state could be `count = 0` with `pending = true`**
   -- "nothing in the FIFO but we're signalling that data is
   pending" -- reflecting the ULA's power-on condition before any
   transfer has configured it. Reads from this state would be
   non-stretching if we interpret the stretch condition as requiring
   both V/M *and* an active transfer.

D. **We may need to differentiate between post-hard-reset state and
   post-`set V bit` state.** The host only sets V immediately before
   the flush, and clears it afterwards (probably). If V is cleared
   again before the "real" transfer begins, maybe the stretch logic
   should respond to the V *transition*, not its level.

### Recommended next steps

1. Read each failing unit test carefully and record what invariant
   it was asserting -- independently of the new behaviour. Don't
   change any test until we understand.
2. Look at BeebEm's R3 initial-state code and host-read stretch
   logic to compare. BeebEm is known to work; if BeebEm initialises
   with count=2, our fix is probably correct and the tests were
   wrong. If BeebEm initialises with count=0 or count=1, we need a
   different fix.
3. Check the B2 emulator similarly.
4. Consult Acorn Tube ULA documentation (datasheet, service
   manual, schematic notes) for the power-on reset state of R3 if
   available.
5. Only then decide: keep the count=2 fix and update the tests
   (with clear explanations of what the new invariants mean), or
   revert and try one of the alternative hypotheses above.

---

## Cross-Emulator Investigation Results

To resolve the uncertainty about whether `count=2` was the correct fix
or was compensating for some other bug, we investigated the R3 P-to-H
implementation in four reference emulators, all of which are known to
correctly run L3FS/ANFS with a Tube co-processor.

### Findings

| Emulator | R3 P->H init count | Host stretch on empty R3 read? | Empty read returns |
|----------|---------------------|--------------------------------|---------------------|
| **B2**      | 1 (dummy, `last_p2h_value`) | **No** | dummy / last bus value |
| **BeebEm**  | 1 (comment: *"To prevent NMI on reset"*) | **No** | stale buffer value |
| **jsbeeb**  | 1 (comment: *"one valid but insignificant byte...to prevent an immediate PNMI state after PRST"*) | **No** | stale buffer value |
| **B-Em**    | 1 (`ph3pos = 1`) | **No** | `phl` latch (last parasite write) |
| **Beebium** (pre-fix) | 1 | **YES, when M or V set** | 0 |
| **Beebium** (count=2 candidate fix) | 2 | **YES, when M or V set** | 0 |

### Conclusion

Four independently-developed reference emulators unanimously:

1. Initialise R3 P->H with **exactly one** dummy byte, specifically
   to prevent a spurious PNMI on reset (jsbeeb and BeebEm both document
   this rationale explicitly in code comments).
2. Implement **no stretching** for host R3 P->H reads. When the FIFO
   is empty, they return whatever stale/latched value is around
   (last parasite write, last bus value, or zero) and let the host
   CPU continue immediately.

**The bug in Beebium is the stretch logic itself, not the dummy-byte
count.** Our `count=2` candidate fix papers over the deadlock by
ensuring the host's 2-byte flush never encounters an empty FIFO.
But the deeper problem -- that Beebium stretches a read which on
real hardware doesn't stretch -- remains.

### The correct fix

Revert `TubeUla::soft_reset` to `count=1` and remove the stretch
branch from `TubeUla::host_read` case 5. Specifically:

```cpp
// Current (buggy):
case 5: {
    uint8_t count = r3_p2h_.count;
    if (count == 0 && (control_flags_ & (FLAG_M | FLAG_V))) {
        host_stretched_ = true;
        pending_offset_ = offset & 7;
        pending_is_read_ = true;
        update_interrupts();
        return 0;
    }
    if (count > 0) {
        // ... read from FIFO ...
    }
    break;
}

// Proposed:
case 5: {
    // Matches hardware (and B2, BeebEm, jsbeeb, B-Em): an empty
    // R3 P-to-H read returns stale/latched data without stretching
    // the host. Software coordinates timing via NMI-driven
    // handshaking, not bus waits.
    if (r3_p2h_.count > 0) {
        uint8_t head = r3_p2h_.head;
        result = r3_p2h_.data[head];
        r3_p2h_.head = head ^ 1;
        --r3_p2h_.count;
        if (r3_p2h_.count == 0)
            r3_p2h_.pending = false;
        // ... trace, counters ...
    }
    // else: result stays at its initial value (0 in this function,
    // but consider using last_bus_value or the last parasite write
    // to better match hardware)
    break;
}
```

Also need to update `TubeUla::try_complete_stretch` to not expect
read stretches on R3 any more (the `pending_is_read_` + `pending_offset_ == 5`
path becomes unreachable via R3 but may still be used by other
read-stretching registers if any exist).

### Why Beebium had this stretch

Inferring from the code comment (*"Bus stretch when empty and a
transfer mode is active (M or V flag set). M enables NMI-driven
transfers; V enables two-byte (paired) transfers."*), the stretch was
presumably added as a protective measure for active transfers --
thinking that if the host reads ahead of the parasite, it should wait
for data. But real hardware doesn't do this. Software coordinates via
NMI handshaking; the ADLC or equivalent protocol timing ensures the
host only reads R3 when data is ready. The stretch is well-intentioned
but unnecessary, and it changes the bus semantics from how real Tube
ULAs behave.

### Test impact

- The 8 failing unit tests encode the `count=1` invariant that
  matches real hardware. Reverting to `count=1` will make them pass
  again.
- One test update (test_parasite_cpu.cpp "ParasiteCpu PNMI: P-to-H
  space triggers NMI", made in commit aaa04bc) should be reverted
  since the original one-dummy-byte assumption was correct.
- The integration test should continue to pass after the fix:
  instead of the flush reads returning valid dummy bytes from the
  (enlarged) FIFO, they will return stale data without stretching,
  and the host will proceed to write the sync byte.

### Related: writes to full R3 H->P

This investigation did not examine the *write* stretch (when the
host writes to a full R3 H-to-P register). That logic may or may not
also need review. Worth a follow-up comparison with B2/BeebEm/etc.
if we find write-side issues later.

---

## Resolution Summary (2026-04-14)

### The final fix

Two lines of code in `src/core/src/TubeUla.cpp`:

1. **Removed the stretch branch** in `host_read` case 5. An empty R3
   P-to-H read now returns the default result (0) without stretching.
2. **Reverted R3 P-to-H init** to one dummy byte (`count=1`,
   `tail=1`, `pending=true`) — the original correct value, matching
   real hardware and all four reference emulators.

Everything else (the `completed_read_result_` machinery, the
`tick_handshake_only` helper, the `try_complete_stretch` read branch,
the diagnostic instrumentation) was left in place. None of it is
actively used by the fixed code path, but none of it hurts either, and
the diagnostic instrumentation may be useful for the next Tube/Econet
investigation.

### Key lessons

1. **Cross-emulator comparison is definitive.** When four
   independently-developed reference implementations unanimously do
   the same thing, that's probably how the hardware works. Our
   stretch branch existed only in Beebium; that alone should have
   been a strong signal that something was wrong with it.

2. **Unit test failures are evidence, not a nuisance.** When the
   `count=2` "fix" made 8 unit tests fail, the tempting reaction was
   to update the tests to match. That would have entrenched the
   wrong model. Instead, pausing to ask "do these tests encode a
   correct invariant?" led to the cross-emulator investigation that
   identified the real bug.

3. **Diagnostic instrumentation pays off.** The `STRETCH-INFO+N`
   logging that dumped the Tube register (offset 5) and parasite PC
   (`0xFDBE`–`0xFDC1`) at intervals after frame 4 was what crystallised
   the deadlock hypothesis. Without it, we were guessing about lost
   NMI edges for a long time.

4. **Subagent disassembly of ROM code is enormously useful.** The
   ANFS agent's identification of `tube_transfer_setup` at `&BF39`
   and its exact 7-write R4 sequence, together with the 6502 Tube
   Client agent's analysis of `data_transfer_setup` at `&FD65` and
   `transfer_wait_sync` at `&FDBE`, gave us the precise protocol the
   host and parasite are speaking. Investigating this by running the
   emulator and staring at ticks would have taken days.

5. **Commit failed hypotheses.** The count=2 WIP commit (aaa04bc) is
   deliberately retained in the history. It documents a blind alley
   we went down and why it was wrong. Later investigators reading
   this branch can follow the logic and understand why we didn't
   keep `count=2`, rather than wondering whether to try it again.

### Follow-up work (optional, not blocking)

- Prune the diagnostic `fprintf` calls in `Machine.hpp` once we're
  sure no further L3FS edge cases need them.
- Consider removing the now-unreachable `completed_read_result_` /
  `pending_is_read_` infrastructure in `TubeUla` and related code,
  since nothing calls it any more.
- Investigate whether the R3 H-to-P *write* stretch (still present
  in `host_write` case 5) matches real hardware. B-Em returns the
  latch on full writes without stretching; we probably do something
  different. Not a problem today but could cause issues for other
  transfer types.
- Also test `OSWORD 72` and other disc operations that use the same
  Tube Transfer mechanism, to confirm the fix holds across all
  transfer types.
