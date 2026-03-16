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

### Interim hacks to be replaced

The following are functional workarounds committed during the CE2023
investigation. They should be replaced by proper implementations based
on the requirements in this document:

- **`bus_stretch_cancel` flag in `TubeShared`**: Set by
  `Machine::pause()` via callback, cleared by `resume()` and
  `prepare_for_step()`. Works but bolted on via callbacks rather than
  being integral to the Tube port design. See Section 6.2.

- **`prepare_for_step()` on Machine and ParasiteRunner**: Clears the
  bus stretch cancel flag before stepping RPCs. Should not be needed
  once bus stretch cancellation is properly integrated.

- **Coupled mode in `run_until_or_timeout`**: Connects to the parasite,
  runs both, polls predicates without stopping. Works but the API is
  messy (the `coupled` flag, auto-discovery of the counterpart inside
  the method). Should be replaced by the `CoupledSystem` abstraction
  (Section 3.2).

- **Startup retry loop in TypeScript `runUntilOrTimeout`**: Retries
  `run()` up to 50 times to handle the `WaitMode::Api` race where
  the machine appears running before `handle_wait_mode` pauses it.
  A proper fix would be for the server to not report ready until the
  wait mode has been applied.

- **`waitForStop()` transition semantics**: Skips the initial state and
  waits for a running-to-stopped transition. This works around the
  event coalescing problem (Section 4.3) but is fragile. A proper
  event queue would make the semantics cleaner.

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
  a small set of addresses using a linear scan is acceptable.
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

Each breakpoint entry contains:

| Field | Type | Description |
|-------|------|-------------|
| `id` | `uint32_t` | Unique identifier for add/remove |
| `address` | `uint16_t` | PC address to match |
| `stop_counterpart` | `bool` | Signal the other processor to stop (Section 1.9) |
| `condition` | compiled expression | Optional, evaluated only on address match (Section 1.7) |
| `hit_count` | `uint64_t` | Counter, incremented on match, available as `hits` in condition |

The hot-path linear scan only examines `address`. All other fields
are consulted only when the address matches (the rare case).

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

Watchpoints monitor bus accesses to address ranges. They are the
single, general-purpose mechanism for both debugging (stop on access)
and recording (collect accesses during execution).

Requirements:
- **Add / Remove / List / Clear**: Manage a set of address-range
  watchpoints, each specifying a half-open address range `[start, end)`
  and an access type (read, write, or both).
- A single-address watch is `[addr, addr+1)`. A range watch like
  `[$FE00, $FF00)` covers all hardware I/O registers.
- The watchpoint check fires on every bus access (every cycle), so it
  must be extremely cheap. A linear scan of a vector of half-open
  ranges sorted by start address is a handful of comparisons --
  acceptable at any clock rate. No function-call indirection, no mutex,
  no `std::function` invocation per cycle on the hot path.
- As with breakpoints, the watchpoint list is modified only while the
  machine is stopped, so no synchronisation is needed on the read path.

Each watchpoint entry contains:

| Field | Type | Description |
|-------|------|-------------|
| `id` | `uint32_t` | Unique identifier for add/remove |
| `start` | `uint16_t` | Start of address range (inclusive) |
| `end` | `uint16_t` | End of address range (exclusive) |
| `type` | read/write/both | Which bus accesses to match |
| `stop_counterpart` | `bool` | Signal the other processor to stop (Section 1.9) |
| `condition` | compiled expression | Optional, evaluated only on range match (Section 1.7) |
| `hit_count` | `uint64_t` | Counter, incremented on match, available as `hits` in condition |

The hot-path linear scan examines `start`, `end`, and `type`. All
other fields are consulted only when the range and type match.

#### Execution model

When a watchpoint's address range and access type match:

1. The **hit callback** fires (always). This is a C++ in-process
   callback set by whoever owns the Machine. The gRPC service sets it
   to emit an event on the execution state stream. C++ test code sets
   it to record the access in a vector. The callback receives the
   watchpoint entry, address, value, and access direction.

2. The **condition** is evaluated (if present). An absent or empty
   condition defaults to `true`. A condition of `false` (or `0`) means
   the watchpoint never stops -- it records only.

3. If the condition is true, the **hit count** is checked (if present).

4. If hit count is satisfied (or absent), the machine **stops** and a
   notification is delivered via the event stream (Section 1.4),
   including the address, value, access type, and cycle count.

This unified model replaces two former mechanisms:

- **Debugger watchpoints** (gRPC): condition defaults to `true`, so the
  machine stops on match. The hit callback emits the stream event.

- **Recording watchpoints** (C++ tests): condition is `false`, so the
  machine never stops. The hit callback records the access. This
  replaces the former `BusTraceCallback` / `MemoryHistogram` mechanisms
  which fired a callback on every bus access regardless of address. The
  watchpoint vector scan is cheaper: it only fires the callback for
  matching addresses, and costs nothing when the vector is empty.

There is no separate "tracing" or "bus trace" API. Recording during
execution is a watchpoint with condition `false`.

### 1.7 Conditional Expressions

Breakpoints and watchpoints can have an optional condition expression.
The condition is evaluated only when the address match fires (the rare
case), so it does not need to be as fast as the address check itself.

The expression language is a small C-like subset, passed as a string
from any client (Python, TypeScript, future GUI), parsed once on the
server, compiled to bytecode, and evaluated by a small stack VM.

Grammar:

```
expr   := or
or     := and ('||' and)*
and    := cmp ('&&' cmp)*
cmp    := bitop (('==' '!=' '<' '>' '<=' '>=') bitop)?
bitop  := sum (('&' '|' '^') sum)*
sum    := term (('+' '-') term)*
term   := factor (('*' '/' '%') factor)*
factor := number | register | 'mem[' expr ']' | '(' expr ')' | '!' factor
```

Available identifiers:
- Registers: `A`, `X`, `Y`, `SP`, `PC`, `P`
- Status flags: `C`, `Z`, `I`, `D`, `V`, `N` (individual bits of P)
- Cycle counter: `cycles`
- Hit counter: `hits` (number of times this breakpoint/watchpoint has matched)
- Memory dereference: `mem[expr]` (peek semantics, no side effects)
- Boolean: `true`, `false`

Number literals: decimal, `0x` hex, `0b` binary.

Examples:

```
A == 0x42 && mem[0x4000] == 0xFF
PC == 0x8000 && cycles > 1000000
mem[0xFE00] & 0x80
X >= 5 && Y < 10
```

The condition string is sent with the `AddBreakpoint` or `AddWatchpoint`
RPC. The server parses it once and stores the compiled bytecode with
the breakpoint/watchpoint entry. Evaluation is a tight loop over a
small bytecode array -- nanoseconds per evaluation.

If the condition string is empty or absent, the breakpoint/watchpoint
is unconditional (stops on every address match). A condition of `false`
(or `0`) means the watchpoint never stops -- the hit callback still
fires, but the machine continues. This is how recording watchpoints
work: the callback accumulates data while execution proceeds
uninterrupted.

### 1.8 Hit Counts

Hit counts are handled by the `hits` pseudo-variable in the condition
expression, not by separate machinery. The `hits` counter increments
on every address match (before the condition is evaluated), giving the
expression access to how many times this breakpoint/watchpoint has
been reached.

Examples:
- **Exact**: `hits == 5` -- fire on the 5th match only.
- **Multiple**: `hits % 10 == 0` -- fire every 10th match.
- **Greater**: `hits > 3` -- fire on every match after the 3rd.
- **Combined**: `A == 0 && hits == 5` -- stop the 5th time A is zero
  at this address.

## 5. Non-Requirements

- **Reverse debugging / time travel**: Not required.
- **Multi-parasite**: Only one parasite is supported at a time.
- **Cross-processor breakpoints with arbitrary conditions**: "Stop the
  parasite when the host writes to address X" is not required as an
  explicit feature. The cross-processor stop signal (see 1.9) combined
  with a host-side watchpoint achieves this implicitly.

### 1.9 Cross-Processor Stop Signal

When a breakpoint or watchpoint fires on one processor, it can
optionally signal the other processor to stop too, via an atomic
flag in shared memory (`TubeShared`).

The other processor checks this flag in its tick loop (alongside
its own watchpoint checks -- one extra atomic load per cycle, same
cost as a single watchpoint range check). Both processors stop
within one cycle of each other.

This provides coupled stop as a natural consequence of the
breakpoint/watchpoint mechanism. Each breakpoint and watchpoint
has a `stop_counterpart` flag (default true for coupled debugging,
false for single-processor debugging).

## 6. Implementation Ideas

### 6.1 Breakpoint Checking in the Tick Loop

The current design uses an instruction callback (`set_instruction_callback`)
which forces the emulation loop into a slow per-instruction path with
function-call overhead on every cycle. A better approach: fold the
breakpoint check into the fast tick loop.

```cpp
// In ParasiteRunner::run() and Machine::run()
while (cycle_count < target && !paused) {
    // Breakpoint check BEFORE step: at this point, register updates
    // from the completed instruction have been applied by the tfn
    // that set read=Opcode. Use opcode_pc (the address of the
    // instruction about to be decoded), not pc (already advanced
    // past the opcode by M6502_NextInstruction's post-increment).
    if (!breakpoint_addresses_.empty() && M6502_IsAboutToExecute(&cpu)) {
        uint16_t pc = cpu.opcode_pc.w;
        for (uint16_t addr : breakpoint_addresses_) {
            if (addr == pc) { on_breakpoint_hit_(pc); return; }
            if (addr > pc) break;  // sorted: early exit
        }
    }
    step();
}
```

**Important: `opcode_pc` not `pc`.**  The M6502 library's
`M6502_NextInstruction` does `abus = pc; pc++` (post-increment), so
when `IsAboutToExecute` is true, `pc.w` is one past the opcode.  The
`opcode_pc` field holds the actual instruction address, and is the
correct value for breakpoint matching.

The breakpoint address vector is a plain `std::vector<uint16_t>`. No
atomics, no mutex on the hot path. Updates (add/remove) happen via gRPC
RPCs which require the machine to be stopped, so there is no concurrent
writer during the tick loop. The vector's contiguous memory gives the
same cache behaviour as a fixed array.

The overhead per cycle is one branch (`IsAboutToExecute`). The overhead
per instruction is a linear scan of a typically small vector -- a
handful of comparisons. At 3 MHz this is negligible.

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
