# Emulation Pacing and PI Control

## Overview

Beebium paces emulation to run at real-time speed: a 2 MHz BBC Micro
executes 2 million cycles per wall-clock second. Without pacing, modern
CPUs would execute the emulation hundreds of times faster than real-time,
breaking timing-dependent software (games, music, delay loops).

When a Tube coprocessor is attached, two independent processes (host and
parasite) must both run at their correct clock rates (2 MHz and 3 MHz
respectively), exchanging data through shared memory Tube registers with
sub-millisecond latency.

## Architecture

Each processor (host and parasite) has its own `PacingClock` instance
running a dedicated timer thread. The emulation thread runs in a loop:

```cpp
clock.start();
while (running) {
    machine.run(clock.cycles_per_tick());   // Execute one tick of cycles
    clock.report_cycles(machine.cycle_count());  // Inform the controller
    clock.wait_for_tick();                  // Block until next tick
}
clock.stop();
```

The timer thread computes sleep durations using a PI controller and
signals the emulation thread when to run the next batch.

### Configuration

| Parameter | Host (Model B) | Parasite (65C02) |
|-----------|----------------|------------------|
| Target clock | 2 MHz | 3 MHz |
| Pacing rate | 200 Hz | 200 Hz |
| Cycles per tick | 10,000 | 15,000 |
| Tick interval | 5 ms | 5 ms |

## PI Controller

### Problem

Emulation speed must match wall-clock time on average. During I/O bursts
(Tube register handshakes), ticks are skipped (no sleep) to reduce
latency. This causes the emulation to run ahead of real-time. The
accumulated time debt must be repaid progressively to maintain the
correct average clock rate, without starving the emulation or causing
visible pauses.

### Design

`PacingController` is a proportional-integral (PI) controller that
computes the sleep duration for each tick:

```
drift = total_cycles_executed - (wall_elapsed × target_clock_hz)
integral += drift
sleep_ns = base_interval + Kp × drift + Ki × integral
sleep_ns = clamp(sleep_ns, 0, 2 × base_interval)
```

- **Drift** (proportional term): how far ahead (+) or behind (−) the
  emulation is right now, in cycles. Positive drift → sleep longer.
- **Integral**: accumulated drift over time. Represents the total time
  debt. Uncapped -- debt is tracked fully and repaid progressively.
- **Output clamp**: sleep duration is limited to [0, 2× base interval].
  The max prevents stalling; zero means "run immediately" (catching up).

### Gains

Selected by automated grid search (`test_pacing_controller.cpp`):

| Gain | Value | Role |
|------|-------|------|
| Kp | 750 | Immediate response to drift (ns sleep per cycle of drift) |
| Ki | 100 | Gradual debt repayment (ns sleep per accumulated cycle of drift) |

The tuning test verifies convergence after I/O bursts, absence of
oscillation, and correct average clock rate for both 2 MHz and 3 MHz
configurations.

### Separation of Concerns

`PacingController` is a pure computation class with no threading, no
clocks, and no side effects. It takes `(wall_elapsed_ns, total_cycles,
io_pending)` and returns `sleep_ns`. This makes it unit-testable with
synthetic time sequences.

`PacingClock` owns the timer thread, condition variables, adaptive sleep
margin, and I/O pending flag. It calls `PacingController::update()` on
each tick to get the sleep duration.

## I/O Pending Flag

### Problem

When the parasite writes to a Tube register, the host may be sleeping
in its pacing clock. Without intervention, the host won't see the data
for up to 5 ms (one tick interval). A single OSBYTE call requires ~6
R2 handshakes; at 5 ms per boundary crossing, this adds 50-60 ms of
latency (measured empirically).

### Design

Each side has an `io_pending` atomic flag in `TubeShared`:

- `io_pending_host`: set by parasite when it writes R1/R2/R3/R4 data
- `io_pending_parasite`: set by host when it writes R1/R2/R3/R4 data

The PacingClock checks the flag in two places:

1. **Before sleeping**: if set, skip the sleep entirely and signal a
   tick immediately. The PI controller still tracks the skipped time
   as drift, which it repays later.
2. **During interruptible sleep**: instead of a single `sleep_until()`,
   the timer thread sleeps in 200 μs intervals, checking the flag
   between each. This limits worst-case interrupt latency to ~200 μs.

### Generalisation

The `io_pending` pointer is not Tube-specific. Any I/O source can set
the flag to request an immediate tick. Future candidates:

- Econet ADLC: set when `AunBackend::receive_frame()` finds a UDP
  packet waiting
- User Port devices with async external I/O
- Any peripheral extension with real-time input requirements

## Adaptive Sleep Margin

The timer thread uses a spin-wait phase after sleeping for precise
tick timing. The duration of this phase (the "safety margin") is
learned adaptively from observed OS sleep overshoot:

- Exponential moving average of overshoot (~0.1 alpha)
- Recent maximum with decay (0.95 factor)
- New margin = average + 0.5 × recent max
- Clamped to [100 μs, interval/2]

This minimises CPU-burning spin time while maintaining timing precision.

## Results

### Before the fix

| Metric | Value |
|--------|-------|
| OSBYTE throughput | 11/s |
| Per-call latency | 50-60 ms |
| Distribution | Alternating 5/6 centiseconds |
| L3FS clock update | Every 3-5 minutes |

### After the fix

| Metric | Value |
|--------|-------|
| OSBYTE throughput | 1219-1280/s |
| Per-call latency | 0.78-0.82 ms |
| Distribution | Almost all 0 centiseconds |
| L3FS clock update | Every ~7 seconds (wall-clock) |
| Expected (real HW) | ~1100/s, ~0.9 ms |

## Possible Enhancements

### Anti-windup clamping

The integral term is currently uncapped. During sustained I/O bursts,
it grows without bound. When the burst ends, the controller commands
maximum sleep (2× interval) for many ticks to drain the integral. If
the burst was very long, this recovery period could cause a noticeable
slowdown.

Standard anti-windup techniques:

- **Conditional integration**: only accumulate integral when the output
  is not saturated. If sleep is already clamped at 0 or 2×, the integral
  can't influence the output, so stop growing it.
- **Back-calculation**: when the output saturates, reduce the integral
  by the difference between the unclamped and clamped output.
- **Integral clamp**: limit the integral to ±N (losing full debt
  tracking but bounding recovery time).

Conditional integration is the most appropriate: it prevents windup
without losing debt tracking during normal operation.

### Pacing statistics via gRPC

The console pacing output includes clock rate, vsync Hz, skipped ticks,
safety margin, and run percentage. The PI controller adds drift and
integral. None of this is currently available via gRPC.

A streaming `WatchPacingStats` RPC on the SystemService would allow
frontends to monitor pacing health in real-time. This should be
available from both host and parasite servers. Useful for:

- GUI frontend performance displays
- Diagnostic tooling
- Detecting sustained integral growth (windup warning)
- Monitoring I/O-skipped tick rate during Tube/Econet activity

### Kernel-level wakeup

The current interruptible sleep uses 200 μs polling intervals. A
pipe-based or eventfd-based wakeup could reduce this to ~10-100 μs
(kernel wakeup latency). This would be a further refinement if the
200 μs polling proves insufficient or wastes too much CPU.

## Key Files

| File | Purpose |
|------|---------|
| `src/core/include/beebium/PacingController.hpp` | PI controller (pure computation) |
| `src/core/include/beebium/PacingClock.hpp` | Timer thread, sleep logic, I/O flag |
| `src/core/include/beebium/PacingConfig.hpp` | Clock rate, tick rate, speed multiplier |
| `src/core/include/beebium/tube/TubeShared.hpp` | io_pending flags in shared memory |
| `src/core/src/TubeHostPort.cpp` | Sets io_pending_parasite on writes |
| `src/core/src/TubeParasitePort.cpp` | Sets io_pending_host on writes |
| `src/server/include/beebium/server/ServerMain.hpp` | Host main loop, report_cycles() |
| `src/server/main_tube_65C02_3MHz.cpp` | Parasite main loop, report_cycles() |
| `tests/test_pacing_controller.cpp` | PI controller unit tests + gain tuning |
| `clients/python/tests/test_tube_osbyte_throughput.py` | End-to-end throughput test |
| `docs/discussion/l3fs-clock-update-investigation.md` | Investigation that motivated this work |
