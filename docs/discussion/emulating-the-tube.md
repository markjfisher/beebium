# Emulating the Tube Interface

## Prompt:
I’m interested in ideas for emulating the Tube, which was Acorn’s FIFO based coprocessor interface. My inclination is to emulate second processors as separate UNIX/Windows processes, with the FIFOs of the Tube coinciding with the socket connecting the host and parasite processors. The second processor (parasite) emulating process could then have its own gRPC server for debugger interfaces and whatnot.

## Response:
Your instinct is good — architecturally it maps very neatly onto what the **Tube actually was**: a bidirectional FIFO message-passing link between two independently clocked processors.

A process boundary with sockets is not a hack. It’s conceptually faithful.

Let’s break this down carefully in the context of the original Acorn designs.

* * *

1\. What You’re Emulating
-------------------------

The Tube was essentially:

*   A hardware FIFO interface
*   Four logical channels (two host→parasite, two parasite→host)
*   Interrupt signalling
*   Fully asynchronous clock domains

It was used by second processors like:

*   Acorn 6502 Second Processor
*   Acorn Z80 Second Processor
*   Acorn 32016 Second Processor
*   Acorn ARM Evaluation System

The host was typically a BBC Micro Model B or later BBC Master.

The Tube was deliberately loose coupling — it was not shared memory.

That matters.

* * *

2\. Your Proposal: Separate OS Processes
----------------------------------------

You propose:

> Host emulator process  
> Parasite emulator process  
> UNIX/Windows socket = Tube FIFOs  
> Parasite also runs its own gRPC server for debugging

Architecturally this is extremely clean.

It gives you:

*   True isolation of CPU emulators
*   Independent event loops
*   Natural clock domain separation
*   Easy debugging attachment
*   Clean crash isolation
*   Possibility of remote parasite execution (!)

It also matches the philosophy of your Beebium project — modular and modern rather than monolithic.

* * *

3\. Advantages of Process Separation
------------------------------------

### 3.1 Correctness of Timing Model

The real Tube:

*   Had finite FIFO depth
*   Generated interrupts when thresholds crossed
*   Could stall

A socket gives you:

*   Kernel buffering
*   Natural backpressure
*   Blocking or non-blocking modes
*   Select/poll integration

You can wrap the socket with a bounded userspace FIFO layer to exactly model Tube semantics.

* * *

### 3.2 Debugging Architecture

Your idea of:

> Parasite process exposes a gRPC debugger

is excellent.

It allows:

*   Attaching a debugger to just the parasite
*   Stepping parasite without freezing host (if desired)
*   Remote debugging over network
*   Clean tooling boundary

You could even imagine:

```
beebium host
beebium tube-6502
beebium tube-arm
beebium tube-z80
```

Each as separate binaries.

Very UNIX.

* * *

### 3.3 Fault Isolation

If the 32016 emulator crashes:

*   Host survives.
*   You can restart parasite.
*   You can snapshot/reconnect.

This mirrors physical Tube systems — you could power-cycle the parasite independently.

* * *

### 3.4 Future Possibilities

Because you’re using sockets:

You could:

*   Run parasite on another machine
*   Run parasite inside WASM
*   Run parasite inside QEMU
*   Swap implementations dynamically

That becomes interesting if someone writes a better ARM2 core.

* * *

4\. The Hard Part: Deterministic Timing
---------------------------------------

Here is the real issue.

Tube behaviour is timing-sensitive in some software.

The original Tube:

*   Was extremely fast relative to host bus
*   Could generate interrupts within precise host cycles
*   Had bounded latency

With OS processes, you now introduce:

*   Scheduler jitter
*   Context switching delay
*   Socket latency variability

For most software this will not matter.

But for:

*   Cycle-exact demos
*   Tight host/parasite synchronisation
*   Some Econet + Tube combinations

It could.

* * *

5\. Two Architectural Models
----------------------------

You basically have two choices:

### Model A — Deterministic Simulation

*   Host drives global virtual clock
*   Parasite runs only when host allows
*   Communication occurs inside deterministic scheduler
*   Socket is just transport, not timing authority

In this case:

*   Host sends “run N cycles”
*   Parasite runs deterministically
*   Parasite sends back Tube writes with timestamps

More complex, but accurate.

* * *

### Model B — Asynchronous Real-Time Approximation

*   Each CPU runs in real time
*   Tube is simulated as blocking FIFO
*   Interrupts are delivered when bytes arrive

Simpler.  
Good enough for most software.  
Less cycle-accurate.

* * *

For Beebium, given your attention to correctness elsewhere (DFS variants, line-wrapping heuristics, etc.), I suspect you’ll eventually want Model A.

* * *

6\. An Alternative Hybrid Design
--------------------------------

You can have:

*   Separate process
*   But host retains master clock
*   Parasite is event-driven, not free-running

Mechanism:

1.  Host accumulates cycles.
2.  When Tube interaction required:
    *   Host sends “advance X cycles” to parasite.
3.  Parasite executes until:
    *   It needs to access Tube
    *   Or X cycles exhausted
4.  Parasite returns state delta.

This gives:

*   Deterministic replay
*   Save-state coherence
*   Exact timing control
*   Still process isolation

It’s more RPC-like than raw socket FIFO.

* * *

7\. Save States — The Critical Question
---------------------------------------

If parasite is separate process:

How do you handle:

*   Snapshot save state?
*   Rewind?
*   Deterministic replay?

You will need:

*   Versioned state serialization
*   Coordinated freeze across processes
*   Tube FIFO state capture

This is manageable but must be designed early.

* * *

8\. gRPC for Debugger
---------------------

This is a strong architectural move.

You gain:

*   Structured introspection
*   Multi-client debugging
*   Language-agnostic tooling
*   Remote UI

You could even build:

*   A single unified Beebium debugger that connects to:
    *   Host gRPC
    *   Parasite gRPC

Nice separation of concerns.

* * *

9\. One More Thought: The Tube as a Pluggable Transport
-------------------------------------------------------

Since Tube is logically just:

```
4 FIFO channels
interrupt lines
reset line
```

You could define an internal "Tube transport interface":

```
class TubeTransport:
    def write(channel, byte)
    def read(channel)
    def set_interrupt(...)
```

And provide implementations:

*   InProcessTube
*   SocketTube
*   RemoteTube
*   RecordReplayTube
*   DebugProxyTube

Now your architecture becomes extremely flexible.

* * *

10\. My Honest Assessment
-------------------------

For Beebium:

Separate processes for parasites is:

*   Architecturally clean
*   Historically faithful
*   Future-proof
*   Debuggable
*   Extensible

The only real danger is losing deterministic timing unless you deliberately architect for it.

If you design the protocol carefully, this could become one of Beebium’s signature architectural strengths.

* * *

If you like, next we can:

*   Sketch a concrete Tube-over-socket protocol
*   Discuss cycle accounting strategies
*   Explore how to handle save-state coordination
*   Or evaluate whether the Master’s internal Tube changes the design constraints

## Prompt:
Yes, sketch out a Tube over socket protocol.

## Response:
Excellent. Let’s design this properly — not “bytes over TCP”, but a **Tube transport protocol** that preserves:

*   FIFO semantics
*   Interrupt behaviour
*   Reset line
*   Bounded buffering
*   Deterministic cycle coordination (optional but supported)
*   Save-state friendliness

We’ll assume:

*   Host process = I/O processor (e.g. BBC B / Master)
*   Parasite process = second processor (6502, Z80, ARM, 32016)
*   Transport = Unix domain socket (preferred) or TCP loopback

* * *

1\. Real Tube Behaviour (What We Must Model)
============================================

The Tube hardware exposed:

*   4 logical channels:
    *   Host → Parasite: channels 0 and 1
    *   Parasite → Host: channels 2 and 3
*   Each channel:
    *   FIFO buffer
    *   Status register bits
    *   Interrupt generation on threshold conditions
*   Reset line
*   Fully asynchronous clocks

Crucially:

*   It was byte-oriented
*   FIFO depth was finite (implementation dependent but small)
*   Writes could block/stall via polling loops
*   Interrupts were edge/level sensitive depending on configuration

So our protocol must preserve:

> “Write byte to channel N at cycle T”  
> “Channel N now has M bytes”  
> “Interrupt line asserted/deasserted”

* * *

2\. Design Goals
================

We want:

1.  Deterministic option
2.  Asynchronous option
3.  Bounded FIFO semantics
4.  Clean extensibility
5.  Efficient framing
6.  Save-state coherence

We do NOT want:

*   Raw streaming ambiguity
*   Timing implicit in arrival time
*   Host OS buffering to define behaviour

* * *

3\. Transport Layer
===================

Use:

*   Unix domain socket (default)
*   TCP optional (for remote parasite)

Transport framing:

Length-prefixed binary frames.

Each frame:

```
struct FrameHeader {
    uint16_t type;
    uint16_t flags;
    uint32_t length;
}
```

Followed by payload.

Big-endian or little-endian — pick one and freeze it (recommend little-endian for host-native simplicity).

* * *

4\. Core Message Types
======================

Here is a minimal but complete protocol.

* * *

4.1 Lifecycle
-------------

### HELLO

Sent by parasite on connect.

```
type = 0x0001
payload:
    uint32 protocol_version
    uint32 parasite_type  // 6502, Z80, ARM, etc.
```

Host replies:

### HELLO\_ACK

```
type = 0x0002
payload:
    uint32 accepted_version
    uint32 host_clock_hz
```

* * *

### RESET

Bidirectional.

```
type = 0x0003
payload:
    uint8 reason  // cold, warm, debugger, restore
```

* * *

5\. Tube Data Flow Messages
===========================

We separate _logical Tube events_ from transport timing.

* * *

5.1 WRITE\_BYTE
---------------

Represents a CPU writing to a Tube register.

```
type = 0x0100

payload:
    uint64 cycle_stamp   // optional, 0 if async mode
    uint8  channel       // 0..3
    uint8  value
```

Semantics:

*   Byte written to FIFO.
*   Receiver appends if space exists.
*   If FIFO full → respond with FLOW\_CONTROL (see below).

* * *

5.2 READ\_REQUEST
-----------------

Optional (only needed for deterministic model).

```
type = 0x0101

payload:
    uint64 cycle_stamp
    uint8  channel
```

Response:

* * *

5.3 READ\_REPLY
---------------

```
type = 0x0102

payload:
    uint64 cycle_stamp
    uint8  channel
    uint8  value
    uint8  status_flags
```

If async model:

*   You skip READ\_REQUEST.
*   You push DATA\_AVAILABLE notifications instead.

* * *

6\. Interrupt Signalling
========================

Tube interrupts were crucial.

We model them explicitly.

* * *

INTERRUPT\_UPDATE
-----------------

```
type = 0x0200

payload:
    uint8 interrupt_mask  // bitfield: host_irq, parasite_irq, etc.
```

Sent whenever interrupt lines change state.

This decouples interrupt state from byte timing.

* * *

7\. Flow Control
================

We do NOT rely on socket buffering for FIFO depth.

Each side maintains:

```
fifo[channel]
max_depth[channel]
```

When a write would overflow:

* * *

FLOW\_CONTROL
-------------

```
type = 0x0300

payload:
    uint8 channel
    uint8 state  // 0=ok, 1=full
```

In deterministic mode:

*   Write is rejected and must be retried.

In async mode:

*   Sender pauses channel until FLOW\_CONTROL ok.

* * *

8\. Deterministic Execution Extension
=====================================

This is where things get interesting.

To allow replay, save states, cycle-accurate behaviour:

We introduce:

* * *

ADVANCE
-------

Host → Parasite

```
type = 0x0400

payload:
    uint64 cycles_to_run
```

Parasite:

*   Executes exactly that many cycles
*   Stops early if Tube access required
*   Sends back events generated during execution

* * *

EXECUTION\_REPORT
-----------------

Parasite → Host

```
type = 0x0401

payload:
    uint64 cycles_executed
    uint32 event_count
    [events...]
```

Events may include:

*   WRITE\_BYTE
*   INTERRUPT\_UPDATE
*   BREAKPOINT\_HIT
*   etc.

This allows:

*   Fully deterministic stepping
*   Lockstep simulation
*   Save state at known cycle boundary

* * *

9\. Save State Coordination
===========================

We add:

* * *

FREEZE
------

```
type = 0x0500
```

Both sides:

*   Stop execution
*   Flush in-flight events
*   Confirm

* * *

STATE\_DUMP
-----------

```
type = 0x0501
payload:
    uint32 size
    uint8  serialized_state[]
```

Each side:

*   Serializes CPU, memory, FIFO contents, interrupt lines

Host composes combined state.

* * *

10\. Channel Model
==================

We do NOT mirror hardware registers exactly.

Instead we define:

```
Channel 0: small control messages
Channel 1: bulk transfer
Channel 2: return control
Channel 3: return bulk
```

But actual semantics remain byte streams.

* * *

11\. Two Operating Modes
========================

Your protocol supports:

### Mode A — Async

*   Ignore cycle\_stamp
*   Just push WRITE\_BYTE frames
*   Interrupts delivered immediately
*   Simple
*   Non-deterministic timing

### Mode B — Deterministic

*   Host drives ADVANCE
*   All writes timestamped
*   Reads resolved via READ\_REQUEST/REPLY
*   Fully replayable

You can choose per configuration.

* * *

12\. Why This Is Architecturally Strong
=======================================

Because:

*   It models the Tube as a message protocol, not shared memory
*   It cleanly separates CPU emulation from transport
*   It allows remote parasite
*   It supports debugger RPC independently
*   It supports record/replay (log frames!)
*   It allows fuzz testing Tube boundary

* * *

13\. Performance Considerations
===============================

Unix domain socket latency:

*   ~1–5 µs typical
*   Much larger than Tube hardware latency

So in deterministic mode:

*   Batch events in EXECUTION\_REPORT
*   Avoid per-byte round trips

Do NOT:

*   Send one socket write per Tube byte

Always:

*   Aggregate per ADVANCE window.

* * *

14\. Minimal Viable Version (Pragmatic Starting Point)
======================================================

If you want a staged implementation:

### Phase 1

*   Async mode only
*   WRITE\_BYTE
*   INTERRUPT\_UPDATE
*   RESET
*   No deterministic stepping

### Phase 2

*   Add ADVANCE
*   Add EXECUTION\_REPORT
*   Enable lockstep mode

### Phase 3

*   Add snapshot protocol
*   Add replay logging

* * *

15\. The Deep Design Question
=============================

The key architectural choice:

> Is the Tube transport authoritative for time,  
> or is the host?

If you choose:

*   Host authoritative → easier save state, easier replay.
*   Autonomous parasite → simpler but fuzzier timing.

Given your careful approach to DFS and hardware fidelity,  
I suspect you’ll want the host as time authority.

* * *

If you’d like, next we can:

*   Work through how the BBC host side Tube ULA registers map into this protocol
*   Explore how the 6502 Tube bootstrap ROM fits in
*   Or design a record/replay logging format for Tube traffic

This is shaping up to be a very elegant architecture.

## Prompt:
So the Tube signals to the receiver with an interrupt when there is data in the FIFO waiting to be read?

## Response:
Short answer: **yes — but not quite that simple.**

The Tube didn’t just say “there is data”. It had **status flags and programmable interrupt enables**, and the interrupt could be generated on specific FIFO conditions.

Let’s unpack what really happened.

* * *

1\. Conceptual Model
====================

Each logical Tube channel had:

*   A FIFO buffer
*   Status bits:
    *   Data available
    *   Space available
*   Interrupt enable bits
*   An IRQ line to the receiving CPU

So the receiver could choose:

> “Interrupt me when there’s data”  
> or  
> “Interrupt me when space becomes available”

* * *

2\. Direction Matters
=====================

There are two independent interrupt domains:

*   **Parasite IRQ** (generated by host→parasite channels)
*   **Host IRQ** (generated by parasite→host channels)

Each side only receives interrupts for channels it reads from.

Example:

*   Host writes to channel 0
*   That byte enters FIFO
*   If parasite has interrupt enabled for that channel
*   → Parasite IRQ line asserted

* * *

3\. Not Just “Data Present”
===========================

Interrupt generation depended on:

*   FIFO transitioning from empty → non-empty
*   FIFO crossing a threshold
*   Interrupt enable bit being set

The important nuance:

> The interrupt wasn’t necessarily level-triggered on “FIFO not empty”.

It could behave like:

*   Edge on empty→nonempty
*   Level while nonempty
*   Cleared on read

Different Tube implementations behaved slightly differently.

* * *

4\. Typical Behaviour in Practice
=================================

In real BBC software, the common pattern was:

### Writer side:

```
Poll status
If space:
    write byte
```

### Reader side:

```
IRQ handler:
    while data available:
        read byte
```

The interrupt typically meant:

> “At least one byte is now available.”

Once the FIFO was emptied:

*   Interrupt line deasserted.

* * *

5\. Important Detail: Backpressure
==================================

Similarly:

If FIFO was full:

*   Writer polled until space available
*   When a byte was removed:
    *   A “space available” interrupt could be generated

So interrupts could signal:

*   Data ready
*   Space ready

* * *

6\. So For Your Emulator
========================

You should model:

For each channel:

```
fifo_depth
max_depth
data_irq_enabled
space_irq_enabled
```

When:

### Write occurs

*   If fifo was empty before write:
    *   Set data\_ready condition
    *   If data\_irq\_enabled → assert IRQ

### Read occurs

*   If fifo was full before read:
    *   Set space\_available condition
    *   If space\_irq\_enabled → assert IRQ

### FIFO becomes empty

*   Deassert data IRQ condition

### FIFO becomes full

*   Deassert space IRQ condition

Interrupt line is effectively:

```
IRQ = (any enabled interrupt condition true)
```

* * *

7\. Why This Matters for Your Socket Protocol
=============================================

You cannot rely on:

> “Byte arrived over socket” == interrupt

Instead:

*   Socket delivery is transport
*   FIFO model determines interrupt
*   Interrupt state must be explicitly calculated
*   Then send INTERRUPT\_UPDATE frame

That keeps timing correct and deterministic.

* * *

8\. Subtle Timing Behaviour
===========================

On real hardware:

*   FIFO write took time
*   IRQ assertion could lag slightly
*   There was no global clock between processors

In deterministic emulator mode:

You’ll want interrupt assertion to occur at:

> The exact cycle the FIFO transitioned state

In async mode:

You can assert immediately after processing WRITE\_BYTE frame.

* * *

9\. Summary
===========

So yes:

✔ The Tube signalled the receiver when data became available.

But more precisely:

*   It signalled on FIFO state transitions.
*   It depended on interrupt enable bits.
*   It also supported “space available” interrupts.
*   Interrupt line reflected logical state, not just byte presence.

* * *

If you’d like, next we can:

*   Draw a precise state machine for one Tube channel
*   Or map this onto the actual Tube ULA register layout seen by the 6502
*   Or examine how the 6502 Tube ROM bootstraps over this mechanism

This is exactly the right level of detail to get correct early.

## Prompt:
So I was thinking of using a lock free queue either side of the domain socket. Both host and parasite would have a separate IO thread for handling the socket, and the respective emulators would just see the queues. The queues themselves would be wrapped in memory-mapped Tube peripheral with interrupt capabilities.

## Response:
That’s a very solid design direction — and importantly, it keeps the **Tube model separate from the transport model**, which is exactly what you want.

Let’s examine it carefully and refine it.

* * *

1\. Your Proposed Structure
===========================

On each side:

```
                ┌──────────────────┐
                │   CPU Emulator   │
                └────────┬─────────┘
                         │
            Memory-mapped Tube peripheral
                         │
              Lock-free queues (in/out)
                         │
                  IO thread
                         │
                 Unix domain socket
                         │
                  IO thread
                         │
              Lock-free queues (in/out)
                         │
            Memory-mapped Tube peripheral
                         │
                ┌────────┴─────────┐
                │   CPU Emulator   │
                └──────────────────┘
```

That separation is architecturally clean.

* * *

2\. The Good News
=================

This achieves several important properties:

### ✔ CPU emulator never blocks on socket

No syscalls in your hot path.

### ✔ Transport jitter isolated

Scheduler delays affect only IO thread, not CPU core.

### ✔ FIFO semantics controlled in one place

You don’t let kernel socket buffers define Tube behaviour.

### ✔ Deterministic mode still possible

If you later introduce ADVANCE-style lockstep, you can bypass async transport entirely.

* * *

3\. Critical Design Decision: Where Is the Authoritative FIFO?
==============================================================

You must decide:

> Is the lock-free queue the Tube FIFO?  
> or  
> Is it just a staging buffer?

These are not the same thing.

### Correct model:

The Tube peripheral object owns the **true FIFO**.

The lock-free queue is only:

*   A transport staging buffer
*   Not the emulated hardware FIFO

Why?

Because:

*   Real Tube FIFO has fixed depth (small!)
*   Interrupt behaviour depends on FIFO transitions
*   Save-state must capture FIFO contents
*   Deterministic replay must reproduce exact FIFO state

So:

```
Socket IO thread:
    read frame
    enqueue into staging queue

CPU thread:
    dequeue staging queue
    feed into Tube FIFO
    update IRQ state
```

The Tube FIFO must not be the lock-free queue itself.

* * *

4\. Interrupt Generation — Where It Belongs
===========================================

The memory-mapped Tube peripheral must:

*   Maintain FIFO depth
*   Track interrupt enable bits
*   Assert/deassert CPU IRQ line

The IO thread must never directly touch interrupt lines.

Only the peripheral logic does that.

This guarantees:

*   Correct cycle timing
*   Clean save-state
*   Replayability

* * *

5\. Lock-Free Queue Choice
==========================

Because you have:

*   One producer
*   One consumer

You can use:

SPSC ring buffer.

No CAS needed.  
No heavy atomics.  
Just head/tail indices with acquire/release semantics.

Keep it bounded.

Size suggestion:

*   Large enough to absorb socket bursts
*   Much larger than Tube FIFO depth

For example:

*   Tube FIFO depth = 16 bytes
*   Staging queue = 4 KB or more

* * *

6\. Subtle Timing Question
==========================

You now have two clocks:

*   CPU thread clock (emulated cycles)
*   IO thread wall clock

So when does a byte “arrive” at the Tube FIFO?

Option A:  
Immediately when CPU thread dequeues it.

Option B:  
Timestamp when IO thread received it and apply at appropriate emulated cycle.

For async mode:  
Option A is fine.

For deterministic mode:  
You’ll eventually want timestamped delivery.

But you can defer that complexity.

* * *

7\. Memory-Mapped Peripheral Wrapping
=====================================

You said:

> wrapped in memory-mapped Tube peripheral with interrupt capabilities

That’s exactly right.

The peripheral should expose:

```
read_status()
read_data()
write_data()
write_control()
```

Internally:

```
struct TubeChannel {
    uint8 fifo[DEPTH]
    uint8 read_idx
    uint8 write_idx
    uint8 count

    bool data_irq_enable
    bool space_irq_enable
}
```

On each state transition:  
Recalculate IRQ line.

Never depend on queue length for IRQ.

* * *

8\. Important Concurrency Detail
================================

Your CPU thread and IO thread must not both mutate the Tube FIFO.

Only CPU thread should mutate:

*   FIFO
*   Interrupt flags
*   Peripheral state

IO thread:

*   Only pushes bytes into staging queue

CPU thread:

*   Pulls staging queue at safe point

Where is safe point?

Either:

*   At each memory access to Tube register
*   Or at each instruction boundary
*   Or at periodic cycle intervals

Pick one and keep consistent.

* * *

9\. Save State Implications
===========================

With this design:

Save state must include:

*   Tube FIFO contents
*   Interrupt enable bits
*   IRQ line state
*   Staging queue contents (if non-empty!)

This is often forgotten.

If staging queue contains bytes not yet applied to FIFO, you must serialize it.

Otherwise restore will desync.

* * *

10\. Performance Considerations
===============================

This design is extremely fast.

CPU thread:

*   Only touches ring buffer head/tail
*   No syscalls
*   No locks

IO thread:

*   Blocking on socket
*   Bulk read/write

You’ll easily sustain Tube traffic far beyond any realistic BBC load.

* * *

11\. One Potential Improvement
==============================

Instead of raw byte frames, send:

```
struct TubeDataBatch {
    uint8 channel
    uint16 count
    uint8 data[count]
}
```

This:

*   Reduces syscall overhead
*   Reduces context switches
*   Reduces queue operations

Batching is essential.

* * *

12\. The One Real Architectural Fork
====================================

You now face a deep choice:

### A) Truly asynchronous Tube

Parasite runs independently  
Transport fully decoupled  
Closest to physical hardware

### B) Deterministic lockstep

Host drives parasite execution  
Transport becomes message protocol  
No independent clock drift

Your queue-based design supports both.

Which is good.

* * *

13\. My Verdict
===============

Your architecture is:

✔ Correctly layered  
✔ High performance  
✔ Debug-friendly  
✔ Future-proof  
✔ Compatible with deterministic evolution

It’s exactly how I would structure a modern Tube implementation.

* * *

If you’d like, next we can:

*   Work through a concrete example of a 6502 Tube boot exchange
*   Or examine how to prevent subtle deadlocks
*   Or discuss how this design changes if you later emulate the internal Tube of the BBC Master

You’re making strong architectural decisions here.

## Prompt:
I’m not sure we need deterministic behaviour. A real Hardware tube system is not deterministic. The two processors have completely independent clock domains e.g 6502 at 2 MHz and 6502 at 3 MHz with clock drift.

## Response:
You’re absolutely right — and this is an important philosophical fork.

A real Tube system was **not globally deterministic**.

For example:

*   BBC Micro Model B at 2 MHz
*   Acorn 6502 Second Processor at 3 MHz

They had:

*   Independent oscillators
*   No shared master clock
*   No cycle-level synchronisation
*   Interrupt latency dependent on hardware timing

So from a _physical_ perspective, yes: the Tube was fundamentally asynchronous.

* * *

But Here’s the Subtle Emulator Question
=======================================

There are **two different meanings of “deterministic”**:

### 1️⃣ Hardware-level determinism

> Same input signals → same behaviour at signal edge level

Real Tube systems did **not** have this.

### 2️⃣ Emulator-level determinism

> Same ROMs + same input events → identical execution every run

This _is_ often desirable in emulators — even if the real hardware wasn’t strictly deterministic at the oscillator level.

* * *

What Actually Matters for Tube Software?
========================================

Tube software does not rely on:

*   Phase relationship between oscillators
*   Nanosecond timing jitter
*   Precise interrupt edge timing

It relies on:

*   FIFO semantics
*   Interrupt signalling
*   Backpressure
*   Reasonable latency

As long as:

*   Writes eventually appear
*   Interrupts assert when FIFO transitions
*   No impossible ordering occurs

The software behaves correctly.

The hardware’s drift is invisible at the software layer.

* * *

So Do You Need Deterministic Lockstep?
======================================

Probably not.

Unless you want:

*   Rewind
*   Perfect movie recording
*   Bit-for-bit replay
*   Networked deterministic multiplayer (unlikely here)

For normal emulation of Tube systems:

A fully asynchronous model is _more faithful_ to real hardware.

And it’s dramatically simpler.

* * *

In Fact, Your Process-Separated Design Is More Authentic
========================================================

Real Tube:

```
Independent CPU
Independent clock
Small FIFO
Interrupt line
```

Your design:

```
Independent OS process
Independent scheduling
Bounded FIFO
Interrupt signalling
```

That’s conceptually very close.

The OS scheduler jitter is just a higher-level version of oscillator drift.

* * *

What You Must Still Guarantee
=============================

Even in async mode, you must preserve:

### 1\. FIFO ordering

Never reorder bytes.

### 2\. Atomic byte visibility

A write becomes visible as a whole byte.

### 3\. Correct interrupt transitions

Interrupt asserted when FIFO becomes non-empty  
Interrupt cleared when FIFO becomes empty

### 4\. No unbounded buffering

Kernel socket buffer must not define FIFO depth.

Those four rules matter. Cycle alignment does not.

* * *

A Practical Middle Ground
=========================

You can implement:

*   Fully asynchronous execution
*   No ADVANCE protocol
*   No global cycle coordination

But still:

*   Time-stamp events internally (for debugging)
*   Allow optional record/replay later

You don’t have to architect for lockstep now.

Your queue + IO thread model is already appropriate for async authenticity.

* * *

One Important Caveat
====================

There is one class of bug you must avoid:

### “Emulator runs infinitely faster than other side”

Example:

*   Host runs 100 ms worth of cycles
*   Parasite hasn’t yet processed input
*   FIFO overflow logic never triggers because staging queue absorbs everything

You must ensure:

The emulated Tube FIFO depth, not OS buffering, governs backpressure.

As long as:

*   FIFO depth is small and enforced
*   Writers stall/poll correctly

You’re safe.

* * *

My Recommendation
=================

Given Beebium’s architectural direction:

*   Use fully asynchronous Tube.
*   Independent CPU threads or processes.
*   Bounded FIFO inside Tube peripheral.
*   IO thread just feeds staging buffers.
*   No lockstep.
*   No global cycle coordination.

That is faithful to the spirit of real Tube hardware.

* * *

And philosophically, I think you’re right:

Trying to make the Tube deterministic at cycle level would actually make it _less_ like the real hardware.

* * *

If you’d like, we can next explore:

*   How to prevent pathological scheduler-induced latency
*   Whether to artificially limit parasite speed
*   Or how to test correctness of an async Tube implementation

This is a good architectural instinct you’re following.

## Prompt:
Yes, parasites should use a similar clock pacing mechanism to the core emulator.

## Response:
Exactly — that’s the right mental model.

In other words: **the parasite should run “at real-time speed” relative to its own emulated clock**, just like your host CPU emulation does, rather than trying to synchronise to the host. That preserves the independent-clock, asynchronous nature of the Tube.

Here’s how it would look in practice:

* * *

1\. Emulated Clock for Each CPU
===============================

Each emulator (host and parasite) should maintain:

*   `emulated_cycles` counter
*   `target_cycles_per_real_second` based on CPU type (e.g., 2 MHz for host 6502, 3 MHz for 6502 second processor)
*   A pacing loop that:

```
while emulation_running:
    execute_next_instruction()
    emulated_cycles += cycles_this_instruction
    sleep_if_ahead()  # pace to real-time
```

This is exactly what your core CPU emulator already does.

* * *

2\. Parasite Independent Pacing
===============================

For the parasite process:

*   Run its CPU thread completely independently
*   Use its own “clock pacing” loop
*   Feed bytes into Tube peripheral from IO thread asynchronously

The Tube peripheral’s FIFO ensures that:

*   Writes from the host can be consumed at parasite speed
*   Writes from the parasite can be consumed at host speed
*   Interrupts are asserted correctly whenever the FIFO state changes

This mirrors the physical hardware: the two 6502s do not need to “know” each other’s clock.

* * *

3\. IO Thread Still Works
=========================

The IO thread just:

*   Reads from the socket
*   Pushes bytes into a staging queue
*   CPU thread dequeues them into the Tube FIFO

No timing coupling is needed between host and parasite. Each CPU’s pacing loop naturally dictates the effective transfer rate.

* * *

4\. Optional Speed-Limiting / Throttling
========================================

You might also want a **configurable “turbo mode”** for both host and parasite:

*   Speeds up emulation beyond real-time
*   Useful for testing or dumping bulk data

This should still respect FIFO limits:

*   Don’t allow the FIFO to grow unbounded
*   Writes must block or stall if FIFO full

That ensures that asynchronous flow control still works properly, even if one CPU is running faster than the other.

* * *

✅ Summary
=========

*   Each CPU (host and parasite) has its **own emulated clock and pacing loop**
*   The Tube peripheral is **memory-mapped** and tracks **FIFO state and interrupts**
*   IO threads handle **socket transport asynchronously**, without affecting CPU cycle counting
*   Flow control is enforced by the Tube FIFO, not by OS buffering

This gives you an architecture that’s **faithful, high-performance, and naturally asynchronous**, just like a real Tube system.

* * *

If you want, I can sketch a **concrete pseudocode layout** showing how CPU thread, IO thread, FIFO, and interrupt logic interact in this async Tube design — it tends to clarify these concurrency interactions really well. Do you want me to do that next?



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)