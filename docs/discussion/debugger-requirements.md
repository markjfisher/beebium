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

### 1.2 Breakpoints

Breakpoints fire at instruction boundaries when the PC falls within a
half-open address range `[start, end)`. A single-address breakpoint is
`[addr, addr+1)`. A full-range breakpoint `[0x0000, 0x10000)` fires at
every instruction boundary.

This generality means that "run until address", "run for N cycles", and
"run until predicate" are all just breakpoints with appropriate ranges
and conditions:

- **Run until address**: Breakpoint `[addr, addr+1)`, no condition.
- **Run for N cycles**: Breakpoint `[0x0000, 0x10000)`, condition
  `cycles >= target`.
- **Run until predicate**: Breakpoint `[0x0000, 0x10000)`, condition
  on memory or registers, e.g. `mem[0x7C28] == 0x42`.

No special RPCs are needed for these modes. The client sets a breakpoint,
subscribes to the event stream, calls `Run`, and waits for the stop
event.

Requirements:
- **Add / Remove / List / Clear**: Manage a set of range breakpoints.
- Breakpoint checks must be lock-free on the hot path. A linear scan
  of a sorted vector of ranges is a handful of comparisons --
  acceptable at any clock rate.
- The breakpoint mechanism must not reduce the processor's throughput
  below its real-time pacing rate. At 3 MHz, a full-range breakpoint
  with a condition evaluates the condition at every instruction boundary
  (~1.5M evaluations/sec); the bytecode VM evaluates typical conditions
  in under 100 ns, giving approximately 2.5% overhead.

Each breakpoint entry contains:

| Field | Type | Description |
|-------|------|-------------|
| `id` | `uint32_t` | Unique identifier for add/remove |
| `start` | `uint16_t` | Start of PC range (inclusive) |
| `end` | `uint16_t` | End of PC range (exclusive) |
| `stop_counterpart` | `bool` | Signal the other processor to stop (Section 1.8) |
| `condition` | compiled expression | Optional, evaluated only on range match (Section 1.6) |
| `hit_count` | `uint64_t` | Counter, incremented on match, available as `hits` in condition |

The hot-path linear scan examines `start` and `end`. All other fields
are consulted only when the range matches (the rare case for
single-address breakpoints; every instruction for full-range).

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
  watchpoint hit, manual stop, counterpart stop, error).
- **Running**: Processor resumed.
- **Reset**: Processor was reset.

Semantics:
- The initial event on subscription reflects the current state.
- Events must not be silently coalesced. If a run and stop happen in
  rapid succession, both events must be delivered. The implementation
  uses a bounded SPSC event queue (not a single atomic flag).
- `waitForStop()` handles the case where the processor is already
  stopped by waiting for a running-to-stopped transition.

### 1.5 Memory Watchpoints

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
| `stop_counterpart` | `bool` | Signal the other processor to stop (Section 1.8) |
| `condition` | compiled expression | Optional, evaluated only on range match (Section 1.6) |
| `hit_count` | `uint64_t` | Counter, incremented on match, available as `hits` in condition |

The hot-path linear scan examines `start`, `end`, and `type`. All
other fields are consulted only when the range and type match.

#### Execution model

When a breakpoint or watchpoint's address range matches:

1. The **hit counter** increments.

2. The **condition** is evaluated (if present). An absent or empty
   condition defaults to `true`. A condition of `false` (or `0`) means
   the entry never stops -- it records only. The `hits` pseudo-variable
   is available in the condition expression.

3. If the condition evaluates to true, the machine **stops** and a
   notification is delivered via the event stream (Section 1.4).

4. If `stop_counterpart` is true, the other processor is signalled
   to stop (Section 1.8).

The **hit callback** (a C++ in-process mechanism, not exposed via gRPC)
fires on every address match regardless of the condition result. The
gRPC service sets the callback to enqueue an event on the stream. C++
test code sets it to record accesses in a vector. This is how recording
watchpoints work: condition `false` means the machine never stops, but
the callback still fires for each matching access.

There is no separate "tracing" or "bus trace" API. Recording during
execution is a watchpoint with condition `false`.

### 1.6 Conditional Expressions

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
cycles >= 10000000
mem[0xFE00] & 0x80
X >= 5 && Y < 10
hits == 5
hits % 10 == 0 && A == 0
```

The condition string is sent with the `AddBreakpoint` or `AddWatchpoint`
RPC. The server parses it once and stores the compiled bytecode with
the breakpoint/watchpoint entry. Evaluation is a tight loop over a
small bytecode array -- under 100 ns for typical expressions in a
debug build.

If the condition string is empty or absent, the breakpoint/watchpoint
is unconditional (stops on every address match). A condition of `false`
(or `0`) means the entry never stops -- the hit callback still fires,
but the machine continues. This is how recording watchpoints work.

### 1.7 Hit Counts

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

### 1.8 Cross-Processor Stop Signal

When a breakpoint or watchpoint fires on one processor, it can
optionally signal the other processor to stop too, via an atomic
flag in shared memory (`TubeShared::debugger_stop_signal`).

The other processor checks this flag in its tick loop (alongside
its own breakpoint/watchpoint checks -- one extra atomic load per
cycle). Both processors stop within one cycle of each other.

This provides coupled stop as a natural consequence of the
breakpoint/watchpoint mechanism. Each breakpoint and watchpoint
has a `stop_counterpart` flag (default true for coupled debugging,
false for single-processor debugging).

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
rates. No special API is needed beyond calling `run()` on both.

### 2.3 Coupled Stop

Stop both host and parasite together. Stopping either side sets the
`bus_stretch_cancel` flag in `TubeShared`, breaking the other out of
any spin-wait. After a coupled stop, both processors are in a
consistent state.

### 2.4 Coupled Run Until

Run both processors until a condition is met on one of them. This is
the key primitive for differential testing.

All "run until" modes are implemented through breakpoints with
appropriate address ranges and conditions:

- **Run until PC reaches $0810**: Set a breakpoint `[0x0810, 0x0811)`
  with `stop_counterpart=true` on the target processor. Run both.
  The breakpoint stops both processors via the cross-processor signal.

- **Run for 10 million cycles**: Set a full-range breakpoint
  `[0x0000, 0x10000)` with condition `cycles >= 10000000` and
  `stop_counterpart=true` on the host. Run both.

- **Run until screen contains text**: This is a client-side predicate
  that cannot be evaluated server-side. The client periodically peeks
  screen memory (side-effect-free, no stop needed) while both
  processors run. When the predicate is satisfied, the client stops
  both.

No polling of execution state is needed for server-side conditions.
The client subscribes to the event stream, sets the breakpoint, calls
`Run` on both, and waits for the stop event.

### 2.5 Counterpart Discovery

The debugger automatically discovers the coupled processor.

- Given a host client, discover and connect to the parasite's debugger.
- Given a parasite client, discover and connect to the host's debugger.
- The connection is established via the existing Tube status gRPC service
  which reports the counterpart's gRPC address.

## 3. Client API Design

The client libraries (Python, TypeScript) provide a clean API that hides
the complexity of coupled debugging.

### 3.1 Single Processor

```python
debugger.run()
debugger.stop()
debugger.step(count=1)
debugger.step_cycles(count=1)

# Breakpoints (address ranges with optional conditions)
bp_id = debugger.add_breakpoint(0xC000)
bp_id = debugger.add_breakpoint(0xC000, condition="A == 0x42")
bp_id = debugger.add_breakpoint(0xC000, condition="hits == 5")

# Watchpoints (address ranges with optional conditions)
wp_id = debugger.add_watchpoint(0xFE00, 0xFF00, type="write")
wp_id = debugger.add_watchpoint(0x0500, 0x0501, condition="A == 0x42")

# Event stream
event = debugger.wait_for_stop()
```

### 3.2 Coupled System

```python
system = CoupledSystem.from_host(bbc)

system.run()           # Run both
system.stop()          # Stop both (bus-stretch-safe)

# Run until PC reaches address on parasite (server-side, no polling)
system.parasite.debugger.add_breakpoint(0x0810, stop_counterpart=True)
system.run()
event = system.parasite.debugger.wait_for_stop()  # both stopped

# Run for emulated time (server-side cycle budget)
system.host.debugger.add_breakpoint(
    0x0000, end_address=0x10000, condition="cycles >= 20000000",
    stop_counterpart=True)
system.run()
event = system.host.debugger.wait_for_stop()

# Client-side predicate (periodic peek, no stop during evaluation)
system.run_until(
    lambda: screen_contains(bbc.memory, "Initialising"),
    emulated_seconds=10.0)
```

The `CoupledSystem` encapsulates the constraints of Section 2. The
caller does not need to know about bus stretching, pacing asymmetry,
or stop ordering.

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

Multiple clients may subscribe to the event stream concurrently. Each
subscription must receive all events independently -- one subscriber
draining events must not starve another.

## 5. Non-Requirements

- **Reverse debugging / time travel**: Not required.
- **Multi-parasite**: Only one parasite is supported at a time.
- **Cross-processor breakpoints with arbitrary conditions**: "Stop the
  parasite when the host writes to address X" is not required as an
  explicit feature. The cross-processor stop signal (see 1.8) combined
  with a host-side watchpoint achieves this implicitly.

## 6. Implementation Notes

### 6.1 Symmetry Between Breakpoints and Watchpoints

Breakpoints and watchpoints share the same design:

| | Breakpoint | Watchpoint |
|---|---|---|
| **Checked** | Instruction boundary | Bus access |
| **Matches** | PC in `[start, end)` | Address in `[start, end)` + type |
| **Condition** | Expression (optional) | Expression (optional) |
| **Hit counter** | `hits` in expression | `hits` in expression |

Both are stored as sorted vectors of entries, checked inline in the
tick loop with early exit on `start > current`. Both use the same
expression engine for conditions. Both support `stop_counterpart` for
cross-processor stop.

### 6.2 M6502 PC Semantics

The M6502 library's `pc.w` field is one past the opcode when
`M6502_IsAboutToExecute` is true, because `M6502_NextInstruction`
does `abus = pc; pc++` (post-increment). The `opcode_pc` field holds
the actual instruction address. All breakpoint matching, PC reporting
(via `Get6502State`), and PC setting (via `Set6502State`) use
`opcode_pc`, not `pc.w`.

`Machine::set_pc(value)` sets `opcode_pc = value`, `pc.w = value + 1`,
and `dbus = peek(value)` to ensure the CPU fetches from the correct
address.

### 6.3 Bus-Stretch Cancellation and Emulation Loop Synchronisation

`Machine::pause()` directly sets `TubeShared::bus_stretch_cancel`,
breaking the other process out of any spin-wait. `Machine::resume()`
clears it. No callback indirection.

RPCs that modify machine state (Reset, Set6502State) must ensure the
emulation loop is not concurrently accessing the machine. `pause()`
sets the flag but `run()` may still be mid-cycle. `wait_until_idle()`
spins until the emulation loop has exited `run()` (tracked by an
`in_run_` atomic flag). The sequence is: `pause()` →
`wait_until_idle()` → modify state → leave paused (or `resume()`).

### 6.4 Event Distribution

The event system has two layers:

1. **SPSC queue**: The emulation loop (breakpoint/watchpoint callbacks)
   writes events to a single lock-free SPSC queue
   (`moodycamel::ReaderWriterQueue`). The emulation loop never contends
   on subscriber mutexes.

2. **Per-subscriber fan-out**: Each `WatchExecutionState` stream has its
   own queue. Whichever subscriber wakes up first drains the SPSC queue
   into all subscriber queues (including its own), protected by a
   drain mutex that maintains the single-consumer guarantee on the SPSC
   queue. This ensures multiple concurrent subscribers each receive all
   events independently.

The `Run` RPC enqueues a "running" event with `is_running=true` before
calling `resume()`, ensuring the running event precedes any breakpoint
stop event in the queue regardless of timing.

### 6.5 Address Range Sizes

The `end` field in `BreakpointEntry` and `WatchpointEntry` is
`uint32_t`, not `uint16_t`, because the exclusive end of the full
address space is `0x10000` which does not fit in 16 bits. The `start`
field remains `uint16_t` since valid start addresses are 0x0000-0xFFFF.
