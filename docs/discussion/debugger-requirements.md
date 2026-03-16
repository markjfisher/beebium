# Debugger Requirements for Host-Parasite Systems

## Context

Beebium's multi-process architecture separates the host (BBC Micro) and
parasite (second processor) into independent processes communicating via
shared memory. Each process has its own gRPC debugger service. This
document specifies what the debugger API needs to support for effective
debugging of this coupled system.

The requirements are driven by real needs encountered during differential
testing of CE2023, where the decompressor running on the parasite produces
different output on Beebium than on the reference emulator (jsbeeb).

## 1. Single-Processor Debugging

These requirements apply to both host and parasite independently.

### 1.1 Execution Control

- **Run**: Resume execution at the processor's native clock rate.
- **Stop**: Halt execution promptly, even if the processor is blocked on
  a bus-stretching spin-wait or other shared-memory operation.
- **Step instruction**: Execute one complete instruction and stop.
- **Step N cycles**: Execute exactly N clock cycles and stop.
- **Run until address**: Execute until PC reaches a specified address,
  then stop. Must not degrade execution speed -- the address check must
  be as cheap as a branch prediction, not a mutex acquisition per
  instruction.

### 1.2 Run Until Address

This is the critical primitive that was missing. Polling the PC from the
client is too coarse (misses transient addresses). Setting a breakpoint
via the existing callback mechanism works but must not introduce
per-instruction overhead that slows the processor below its real-time
pacing rate.

Requirements:
- The server handles the address check internally -- no gRPC round-trip
  per instruction.
- The check must be lock-free on the hot path. A compare of PC against
  a small set of addresses (up to 16) using atomics is acceptable.
- When the address is hit, the processor stops and a notification is
  delivered to subscribed clients (see 1.4).
- If a cycle budget is specified and exhausted before the address is
  hit, the processor stops and the client is informed that the budget
  was exceeded rather than the address being reached.

### 1.3 Memory Access

- **Read / Write**: Access the processor's address space with full side
  effects (register reads, I/O triggers).
- **Peek**: Read without side effects. Must work regardless of whether
  the processor is running or stopped.
- **Region access**: Read/write named memory regions (banks, shadow RAM)
  by name rather than requiring knowledge of the current bank selection.

### 1.4 Event Streaming

Clients subscribe once and receive notifications on state transitions.
No polling.

Events:
- **Stopped**: Processor stopped. Includes the reason (breakpoint hit,
  address reached, manual stop, cycle budget exhausted, error).
- **Running**: Processor resumed.
- **Reset**: Processor was reset.

Semantics:
- The initial event on subscription reflects the current state.
- `waitForStop()` must handle the case where the processor is already
  stopped when the subscription starts. If the processor is stopped,
  `waitForStop()` waits for a running-to-stopped transition rather than
  returning the stale initial state.
- Events must not be silently coalesced. If a run and stop happen in
  rapid succession, both events must be delivered. A single atomic flag
  is insufficient -- use a sequence counter or event queue.

### 1.5 Breakpoints

- **Add / Remove / List / Clear**: Manage a set of address breakpoints.
- Breakpoint checks must be lock-free on the hot path (see 1.2).
- The breakpoint mechanism must not reduce the processor's throughput
  below its real-time pacing rate. At 3 MHz, the processor executes
  ~1.5 million instructions per second. The per-instruction overhead
  of a breakpoint check must be well under 1 microsecond.

## 2. Coupled System Debugging

These requirements apply when a host and parasite are connected via the
Tube.

### 2.1 The Fundamental Constraint

The host and parasite communicate through bus-stretching spin-waits on
shared memory. When the host writes to a Tube register, it spins until
the parasite reads. When the parasite writes, it spins until the host
reads. Neither side can make progress independently when a Tube transfer
is in flight.

This means:
- **Stopping one side blocks the other.** If the parasite is stopped by
  the debugger while the host is mid-Tube-write, the host spins forever.
- **Synchronous stepping of one side deadlocks.** `stepCycles(N)` on the
  host blocks when it hits a Tube write, because the parasite isn't
  advancing.
- **Both sides must advance together** during any operation that may
  involve Tube I/O.

### 2.2 Coupled Run

Run both host and parasite together. Both advance at their natural clock
rates. This is the normal operating mode. No special API is needed
beyond calling `run()` on both.

### 2.3 Coupled Stop

Stop both host and parasite together. The stop must be prompt and must
not deadlock on bus stretching.

Requirements:
- Stopping either processor must break the other out of any bus-stretch
  spin-wait it may be in. The spin-wait must be cancellable.
- After a coupled stop, both processors are in a consistent state: no
  half-completed Tube transfers, no lost data. (This may require
  abandoning a partially-written byte if the spin-wait was interrupted.)
- A single API call stops both. The caller should not need to stop them
  individually and hope the order is right.

### 2.4 Coupled Run Until

Run both processors until a condition is met on one of them. This is
the key primitive for differential testing.

Examples:
- Run both until the parasite PC reaches $0810.
- Run both until the host has advanced 10 million cycles.
- Run both until a predicate on host memory is satisfied (e.g. screen
  contains specific text).

Requirements:
- Both processors advance at their natural rates while the condition is
  being evaluated.
- The condition check does not stop either processor (for predicates
  based on peek, which is side-effect-free).
- When the condition is met, both processors are stopped promptly.
- For address-based conditions, the check is server-side (no gRPC
  round-trip per instruction).
- A cycle or time budget can be specified. If exhausted, both stop and
  the caller is informed.

### 2.5 Coupled Step

Step both processors together by a specified number of emulated seconds
(or host cycles and proportionally-scaled parasite cycles).

Requirements:
- Both processors advance concurrently during the step. Neither blocks
  on bus stretching because both are making progress.
- The step is synchronous from the caller's perspective: the call
  returns when both processors have completed their cycle budget.
- The caller specifies emulated time (e.g. 1 second), and the
  implementation converts to cycle counts based on each processor's
  clock speed.

### 2.6 Counterpart Discovery

The debugger should automatically discover the coupled processor.

- Given a host client, discover and connect to the parasite's debugger.
- Given a parasite client, discover and connect to the host's debugger.
- The connection is established via the existing Tube status gRPC service
  which reports the counterpart's gRPC address.

## 3. Client API Design

The client libraries (Python, TypeScript) should provide a clean API
that hides the complexity of coupled debugging.

### 3.1 Single Processor

```
debugger.run()
debugger.stop()
debugger.step(count=1)
debugger.step_cycles(count=1)
debugger.run_until(address, cycle_budget=None) -> StopEvent
debugger.wait_for_stop() -> StopEvent
```

### 3.2 Coupled System

```
# The system object manages both host and parasite as a unit.
system = CoupledSystem(host, parasite)  # or auto-discovered

system.run()           # Run both
system.stop()          # Stop both (bus-stretch-safe)
system.run_until(      # Run both until condition on either side
    predicate,
    cycle_budget=None,
) -> StopEvent
system.run_for(seconds)  # Run both for emulated time
```

The `CoupledSystem` encapsulates the constraints of Section 2. The
caller does not need to know about bus stretching, pacing asymmetry,
or stop ordering.

### 3.3 Predicates

Predicates for `run_until` can reference either processor:

```
# Stop when parasite PC reaches address
system.run_until(parasite_pc == 0x0810)

# Stop when host screen contains text
system.run_until(host_screen_contains("Initialising"))

# Stop when parasite memory at address equals value
system.run_until(parasite_peek(0x0031) == 0x00)
```

Server-side predicates (PC comparison) are evaluated without gRPC
round-trips. Client-side predicates (screen text) are evaluated
periodically via peek without stopping either processor.

## 4. Correctness Requirements

### 4.1 No Silent Data Loss

If a bus-stretch spin-wait is interrupted (e.g. by a debugger stop), the
interrupted write must be retried or reported as failed. Silently
dropping the byte corrupts the Tube protocol state.

### 4.2 Deterministic Replay

Given the same ROM images, disc images, and sequence of debugger
commands, the system must produce the same execution trace. The debugger
must not introduce non-determinism (e.g. from race conditions between
the event stream and the stop mechanism).

### 4.3 Event Ordering

Events delivered via the streaming API must reflect the actual order of
state transitions. A stop event must not be delivered before the
corresponding run event. Coalescing multiple transitions into a single
event is not acceptable.

### 1.6 Memory Watchpoints

The core already has a watchpoint mechanism (`Machine::add_watchpoint`)
that fires a callback on bus reads, writes, or both to an address range.
This is not yet exposed via gRPC.

Requirements:
- **Add / Remove / List / Clear**: Manage a set of up to 16 address-range
  watchpoints, each specifying a half-open address range `[start, end)`
  and an access type (read, write, or both).
- A single-address watch is `[addr, addr+1)`. A range watch like
  `[$FE00, $FF00)` covers all hardware I/O registers.
- When a watchpoint fires, the processor stops and a notification is
  delivered via the event stream (Section 1.4), including the address,
  value, access type (read/write), and cycle count.
- The watchpoint check fires on every bus access (every cycle), so it
  must be extremely cheap. A linear scan of 16 half-open ranges is a
  handful of comparisons -- acceptable at any clock rate. No function-
  call indirection, no mutex, no `std::function` invocation per cycle.
- As with breakpoints, the watchpoint list is modified only while the
  machine is stopped, so no synchronisation is needed on the read path.

The existing implementation uses `std::function<void(...)>` callbacks
dispatched from `CpuBinding` via a `std::vector<Watchpoint>`. This has
per-access overhead from the virtual dispatch and vector iteration. The
same approach as breakpoints should be used: a plain array checked
inline in the tick loop, with the `std::function` callback invoked only
when a watchpoint actually fires (the rare case).

## 5. Non-Requirements

- **Reverse debugging / time travel**: Not required.
- **Conditional breakpoints with expressions**: Not required. Simple
  address matching is sufficient. Complex conditions can be implemented
  client-side.
- **Multi-parasite**: Only one parasite is supported at a time.
- **Cross-processor breakpoints**: "Stop the parasite when the host
  writes to address X" is not required. The Tube register interface
  provides this implicitly.

## 6. Implementation Ideas

### 6.1 Breakpoint Checking in the Tick Loop

The current design uses an instruction callback (`set_instruction_callback`)
which forces the emulation loop into a slow per-instruction path with
function-call overhead on every cycle. A better approach: fold the
breakpoint check into the fast tick loop.

```cpp
// In ParasiteRunner::run() and Machine::run()
for (uint64_t i = 0; i < cycles; ++i) {
    cpu_.tick();
    if (M6502_IsAboutToExecute(&cpu_)) {
        uint16_t pc = cpu_.pc.w;
        // Linear scan of a small fixed-size array (max 16 entries).
        // Updated only while the machine is stopped, so no
        // synchronisation needed on the read path.
        for (uint32_t j = 0; j < breakpoint_count_; ++j) {
            if (breakpoint_addresses_[j] == pc) {
                pause();
                return;
            }
        }
    }
}
```

The breakpoint address array is a plain `std::array<uint16_t, 16>` with
a plain `uint32_t` count. No atomics, no mutex on the hot path. Updates
(add/remove) happen via gRPC RPCs which require the machine to be
stopped, so there is no concurrent writer during the tick loop.

The overhead per cycle is one branch (`IsAboutToExecute`). The overhead
per instruction is a linear scan of at most 16 entries -- a handful of
comparisons that fit in a cache line. At 3 MHz this is negligible.

This eliminates the separate "fast path" vs "instruction callback path"
distinction entirely. The tick loop is always the same; breakpoints are
just a check at instruction boundaries.

### 6.2 Bus-Stretch Cancellation

Bus-stretching spin-waits in `TubeHostPort::host_write()` currently spin
indefinitely. To support coupled stop (Section 2.3), the spin must be
cancellable.

The cancellation flag lives in `TubeShared` (shared memory) so both
processes can see it. `Machine::pause()` sets the flag via a callback;
`Machine::resume()` and the stepping RPCs clear it. The spin loops check
it alongside the register-ready flag:

```cpp
while (shared_->r1_h2p.ready.load(acquire) != 0) {
    if (shared_->bus_stretch_cancel.load(relaxed)) return;
}
```

When a cancelled write returns without writing, the partially-written
state must be handled. Options:

- **Retry on resume**: Track that a write was cancelled and retry it
  when the machine resumes. Guarantees no data loss.
- **Abandon**: Accept the lost byte. Simple but breaks the Tube
  protocol. Only acceptable if the machine is being reset.

Retry-on-resume is preferred for correctness (Section 4.1).

### 6.3 Event Queue

Replace the single `atomic<bool> execution_state_changed_` flag with a
bounded queue of events. Each state transition pushes an event with a
sequence number. The `WatchExecutionState` loop drains the queue and
sends each event individually. This guarantees no coalescing (Section
4.3).

A single-producer (emulation thread) single-consumer (gRPC stream
thread) lock-free queue is sufficient. The producer is the emulation
loop (breakpoint hit, pause, resume); the consumer is the streaming RPC
handler.

### 6.4 CoupledSystem

The `CoupledSystem` abstraction (Section 3.2) can be implemented
entirely in the client library. It holds two `Beebium` client instances
(host and parasite) and coordinates their debugger APIs.

Key implementation points:

- **`stop()`**: Sets `bus_stretch_cancel` (via one side's pause
  callback), then stops both. Order doesn't matter because the cancel
  flag breaks the other side out of any spin-wait.
- **`run()`**: Clears `bus_stretch_cancel`, then runs both.
- **`run_until(predicate, budget)`**: Runs both, polls predicate via
  peek (no stop needed), stops both when predicate fires or budget
  exhausted.
- **`run_for(seconds)`**: Computes cycle counts for each processor
  based on their clock speeds, runs both, polls host cycle count,
  stops both when budget reached.
- **Counterpart discovery**: Uses `Tube.getStatus()` to find the
  counterpart's gRPC address and connects automatically.
