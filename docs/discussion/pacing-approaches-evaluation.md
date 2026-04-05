# Pacing Approaches: What We Tried and What We Learned

## The Problem

The host (2 MHz) and parasite (3 MHz) run as separate processes with
independent 200 Hz pacing clocks. Each runs a batch of cycles, then
sleeps until the next tick. Tube R2 handshakes require multiple
round-trips between the two processors. With independent 5ms sleeps,
each round-trip stalls at tick boundaries, producing 50-60ms per OSBYTE
call (should be ~0.9ms on real hardware).

## Constraint Summary

Three requirements are in tension:

1. **Low I/O latency**: Tube R2 handshakes must complete in <1ms, not
   50ms. Requires waking the sleeping processor when data arrives.
2. **Correct clock rate**: The emulation must execute 2,000,000 host
   cycles per wall-clock second on average, so timing-dependent software
   (BASIC TIME, cursor blink, music) runs at the right speed.
3. **No visible speed variation**: The user shouldn't see the cursor
   race or crawl after I/O bursts.

## Approaches Tried

### 1. I/O-pending flag with sleep skip (Phase 1)

**Mechanism**: Add `io_pending` atomic flag to TubeShared. When one side
writes a Tube register, set the other side's flag. The PacingClock
checks the flag before sleeping: if set, skip sleep entirely and signal
a tick immediately.

**Result**: OSBYTE latency dropped from 50-60ms to ~15ms (6x improvement).
Most of the remaining latency was from the `sleep_until()` call that
couldn't be interrupted.

**Problem**: Only helped when the flag was checked before sleeping. If
already in `sleep_until()`, the flag wasn't seen until sleep expired.

### 2. Interruptible sleep (Phase 3)

**Mechanism**: Replace `sleep_until()` with a loop of short
`sleep_for(200us)` calls, checking `io_pending` between each.

**Result**: Combined with Phase 1, OSBYTE latency dropped to 0-10ms.
Most calls completed in 0 centiseconds (sub-10ms). Test passed at
1200+ OSBYTE/s.

**Problem**: The io_pending skip path bypassed the pacing entirely.
During sustained Tube I/O (e.g., L3FS polling), every tick was
io-skipped, causing the emulation to run at 11 MHz (550% of target).
The host consumed 100% CPU and timing-dependent software ran too fast.

### 3. PI controller on sleep duration

**Mechanism**: Replace fixed-interval sleep with a PI controller that
computes sleep duration based on drift (cumulative cycles vs wall-clock
target). The controller adjusts sleep to maintain the correct average
clock rate.

**Result**: In unit tests with synthetic time sequences, the controller
converged correctly. Gains were tuned via automated grid search.

**Problems**:
- The interruptible sleep undermined the controller: during I/O bursts,
  every sleep was interrupted at 200us, so the actual sleep was always
  ~200us regardless of what the controller recommended. The controller's
  output was effectively ignored.
- The integral term grew without bound during sustained I/O (integral
  windup), causing violent oscillation between 1 MHz and 4.5 MHz during
  recovery.
- Back-calculation anti-windup prevented the oscillation but caused
  permanent time loss (the integral lost track of debt during saturation).
- Conditional integration prevented windup but also lost debt tracking.

**Fundamental issue**: Sleep duration and I/O responsiveness are the
same variable. You can't independently control both. Shortening sleep
for I/O response necessarily means less time for the controller to
work with.

### 4. PWM: variable cycles per tick (deficit controller)

**Mechanism**: Fix the sleep at the base interval (5ms, always
interruptible by I/O) and vary the NUMBER OF CYCLES per tick instead.
A deficit controller computes: `cycles = target_total - actual_total`,
clamped to `[0, max_cycles]`. During shortened I/O ticks, fewer cycles
are run; during normal ticks, the base 10,000.

**Result**: Clean separation of concerns:
- Sleep is always interruptible → low I/O latency
- Deficit controller maintains correct cycle count → accurate rate
- No PI gains, no integral, no anti-windup needed

OSBYTE throughput: 500-520/s (about half real hardware, but well above
the original 11/s). Clock rate: stable 2.000 MHz.

**Problems discovered**:

**(a) Timer thread not advancing tick on I/O wake**: When I/O woke the
timer early, `next_tick` was advanced by a full interval, causing the
next sleep to be a full 5ms regardless. This meant only one I/O wakeup
per base interval instead of many. Fixed by not advancing `next_tick`
on early wakes, allowing multiple short ticks within one interval.

**(b) 200us poll interval limiting throughput**: Each R2 byte exchange
took at least 200us (one poll interval). An OSBYTE needs ~6 R2
exchanges: minimum 1.2ms per OSBYTE. Replaced with `yield()`-based
spin-wait for sub-microsecond response. Trade-off: higher CPU usage
during I/O-active periods.

**(c) Persistent -10,000 drift bias**: The deficit was always ~10,000
because `cycles_for_next_tick()` read wall-clock on the emulation
thread (after CV wakeup latency). Fixed by capturing the timestamp
in the timer thread and storing it atomically.

**(d) Catch-up racing after disc loading**: After sustained FDC activity,
the deficit controller would run `max_cycles` (150% of base) to catch
up, causing visible speed-up (cursor racing, fast scrolling). Reducing
`max_cycles_ratio` to 1.05 helped but didn't solve the root cause.

**(e) Execution time counted against deficit**: Wall-clock time spent in
`run()` was treated as "time that should have produced cycles," even
though on real hardware the CPU is also busy during that time. Attempts
to exclude execution time by advancing `start_time_` caused the system
to become slow and unresponsive (current state -- needs investigation).

## Key Insights

### The fundamental tension

Low I/O latency requires the ability to interrupt or shorten the sleep
phase. But any time taken from sleep must be accounted for to maintain
the correct clock rate. These two requirements fight each other:

- **Skip sleep for I/O** → emulation runs too fast (no rest period)
- **Don't skip sleep** → I/O latency is high (original problem)
- **Skip sleep but reduce cycles** → correct rate but throughput is
  limited by how quickly we can cycle through wake/compute/run/report

### Sleep duration vs cycle count as control variable

Varying **sleep duration** (PI controller, Approaches 1-3) is natural
but has a critical flaw: the interruptible sleep for I/O responsiveness
undermines the controller. The controller computes "sleep 4.5ms" but
the sleep exits after 200us because I/O arrived. The actual sleep is
determined by I/O timing, not the controller.

Varying **cycle count** (deficit controller, Approach 4) separates the
concerns: sleep is always fixed/interruptible (for I/O) and cycles are
adjusted (for rate). But the deficit controller needs to distinguish
between "too few cycles because I/O shortened the tick" (should catch
up) and "too few cycles because the host CPU was busy" (should not
catch up).

### What the BBC Micro actually does

On real hardware, there is no pacing problem because both processors
share the same clock crystal and the Tube ULA handles synchronisation
in hardware. The host CPU stretches when accessing 1 MHz devices but
the clock keeps ticking -- cycle count equals wall-clock time by
definition.

In our emulation, cycle count and wall-clock are independent. The
pacing system tries to keep them synchronised, but any perturbation
(I/O bursts, CPU-heavy workloads, OS scheduling) creates drift that
must be handled.

### The execution time problem

When the host is busy (disc loading, heavy NMI handling), each `run()`
call takes longer in wall-clock but the same number of emulated cycles.
The deficit controller sees the extra wall-clock time and tries to
catch up afterwards. But on real hardware, the CPU was also busy --
there's nothing to catch up for.

Excluding execution time from the deficit (Approach 4e) seems correct
in principle but caused the system to become unresponsive. The likely
reason: advancing `start_time_` by execution time effectively slows
the wall-clock reference, which makes the deficit controller think
less time has passed, which causes it to run fewer cycles. During I/O
bursts where execution time is high (spin-waiting on Tube registers),
this over-correction starves the emulation.

### CPU usage trade-offs

Spin-waiting (`yield()` loop) for I/O pending gives the lowest latency
but consumes CPU during I/O-active periods. Sleep-based polling (200us
intervals) saves CPU but limits throughput. A hybrid approach (spin
briefly after I/O, then fall back to sleeping) might give the best of
both worlds but adds complexity.

## Current State

The system uses the deficit controller (Approach 4) with:
- Interruptible sleep (yield-based spin-wait when io_pending configured)
- Multiple short ticks within one base interval for rapid Tube handshakes
- Tick timestamp captured in timer thread to avoid CV wakeup latency bias
- Execution time exclusion (which causes unresponsiveness -- needs revert
  or rethinking)

The throughput test passes at ~500 OSBYTE/s (threshold 500, real HW
~1100). The clock rate is stable at 2.000 MHz during idle. But the
execution time exclusion (latest change) has made the system
unresponsive and needs investigation.

## Open Questions

1. **How to handle execution time correctly?** We need to not compensate
   for CPU-busy periods, but the current approach of advancing
   `start_time_` over-corrects during I/O bursts. Perhaps execution time
   should only be excluded when it exceeds a threshold (indicating genuine
   CPU work, not Tube spin-waiting)?

2. **Is 500 OSBYTE/s sufficient?** Real hardware does ~1100/s. At 500/s,
   the L3FS clock updates every ~60s instead of ~30s. Getting closer to
   1100/s requires reducing the per-tick overhead (wake/compute/run/report
   cycle) which is dominated by the yield-based spin-wait.

3. **Should the deficit controller have a small catch-up allowance?**
   With `max_cycles_ratio = 1.0` (no catch-up), the test fails. With
   `1.05` (5% catch-up), it passes but causes subtle racing after heavy
   workloads. What's the right value?

4. **Could the host and parasite share a single pacing clock?** Instead
   of independent pacing, a single clock could orchestrate both, ensuring
   Tube handshakes happen within a single tick. This would be a larger
   architectural change.

5. **Is there an OS-level solution?** Using `pthread_cond_signal` across
   processes (via shared memory) or a shared pipe/eventfd for wakeup
   would give kernel-efficient sleep with instant wakeup, avoiding the
   spin-wait CPU cost entirely.
