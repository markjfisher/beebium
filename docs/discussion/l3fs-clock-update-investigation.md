# L3FS Clock Display Update Timing Investigation

## Problem Statement

The Level 3 File Server v1.26 running in Beebium with the `--acorn-rtc`
extension displays a clock (bottom-right of the MODE 7 screen) that should
update roughly every 25-30 seconds, matching the behaviour reported by a
user with real hardware and Ken Lowe's reproduction User Port RTC module.
Instead, the display updates only every 3-5 minutes, causing the shown time
to lag significantly behind wall-clock time.

### Reproduction

```
./beebium-model-b-romram \
  --sideways 9:rom:acorn-anfs_4_18.rom \
  --sideways 10:rom:acorn-adfs_1_30.rom \
  --sideways 11:rom:acorn-dfs_2_26.rom \
  --fdc acorn-1770 \
  --floppy 0:/path/to/FS3v126.ssd \
  --acorn-scsi --scsi-hdd 0:/path/to/scsi0.dat \
  --station 254 --aun-port 10254 \
  --aun-map 0.221:127.0.0.1:10221 \
  --machine-name "L3FS" \
  --acorn-rtc layout=7bit-year-in-r7 \
  --tube 65C02-3MHz --advertise
```

After boot, `*RUN FS3v126`, respond `1` (drives), `S` (start), `2`
(stations). The time display at the bottom-right should update roughly
every 30 seconds. In Beebium it updates every 3-5 minutes.


## Investigation Method

All measurements were empirical, using gRPC diagnostic APIs to observe the
running system from the outside. No code changes were needed for the core
measurements (only for the new `WatchActivity` stream used by the
integration test).

### Tools used

| Tool | Purpose |
|------|---------|
| `DebuggerControl.GetState()` | Read host and parasite cycle counts |
| `DeviceInspection.GetTubeState()` | Read Tube register transfer counters |
| `CPU.GetRegisters()` | Sample program counter for profiling |
| `Memory.ReadBytes()` | Dump memory at hotspot addresses |
| `AcornRtcService.WatchActivity()` | Stream register read/write events (new) |
| Python `time.monotonic()` | Wall-clock timestamps for rate calculations |


## Findings

### 1. CBUS protocol is correct

The SAF3019P CBUS protocol implementation was verified against the
datasheet (Fig. 4, TIME ADDRESS / TIME READ cycle) and the exact byte
sequences produced by the L3FS v1.26 code (Uade04.asm).

Key detail: the DNG10 "load pulse" routine produces **three** ORB writes
(`0x00`, `0x02`, `0x00`), not two. This is because `JSR DNG45` returns to
the instruction after the JSR, which is `DNG15` (due to code layout), which
falls through into `DNG45` again. The third write creates the CLB falling
edge with DLEN LOW that transitions the chip into read phase. This is
verified by 18 passing unit tests in `test_saf3019p.cpp` and
`test_saf3019p_v126.cpp`.

### 2. RTC counter advancement is correct

`Saf3019p::advance_counters()` uses `std::chrono::steady_clock` to track
wall-clock time. The registers always reflect the actual current time. This
was never the problem -- the RTC has the right time, but the L3FS doesn't
read it often enough.

### 3. VIA integration is correct

Reading IRB (OSBYTE 150) correctly calls `update_port_pins()` at
`Via6522.cpp:41`, which triggers `update_port_b()` on the
`AcornRtcExtension`, giving the chip a chance to return fresh data bits.
The data bit (`read_data_bit()`) correctly reflects the current position in
the shift register.

### 4. Clock rates are correct

Measured empirically over 6 x 10-second intervals:

| Processor | Expected | Measured | 
|-----------|----------|----------|
| Host      | 2.000 MHz | 2.000 MHz |
| Parasite  | 3.000 MHz | 3.000 MHz |
| Ratio     | 1.500     | 1.500     |

Both processors run at exactly the right speed. The pacing console output
confirms: `Pacing: 2.000 MHz (target 2.000 MHz, 100.0%)`.

### 5. The L3FS scheduling mechanism

The L3FS main loop (`Uade15.asm:65-98`) controls when the RTC is read:

```
CPOLXX:
    JSR PRTIM           ; Read RTC dongle, display time
    LDA #85
    STA TCOUNT          ; Reset counter

CPOLL0:
    ...                 ; Check keyboard events

COMRTS:
    LDY #1              ; ~0.36 second timeout
    JSR WAIT3            ; Poll Econet Rx (OSBYTE 51 x ~400)
    BMI DOCMND           ; Received packet -> process command

    DEC TCOUNT           ; No packet -> decrement
    BPL CPOLL0           ; Counter > 0 -> keep polling
    BMI CPOLXX           ; Counter < 0 -> read RTC again
```

With TCOUNT=85 and WAIT3 taking ~0.36 seconds on real hardware, the RTC
is polled every 85 * 0.36 = **~30 seconds**.

### 6. WAIT3 inner structure

`WAIT3` (`Uade20.asm:159`) is a triple-nested polling loop:

- Outer: `TIMER2 = Y = 1`
- Middle: `TIMER1 = WAITCL = 80` (`Uade02.asm:276`)
- Inner: `TIMER = ONEMS = 5` (`Uade02.asm:277`)

Each inner iteration calls OSBYTE 51 (poll Econet Rx) through the Tube.
Total: **~400 OSBYTE calls** per WAIT3 invocation.

On real hardware, each Tube OSBYTE takes ~0.9ms, giving
400 * 0.9ms = 360ms per WAIT3.

### 7. Tube R2 throughput is the bottleneck

Measured via `DeviceInspection.GetTubeState()` Tube transfer counters over
12 x 5-second intervals during steady-state L3FS operation:

| Metric | Measured | Expected (real HW) |
|--------|----------|---------------------|
| R2 host-to-parasite | 67 bytes/s | ~1100 bytes/s |
| R2 parasite-to-host | 202 bytes/s | ~3300 bytes/s |
| OSBYTE calls/sec | **67/s** | **~1100/s** |
| Time per OSBYTE | **14.9 ms** | **~0.9 ms** |

The throughput is **16x slower** than real hardware.

Tube R4 (the command dispatch register) showed zero transfers during
steady-state operation. All OSBYTE traffic goes through R2 (the
single-byte data channel), consistent with the parasite MOS protocol.

### 8. Parasite is spinning on Tube R2 status

PC sampling (75,068 samples over 10 seconds) revealed:

| Address | % Time | Code |
|---------|--------|------|
| $FA93 | 50.1% | `BIT $FEFA` (Tube R2 status) |
| $FA96 | 41.2% | `BPL $FA93` (loop while bit 7 clear) |
| $FA82 | 3.9% | `BIT $FEFA` (earlier R2 wait) |
| $FA85 | 3.0% | `BVC $FA82` (loop while bit 6 clear) |

The parasite spends **91% of its time** in a two-instruction spin loop at
`$FA93`-`$FA96`, waiting for the host to place a response byte in Tube R2.

Note: the parasite MOS accesses Tube registers at `$FEFA`/`$FEFB`. Since
the Tube ULA registers repeat every 8 bytes, `$FEFA & 0x07 = 0x02` = R2
status and `$FEFB & 0x07 = 0x03` = R2 data.

The full parasite OSBYTE dispatch code at `$FA77`:

```
$FA77: PHA            ; Save A (OSBYTE number)
$FA78: LDA #$04       ; Tube command type 4 = OSBYTE
$FA7A: BIT $FEFA      ; \
$FA7D: BVC $FA7A      ; / Wait R2 ready, write command type
$FA7F: STA $FEFB      ;
$FA82: BIT $FEFA      ; \
$FA85: BVC $FA82      ; / Wait R2 ready, write X parameter
$FA87: STX $FEFB      ;
$FA8A: PLA            ; Restore A
$FA8B: BIT $FEFA      ; \
$FA8E: BVC $FA8B      ; / Wait R2 ready, write A parameter
$FA90: STA $FEFB      ;
$FA93: BIT $FEFA      ; \
$FA96: BPL $FA93      ; / Wait for host response (bit 7 = data available)
$FA98: LDX $FEFB      ; Read result X from R2
$FA9B: RTS
```

### 9. Host polling loop checks R2 intermittently

The host-side Tube polling loop at `$0036`:

```
$0036: BIT $FEE0      ; Check R1 status (OSWRCH)
$0039: BPL $0041      ; Skip if no R1 data
$003B: LDA $FEE1      ; Read R1 data
$003E: JSR $FFEE      ; Process OSWRCH character
$0041: BIT $FEE2      ; Check R2 status
$0044: BPL $0036      ; Loop if no R2 data -> check R1 again
$0046: ...            ; Process R2 data (OSBYTE dispatch)
```

The host alternates between checking R1 (screen output) and R2 (OSBYTE
commands). When R2 has data, it falls through to the command dispatcher.

Host PC sampling (74,929 samples) showed the host spending 18% of time in
this polling loop and 21% in ROM code at $8E40 (likely ANFS/Econet
handling).


## Root Cause

### Independent pacing creates tick-boundary latency

Both host and parasite are paced independently at **200 Hz** (5ms per
tick):

- Host: 2 MHz / 200 Hz = 10,000 cycles per tick
- Parasite: 3 MHz / 200 Hz = 15,000 cycles per tick

Each processor runs a batch of cycles, then sleeps until its next tick.
They are **not synchronised** with each other at the tick level.

When the parasite writes a byte to Tube R2 and the host is sleeping (which
it is most of the time -- the `run` percentage is only 17%), the parasite
must wait up to **5ms** for the host to wake up, see the data, and process
it. Conversely, when the host writes the response back to R2, the parasite
may need to wait for its next tick to see it.

A single OSBYTE call requires multiple R2 handshakes:

1. Parasite writes command type → host must wake up to read it
2. Parasite writes X parameter → host must process it
3. Parasite writes A parameter → host must process it
4. Host writes result X → parasite must wake up to read it

Each handshake potentially crosses a tick boundary, adding up to 5ms of
latency. With 3-4 tick boundary crossings per OSBYTE, the measured
**14.9ms per OSBYTE** is consistent with approximately **3 tick
boundaries** per call.

### Impact on L3FS

| Metric | Real hardware | Beebium | Ratio |
|--------|---------------|---------|-------|
| OSBYTE throughput | ~1100/s | 67/s | 16x slower |
| WAIT3 duration | 0.36s | 6.0s | 17x slower |
| PRTIM interval | 30s | 510s (~8.5 min) | 17x slower |

The L3FS calls OSBYTE 51 approximately 34,000 times between each RTC read
(85 WAIT3 calls * 400 OSBYTE calls each). At 67 OSBYTE/s, this takes
**510 seconds** instead of the expected 30 seconds.


## Artefacts Created

### New gRPC API: `AcornRtcService.WatchActivity`

Streaming RPC that emits events when the BBC reads or writes SAF3019P
registers via the CBUS protocol. Analogous to watching the DATA LED on Ken
Lowe's reproduction hardware.

Files modified:
- `src/extensions/acorn-rtc/acorn_rtc.proto` -- added `WatchActivity` RPC
  and `RtcActivityEvent` message
- `src/extensions/acorn-rtc/Saf3019p.hpp` -- added `ActivityCallback` and
  `set_activity_callback()`
- `src/extensions/acorn-rtc/Saf3019p.cpp` -- fire callback on CBUS read/write
  completion (outside mutex lock)
- `src/extensions/acorn-rtc/AcornRtcService.hpp` -- implemented
  `WatchActivity()` with bounded thread-safe event queue
- `clients/python/src/beebium/_proto/acorn_rtc_pb2.py` -- generated Python
  stub
- `clients/python/src/beebium/_proto/acorn_rtc_pb2_grpc.py` -- generated
  Python stub (with fixed import path)

### Integration test

`integration_tests/l3fs-clock/tests/test_l3fs_clock_update.py`

Boots the full L3FS stack, monitors the `WatchActivity` stream for 3
minutes, groups events into RDDONG calls (6-register read sequences), and
asserts the average interval is ~30 seconds. Currently **fails as expected**,
detecting only 1 RDDONG call in 180 seconds.

Marked `@pytest.mark.slow` -- excluded from default test runs, run with
`pytest -m slow`.

### Diagnostic scripts

All in `integration_tests/l3fs-clock/tests/`:

- `measure_clock_rates.py` -- confirmed host/parasite clock rates and Tube
  transfer throughput (finding: 67 OSBYTE/s, 14.9ms each)
- `measure_parasite_activity.py` -- PC sampling showing 91% time at R2
  spin loop
- `measure_r2_per_tick.py` -- detailed R2 throughput vs pacing rate
  analysis
- `disassemble_hotspots.py` -- memory dumps at hot addresses for manual
  disassembly


## What Needs to Change

The fix must allow Tube R2 handshakes to complete within a single pacing
tick on each side, rather than requiring each side to wake up from a
pacing sleep. This is a host-parasite synchronisation problem, not a
clock rate problem.

Possible approaches (not yet evaluated):

1. **Wake the host when the parasite writes to a Tube register** -- break
   the host out of its pacing sleep when R2 data arrives, so it can
   process the OSBYTE immediately rather than waiting up to 5ms for its
   next scheduled tick.

2. **Run the host polling loop during the pacing sleep** -- allow the
   host's Tube polling loop to run even while the pacing clock is sleeping,
   so it can respond to R2 data without waiting for the next tick.

3. **Increase pacing frequency** -- higher Hz = smaller ticks = less
   latency per boundary crossing. However, this increases CPU usage and
   may affect audio/video quality.

4. **Synchronise host and parasite ticks** -- ensure both processors run
   their batches in lock-step rather than independently, so R2 handshakes
   complete within a single combined tick.


## Unrelated Observations

### IDLE_COOLDOWN in FourWayHandshake

`FourWayHandshake.hpp:57` defines `IDLE_COOLDOWN = 5000` ticks (~2.5ms)
that blocks all `receive_frame()` polling after a completed Econet
transaction. There is a TODO at line 130 acknowledging this should be
replaced with a buffered approach. This is **not** the cause of the clock
update issue (the cooldown only activates after completed transactions, and
no Econet transactions occur during idle polling), but it may affect
Econet throughput in other scenarios.

### Pacing stats not available via gRPC

The console output `Pacing: 2.000 MHz (target 2.000 MHz, 100.0%) | vsync
50.0 Hz | skipped 119 | margin 1195 us | run 17.0%` is computed in the
server main loop but not exposed via any gRPC service. This data would be
valuable for GUI frontends and diagnostic tooling, from both host and
parasite. Noted for future work.
