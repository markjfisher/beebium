# Emulation Pacing

## Overview

Beebium paces emulation to run at real-time speed: a 2 MHz BBC Micro
executes 2 million crystal oscillator ticks per wall-clock second. On
a modern host CPU (~3 GHz), each emulated tick takes ~1ns of host time.
The remaining ~499ns per tick is spent sleeping. How this sleeping is
distributed is the pacing problem.

When a Tube coprocessor is attached, two independent processes (host and
parasite) must both run at their correct crystal frequencies (2 MHz and
3 MHz respectively), exchanging data through shared memory Tube registers
with sub-millisecond latency.

## Design: Adaptive Sleep Quantum with Deficit Controller

### The loop

Every iteration: run N cycles, then do one minimum-length OS sleep.
N varies smoothly, controlled by a deficit calculation. The sleep
duration is fixed at the platform's measured quantum.

```cpp
clock.start();
while (running) {
    clock.wait_for_tick();                    // one quantum sleep
    uint64_t n = clock.cycles_for_next_tick(); // deficit controller
    machine.run(n);                           // execute n crystal ticks
    clock.report_cycles(machine.cycle_count()); // feed back
}
clock.stop();
```

Progress is perfectly smooth: every iteration is identical in structure.
Only N varies, and it changes gradually.

### Adaptive sleep quantum

The minimum reliable sleep varies by platform and hardware. It is
measured empirically at emulator startup by firing 100 minimum-length
`nanosleep` calls and taking the median actual duration.

Typical measured values:
- macOS Apple Silicon: ~100us
- Linux with high-res timers: ~50-200us
- Windows with timeBeginPeriod: ~1-2ms

The deficit controller adapts to whatever quantum it gets. With a 100us
quantum on macOS, the nominal cycles per tick are 200 (at 2 MHz). With
a 1ms quantum on Windows, nominal is 2,000. The average rate converges
to the target regardless of tick granularity.

### Deficit controller

The controller computes how many cycles are "owed" at the current
wall-clock time:

```
target = wall_elapsed × target_clock_hz
deficit = target - actual_cycles
N = clamp(deficit, 0, max_cycles)
```

No PI gains, no integral term, no anti-windup. The deficit IS the
control signal. When the OS sleep overshoots (e.g., 130us instead
of 100us), the next tick's deficit is larger, so more cycles run.
The average rate self-corrects on every tick.

`max_cycles` is set to 3× nominal. This allows absorbing up to 200us
of sleep overshoot per tick while keeping individual batches small
enough to be imperceptible (<300us of BBC time per batch).

### Speed control

The deficit naturally supports variable speed by scaling the target:

| Speed | Effective target | N per quantum (100us) |
|-------|------------------|-----------------------|
| 1×    | 2 MHz            | ~200                  |
| 2×    | 4 MHz            | ~400                  |
| 0.5×  | 1 MHz            | ~100                  |
| Max   | unlimited        | max_cycles            |

Speed changes are instant and smooth -- just change the multiplier.

### Clock stretching

The BBC Micro's crystal oscillator frequency is constant. During clock
stretching (1 MHz device access), the CPU halts but the crystal keeps
ticking. Our `cycle_count` counts crystal ticks including stretch
cycles, which is the correct pacing reference. Clock stretching reduces
instruction throughput but not the crystal tick rate. No special
handling is needed -- the deficit controller paces to the crystal
frequency automatically.

### I/O pending (optional Tube optimisation)

When a Tube coprocessor writes to a shared register, it sets an
`io_pending` flag in shared memory. The sleeping timer thread checks
this flag during its interruptible sleep loop (polling at ~100us
intervals) and breaks out early when I/O arrives. This reduces
worst-case Tube latency from one full quantum to ~100us.

Without the flag (no Tube attached), the timer uses a single efficient
`sleep_for` call with no polling overhead.

Note: with the measured quantum already at ~100us on macOS, the
interruptible sleep provides at most ~100us of latency improvement
over a simple non-interruptible `sleep_for`. The mechanism could be
removed entirely with negligible impact on Tube throughput at this
quantum size. It is retained because it may be more significant on
platforms with larger quanta (e.g., Windows at ~1-2ms), and the
additional code complexity is minimal.

## Architecture

### Components

| Component | File | Purpose |
|-----------|------|---------|
| SleepQuantum | `SleepQuantum.hpp` | Measure platform sleep quantum at startup |
| PacingController | `PacingController.hpp` | Deficit computation (pure, testable) |
| PacingClock | `PacingClock.hpp` | Timer thread + emulation thread coordination |
| PacingConfig | `PacingConfig.hpp` | Target clock rate, speed multiplier |

### Thread model

Each processor (host and parasite) has its own PacingClock:

- **Timer thread**: sleeps for one quantum, signals `tick_ready` via
  condition variable. Trivial loop with no control logic.
- **Emulation thread**: waits for tick, computes deficit, runs cycles,
  reports. All control logic is here.

### Tube shared memory

Both processors share `TubeShared` (via `shm_open`/`mmap`) containing:
- Tube register latches/FIFOs (atomic, acquire/release)
- `io_pending_host` / `io_pending_parasite` flags
- Transfer counters for diagnostics

### gRPC monitoring

`SystemService.GetPacingStats` and `WatchPacingStats` expose:
- `ticks_executed`, `ticks_io_woken`
- `controller_deficit` (current deficit in cycles)

Available from both host and parasite servers.

## Performance

### Measured results (macOS Apple Silicon, 100us quantum)

| Metric | Before fix | After fix | Real hardware |
|--------|-----------|-----------|---------------|
| OSBYTE throughput | 11/s | 1280-1347/s | ~1100/s |
| OSBYTE latency | 50-60ms | 0.74-0.78ms | ~0.9ms |
| L3FS clock update | 3-5 min | ~36s | ~30s |
| Clock rate | 2.000 MHz | 2.000 MHz | 2.000 MHz |
| Deficit (steady) | N/A | ~260 cycles | N/A |
| CPU usage (idle) | ~17% | ~19% | N/A |

### Why the L3FS clock matters

The Acorn Level 3 File Server v1.26 provided the original motivation
for this work. The L3FS runs on a 65C02 Tube coprocessor and polls the
SAF3019P Real Time Clock dongle on the User Port approximately every
30 seconds. Each poll involves ~34,000 OSBYTE 51 calls through the
Tube (85 iterations of a 400-call polling loop). With the original
independent 200 Hz pacing, each OSBYTE took 50-60ms (due to tick
boundary stalls), causing the clock to update only every 3-5 minutes.

The L3FS exercised every aspect of the pacing problem simultaneously:
sustained Tube I/O, cross-process synchronisation, timing-sensitive
polling loops, and real-time clock accuracy. Fixing the pacing for
the L3FS fixed it for all Tube software.

## Design Evolution

The current design emerged from extensive experimentation. Earlier
approaches and their limitations are documented in
`docs/discussion/pacing-approaches-evaluation.md`. Key lessons:

1. **Varying sleep duration fails** because I/O interruption undermines
   the controller. Sleep and I/O latency are the same variable.
2. **Varying cycle count works** because it separates I/O latency
   (sleep, always interruptible) from rate control (cycles).
3. **PI controllers on sleep duration** suffered integral windup and
   couldn't be tuned for both burst and recovery conditions.
4. **The deficit controller is simpler and more robust** -- no gains
   to tune, no integral to manage, no anti-windup needed.
5. **Short, fixed-duration sleeps** give naturally low I/O latency
   without needing special wakeup mechanisms.
6. **Crystal frequency is the correct pacing reference** -- cycle_count
   includes stretch cycles, which is what the real crystal produces.

## Possible Enhancements

### Coordinated host-parasite ticking

The host could publish its cycle count to TubeShared after each tick.
The parasite would read it and track the host's progress, keeping
both processors in phase. This would further reduce Tube handshake
latency by ensuring both processors are awake simultaneously.

### Higher-precision sleep on Linux

Linux supports `timerfd_create` with nanosecond precision. Using
`timerfd` instead of `nanosleep` could reduce the quantum to ~10us
on Linux, giving even smoother progress and lower I/O latency.

### Shorter sleep quantum on Windows

The current implementation uses `std::this_thread::sleep_for` which on
Windows resolves to the system timer, defaulting to ~15.6ms (64 Hz). The
`measure_sleep_quantum` function detects this, giving nominal cycles of
~31,000 per tick at 2 MHz -- large batches that increase latency.

Several approaches could reduce the Windows quantum to sub-millisecond:

1. **`NtSetTimerResolution` (undocumented ntdll)**
   Sets the system timer resolution down to 0.5ms. Used by many games
   and emulators. Call once at startup:
   ```cpp
   ULONG actualResolution;
   ZwSetTimerResolution(1, true, &actualResolution); // 100ns units
   ```
   After this, `sleep_for` granularity drops to ~0.5-1ms. No busy-wait
   needed. System-wide side effect (increases power consumption).

2. **`NtDelayExecution` (undocumented ntdll)**
   Direct kernel sleep with 100ns resolution units. Combined with
   `ZwSetTimerResolution`, achieves ~0.5ms actual resolution:
   ```cpp
   LARGE_INTEGER interval;
   interval.QuadPart = -1 * (int)(milliseconds * 10000.0f);
   NtDelayExecution(false, &interval);
   ```

3. **`CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`**
   Available since Windows 10 1803 (April 2018). Documented, supported
   API for high-resolution waitable timers. Does not require setting
   global timer resolution.

4. **Hybrid sleep + busy-wait (from SO answer 77941601)**
   Use `NtSetTimerResolution` + `sleep_for` for the bulk of the wait,
   then `QueryPerformanceCounter` spin-wait for the final ~200us. Claims
   sub-microsecond precision. Trades CPU for accuracy in the tail.

5. **`timeBeginPeriod` / `timeEndPeriod` (documented multimedia API)**
   Sets minimum timer resolution to 1ms. Simpler than `NtSetTimerResolution`
   but limited to 1ms floor. System-wide effect.

For Beebium, approach (3) is preferred if targeting Windows 10 1803+,
as it is documented and per-timer rather than system-wide. Approach (1)
is the fallback for older systems. The hybrid approach (4) could be
layered on top if sub-millisecond precision is needed.

Reference: https://stackoverflow.com/q/85122 (collected approaches)

### External I/O wakeup

For Econet (UDP packets arriving from other machines), the io_pending
mechanism could be extended: a background thread polls the UDP socket
and sets `io_pending_host` when data arrives, waking the timer thread
from sleep. This would reduce Econet receive latency from one quantum
to ~100us.
