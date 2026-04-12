# Tube Single-Threaded Resume Race

## Problem

After the single-threaded Tube migration, 4 of 22 integration tests
fail. All failures share a common symptom: the Tube present flag at
$025F gets cleared mid-session during complex multi-step sequences.
Simple operations (boot, *ADFS, *CAT, single OSWORD &72) work
correctly. Failures appear in:

- OSWORD &72 + ADFS select combined sequence
- File loading via Tube (*LOAD to parasite memory)
- WFSINIT full sequence

## Root Cause: Resume Ordering in TubeSystem

The Python integration test infrastructure uses `TubeSystem.run_until()`
to run the emulated machine in chunks. Between chunks, both processors
stop. When resuming, `TubeSystem.run()` does:

```python
def run(self) -> None:
    self._host.debugger.ensure_running()      # (1) Host starts running
    self._parasite.debugger.ensure_running()   # (2) Parasite unpaused
```

Each call is a gRPC round-trip. Between (1) and (2), there is a window
where the host Machine is running but the parasite is still paused from
the previous stop.

### What happens during the window

1. Host's `Machine::step()` calls `tube_socket.tick_parasite()`
2. `tick_parasite()` checks `parasite_->is_paused()` -- returns **true**
3. Parasite does not tick
4. Host executes alone for ~2000 cycles (gRPC round-trip latency)

### Why this causes failures

When the host is in the Tube Host Code main loop and the parasite is
paused, the host may:

- Write an R2 command and poll for a response that never comes
- Read R4 status expecting data the parasite hasn't sent
- Time out waiting for a Tube register and conclude the Tube is absent

MOS's Tube Host Code has finite polling loops. If the parasite doesn't
respond within the expected number of iterations, the host gives up and
clears the Tube present flag ($025F = $00). Once cleared, subsequent
Tube operations are redirected to host-only paths, breaking the
host-parasite protocol.

### Why simple tests pass

Simple tests like `*ADFS` only require one or two chunk boundaries.
The probability of the host being in a critical Tube polling loop at
the exact moment of a chunk boundary is low. Complex tests like OSWORD
&72 + ADFS select involve many chunk boundaries during sustained Tube
traffic, making the race much more likely.

## Why this didn't happen with dual-threaded Tube

In the old dual-threaded model, `TubeSystem.run()` called
`ensure_running()` on both processors, but each ran on its own thread.
The parasite's thread was independent -- its `resume()` unblocked its
condition variable and it continued from where it left off. The host
and parasite were always either both running or both stopped. The
resume ordering didn't matter because neither thread depended on the
other being unpaused to make progress (they communicated through
lock-free atomics).

In the single-threaded model, the host's `Machine::step()` drives the
parasite. If the parasite's `is_paused()` flag is still set when the
host resumes, the parasite is effectively dead until the flag clears.

## Fix

Reverse the resume order in `TubeSystem.run()`: unpause the parasite
**before** resuming the host.

```python
def run(self) -> None:
    self._parasite.debugger.ensure_running()   # Parasite first
    self._host.debugger.ensure_running()        # Host sees parasite ready
```

This ensures that when `Machine::step()` calls `tick_parasite()`, the
`is_paused()` check returns false and the parasite ticks normally.

The stop order doesn't need to change. When the host stops (via
breakpoint with `stop_counterpart=True`), the counterpart callback
pauses the parasite. Both are stopped before the predicate is checked.

## Alternative fixes considered

1. **Don't check `is_paused()` in `tick_parasite()`**: This would
   break debugger pause semantics -- stepping the host would also step
   the parasite, even when the parasite is supposed to be paused for
   debugging.

2. **Batch the two Run() calls into a single gRPC call**: More
   complex, requires a new RPC, and doesn't address the fundamental
   ordering issue.

3. **Add a "Tube-aware resume" that atomically resumes both**: Same
   complexity issue. The ordering fix is simpler and correct.

## Verification

After fixing the resume order, all 22 integration tests (minus the 3
pre-existing ASM failures) should pass. The fix is a one-line change
in the Python client.
