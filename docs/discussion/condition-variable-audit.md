# Condition-Variable Lost-Wakeup Audit

Status: Proposed (2026-06-19). Triggered by a real lost-wakeup deadlock in
`HostSerialEndpoint` (fixed on `fix/host-serial-reopen-deadlock`). This document
records the rule, the failure it caused, and a checklist for sweeping the rest
of the codebase. The sweep itself is deferred.

---

## Why this exists

A flaky ~10-minute CI timeout (`test_host_serial_extension`, "HostSerial
dispatcher reports and re-points the bridge", macOS x86_64) turned out to be a
textbook condition-variable lost wakeup, not a slow runner. It failed roughly
once in thousands of runs, wasted the whole 40-minute build, and skipped every
job that depended on its artifacts.

The bug is cheap to introduce, invisible in code review unless you are looking
for it, and catastrophic when it fires (an unbounded `join()` that hangs the
process). The same shape can exist anywhere we pair a `std::condition_variable`
with a flag. This document makes the rule explicit and lists where to look.

## The rule

> State read by a `condition_variable` wait predicate must be **mutated while
> holding the same mutex** the waiter uses -- even if that state is a
> `std::atomic`.

Making the flag `std::atomic` is **not** sufficient. Atomicity fixes visibility,
not the wakeup race. The race is in the gap between the waiter evaluating its
predicate (and deciding to block) and the wait actually parking on the cv:

```
waiter (writer thread)                  notifier (teardown thread)
----------------------                  --------------------------
lock(m)
pred() -> stop_ == false
                                        stop_.store(true)   // OUTSIDE m
                                        cv.notify_all()     // reaches no one
cv.wait(m)  // releases m, blocks       // ... notify already gone
// blocks forever
```

Holding `m` across the store closes the gap: while the writer is between its
predicate check and blocking, it *holds* `m`, so the notifier cannot run its
store+notify until the writer has either observed the flag or fully parked (at
which point `notify` is guaranteed to be delivered).

## Two correct idioms

Both are valid; pick one and be consistent within a file.

**A. Store under the lock (textbook, self-documenting):**

```cpp
{
    std::lock_guard<std::mutex> lock(tx_mutex_);
    stop_.store(true, std::memory_order_release);
}
tx_cv_.notify_all();
```

**B. Store, then an empty critical-section barrier, then notify:**

```cpp
stop_.store(true, std::memory_order_release);
{ std::lock_guard<std::mutex> lock(tx_mutex_); }  // barrier: waiter is past its check
tx_cv_.notify_all();
```

Idiom B publishes the flag fractionally earlier (useful when *other* threads --
e.g. a reader polling the same flag without the cv -- benefit from seeing it
sooner), at the cost of an empty block that reads oddly without a comment. The
host-serial fix uses A; the ip232 / rfc2217 serial endpoints use B.

## Bounded vs unbounded waits

The damage depends on whether the wait can time out:

- **Untimed `wait(lock, pred)`** -- a lost wakeup hangs **forever**. These are
  the dangerous ones (the host-serial writer was one).
- **`wait_for` / `wait_until`** -- a lost wakeup only costs one timeout
  interval; the next spurious/timed wakeup re-checks the predicate and recovers.
  Still worth fixing for latency, but not a deadlock.

When auditing, prioritise untimed waits whose predicate reads a flag set by
another thread.

## What has already been checked (serial subsystem)

| Site | Verdict |
|------|---------|
| `HostSerialEndpoint` (`tx_cv_`) | **Was buggy** -- flag set outside `tx_mutex_`. Fixed (idiom A) + watchdog stress test. |
| `Ip232SerialEndpoint` (`tx_cv_`) | Safe -- idiom B. (`io_cv_` is `wait_for`, self-healing.) |
| `Rfc2217ClientEndpoint` (`tx_cv_`) | Safe -- idiom B. |
| `Rfc2217ServerEndpoint` (`tx_cv_`) | Safe -- idiom B. |
| `acorn-scsi/ScsiBusEventBuffer` (`cv_`) | Safe -- `closed_` set under `mutex_`. |

## To sweep (deferred)

Every other `std::condition_variable` user. Grep:

```
grep -rl condition_variable src/ --include=*.hpp --include=*.cpp
```

Known sites to start from (verify each: is the predicate's state mutated under
the wait's mutex? is the wait timed or untimed?):

- `src/core/include/beebium/PacingClock.hpp`
- `src/core/include/beebium/Machine.hpp`
- `src/core/src/econet/AunBackend.cpp`
- `src/core/src/TypeAheadQueue.cpp`
- `src/core/src/Sn76489.cpp`
- `src/core/src/econet/piconet/PiconetBackend.cpp`, `Events.cpp`
- `src/service/include/beebium/service/SystemService.hpp`
- `src/service/include/beebium/service/DebuggerService.hpp`

For each finding, prefer the same watchdog-stress-test approach used for
host-serial (`tests/test_host_serial_endpoint.cpp`): a loop that races teardown
against the waiter, with a timeout that fails fast instead of hanging the suite.

## Related

- `docs/discussion/grpc-windows-streaming-race.md` -- the "Piconet lesson" on
  cross-thread state in the same serial/transport family.
