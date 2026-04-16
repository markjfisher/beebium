# Tube Single-Threaded Migration Plan

## Problem

The current Tube implementation runs the host and parasite CPUs on
independent threads, communicating through a lock-free `TubeUla` with
atomic operations and spin-wait bus stretching. This causes a
non-deterministic protocol desynchronisation (GitHub issue #24) where
OSWORD &72 SCSI operations followed by OSBYTE &8F ADFS filing system
selection deadlock. The R2 command/response protocol loses
synchronisation because the two threads can interleave in ways that
don't occur on real hardware.

## Root Cause

The real BBC Micro's host (2 MHz) and parasite (3 MHz) have
independent clock domains, bridged asynchronously by the Tube ULA. The
Tube ULA's FIFOs and status latches are designed for this asynchronous
operation. However, the emulator's dual-threaded approach introduces a
different kind of non-determinism: OS thread scheduling determines the
relative timing of host and parasite register accesses, and this
scheduling has no relationship to the real hardware's timing.

The specific failure mode: during a sequence of SCSI OSWORD &72 calls
followed by an ADFS filing system selection (OSBYTE &8F), the R2
protocol between parasite and host loses synchronisation. The parasite
sends an R2 command that the host never reads, because the host has
already returned from the Tube Host Code main loop to the MOS event
loop. Transfer counters and protocol traces confirm significant Tube
traffic (461 R2 exchanges, 16384 R3 bytes, 457 R4 bytes) before the
deadlock, but the exact desync point occurs non-deterministically.

## Solution

Replace the dual-threaded architecture with a single-threaded
interleaved design, inspired by B2. Both CPUs tick within
`Machine::step()`, interleaved deterministically. This eliminates the
entire class of threading-related timing bugs.

B2's approach:
- Single update function ticks parasite first, then host
- All Tube register accesses are plain struct operations (no atomics)
- Status flags update immediately on read/write
- IRQ/NMI lines update immediately after each CPU tick
- Clock ratio handled by a fractional accumulator

## Architecture Overview

### Current (Broken)

```
Host Thread                    Parasite Thread
    |                               |
Machine::step()             ParasiteRunner::run()
    |                               |
    v                               v
host_write(R2) ----atomic---> parasite_read(R2)
    |          TubeUla (SPSC)       |
host_read(R4) <----atomic---- parasite_write(R4)
```

Both threads tick independently via separate PacingClocks.
Communication through lock-free atomics with acquire/release ordering.
Bus stretching via spin-wait yield loops.

### Target (Single-Threaded)

```
Machine::step()
    |
    +-- TubeSocket::tick_parasite()
    |       |
    |       +-- ParasiteCpu::tick()  [1 or 2 times per host cycle]
    |       |       |
    |       |       +-- parasite_read/write -> TubeUla (plain structs)
    |       |
    |       +-- update parasite IRQ/NMI from TubeUla state
    |
    +-- CpuBinding::tick()  [host CPU]
    |       |
    |       +-- host_read/write -> TubeUla (plain structs)
    |
    +-- update host IRQ from TubeUla state
    +-- check Tube bus stretch
```

Single thread. No atomics. No spin-waits. Deterministic interleaving.

## Implementation Phases

### Phase 1: De-atomicise TubeUla

Remove all `std::atomic` from `TubeUla`, replacing with plain types.
Remove spin-wait yield loops. Merge the split interrupt update
functions into a single `update_interrupts()`.

#### Files

**`src/core/include/beebium/tube/TubeUla.hpp`**

Replace the three atomic data structures with plain equivalents:

```cpp
// BEFORE (atomic)
struct AtomicLatch {
    std::atomic<uint8_t> data{0};
    std::atomic<bool> available{false};
    std::atomic<bool> full{false};
};

// AFTER (plain)
struct Latch {
    uint8_t data = 0;
    bool available = false;
    bool full = false;
};
```

```cpp
// BEFORE (atomic FIFO)
struct AtomicFifo24 {
    std::array<std::atomic<uint8_t>, 24> data{};
    std::atomic<uint8_t> head{0};
    std::atomic<uint8_t> tail{0};
    std::atomic<uint8_t> count{0};
};

// AFTER (plain FIFO)
struct Fifo24 {
    std::array<uint8_t, 24> data{};
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;
};
```

```cpp
// BEFORE (atomic R3 register)
struct AtomicReg3 {
    std::array<std::atomic<uint8_t>, 2> data{};
    std::atomic<uint8_t> head{0};
    std::atomic<uint8_t> tail{0};
    std::atomic<uint16_t> state{0};  // packed count + pending
    // ... pack/unpack helpers ...
};

// AFTER (plain R3 register)
struct Reg3 {
    std::array<uint8_t, 2> data{};
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;
    bool pending = false;
};
```

Replace all `std::atomic<bool>` interrupt outputs with plain `bool`.
Replace `std::atomic<uint8_t> control_flags_` with plain `uint8_t`.

Remove `prev_pnmi_` and `pnmi_edge_` -- the split edge detection was
needed because two threads updated PNMI state. In single-threaded
mode, a single `prev_pnmi_` with inline edge detection suffices.

Replace `TransferCounters` atomic counters with plain `uint64_t`.

**`src/core/src/TubeUla.cpp`**

Simplify all register access methods:
- Remove all `std::memory_order_*` annotations
- Remove `.load()` / `.store()` / `.fetch_add()` / `.fetch_sub()` --
  use direct assignment
- Remove `compare_exchange_weak` loops in R3 -- use direct
  count/pending manipulation
- Remove `std::this_thread::yield()` spin-waits (Phase 2 adds the
  replacement)
- Remove `std::atomic_thread_fence` in `soft_reset()`
- Remove `#include <thread>`

Merge `update_host_interrupts()` and `update_parasite_interrupts()`
into a single `update_interrupts()`:

```cpp
void TubeUla::update_interrupts() {
    auto flags = control_flags_;

    // HIRQ: Q=1 and R4 P-to-H has data
    hirq_ = (flags & FLAG_Q) && r4_p2h_.available;

    // PIRQ: (I=1 and R1 H-to-P has data) or (J=1 and R4 H-to-P has data)
    pirq_ = ((flags & FLAG_I) && r1_h2p_.available)
          || ((flags & FLAG_J) && r4_h2p_.available);

    // PNMI: M=1 and R3 conditions met
    bool new_pnmi = false;
    if (flags & FLAG_M) {
        uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
        new_pnmi = (r3_h2p_.pending || r3_h2p_.count >= threshold)
                || !r3_p2h_.pending;
    }

    // Edge detection (single-threaded, no race)
    if (new_pnmi && !prev_pnmi_)
        pnmi_edge_ = true;
    if (!new_pnmi)
        pnmi_edge_ = false;
    prev_pnmi_ = new_pnmi;
    pnmi_level_ = new_pnmi;
}
```

Call `update_interrupts()` after every `host_read`, `host_write`,
`parasite_read`, and `parasite_write`.

#### Tests

All single-threaded Tube register tests should pass unchanged:
- `test_tube_ula.cpp`
- `test_tube_r1_6502.cpp`, `test_tube_r2_6502.cpp`,
  `test_tube_r4_6502.cpp`
- `test_tube_pirq_6502.cpp`, `test_tube_control_6502.cpp`
- `test_tube_r3_nmi_6502.cpp`, `test_tube_r3_p2h_6502.cpp`
- `test_tube_ce2023_trace.cpp`

Remove `test_tube_r3_race.cpp` (tests threading-specific race
conditions that no longer apply).

Run: `cd build-release && ctest --output-on-failure -R tube`

### Phase 2: Add Bus Stretch Flag

Replace spin-wait bus stretching with a deferred-write mechanism.

In real hardware, when the host writes to a full Tube register, the
Tube ULA holds the host CPU's clock until the parasite drains the
register. During stretch, the parasite and all peripherals (VIAs,
video, sound) continue running.

In single-threaded mode, we cannot spin-wait (that would also block
the parasite). Instead, we defer the write and signal that the host is
stretched.

#### Changes to TubeUla

Add members:

```cpp
bool host_stretched_ = false;
uint8_t pending_offset_ = 0;
uint8_t pending_value_ = 0;
```

In `host_write()`, for registers with bus stretching (R1 offset 1, R3
offset 5, R4 offset 7): if the register is full, set
`host_stretched_ = true`, store the pending offset and value, and
return without writing.

```cpp
case 1: {
    // R1 data: write to H-to-P latch.
    if (r1_h2p_.full) {
        host_stretched_ = true;
        pending_offset_ = offset;
        pending_value_ = value;
        return;  // Deferred -- Machine will retry after parasite ticks
    }
    r1_h2p_.data = value;
    r1_h2p_.available = true;
    r1_h2p_.full = true;
    counters_.r1_h2p_writes++;
    break;
}
```

Similarly for R3 (offset 5, when count >= 2) and R4 (offset 7, when
full).

For `host_read()` case 5 (R3 P-to-H with M set, spin-wait when
empty): if the FIFO is empty and M is set, set `host_stretched_ =
true` and return 0 without consuming.

Add `try_complete_stretch()`:

```cpp
bool TubeUla::try_complete_stretch() {
    if (!host_stretched_) return true;

    // Check if the blocking condition has cleared
    switch (pending_offset_ & 7) {
    case 1: if (r1_h2p_.full) return false; break;
    case 5: {
        // Writing: check count < 2. Reading: check count > 0.
        if (pending_is_read_) {
            if (r3_p2h_.count == 0) return false;
        } else {
            if (r3_h2p_.count >= 2) return false;
        }
        break;
    }
    case 7: if (r4_h2p_.full) return false; break;
    }

    // Condition cleared -- perform the deferred operation
    if (pending_is_read_) {
        // Re-execute the read (parasite_read already drained)
        // The host CPU will see the data on the next tick
    } else {
        host_write_immediate(pending_offset_, pending_value_);
    }
    host_stretched_ = false;
    return true;
}
```

The `TubeHostBackend` interface already declares `stretched()` and
`complete_pending_write()`, so no interface changes are needed.

#### Tests

Existing stretch-related tests in `test_tube_ula.cpp` should be
adapted to test the new deferred mechanism. Add a test that verifies
stretch clears after the parasite reads.

### Phase 3: Parasite Ticking in Machine::step()

The core architectural change.

#### Changes to TubeSocket

**`src/core/include/beebium/tube/TubeSocket.hpp`**

Add parasite management to TubeSocket:

```cpp
class TubeSocket {
public:
    // ... existing methods ...

    // Install a parasite runner for single-threaded ticking.
    // The caller (extension) owns the runner's lifetime.
    void install_parasite(ParasiteTickable* runner) {
        parasite_ = runner;
    }
    void remove_parasite() { parasite_ = nullptr; }

    // Tick the parasite CPU. Called from Machine::step() before the
    // host CPU tick. Uses fractional accumulator for clock ratio.
    void tick_parasite();

    // Tick the parasite during Tube bus stretch (host CPU halted).
    void tick_parasite_stretch();

    // Check if the host is Tube bus-stretched.
    bool tube_stretched() const;

    // Attempt to complete a pending stretch operation.
    bool try_complete_tube_stretch();

    // Configure clock ratio. numerator/denominator = parasite/host
    // clock ratio. For 3 MHz parasite with 2 MHz host: 3/2.
    void set_parasite_clock_ratio(uint8_t numerator, uint8_t denominator);

private:
    // ... existing members ...
    ParasiteTickable* parasite_ = nullptr;
    uint8_t parasite_phase_ = 0;
    uint8_t parasite_clock_num_ = 3;  // 3 MHz default
    uint8_t parasite_clock_den_ = 2;  // 2 MHz host
};
```

`ParasiteTickable` is a minimal interface that ParasiteRunner
implements:

```cpp
// src/core/include/beebium/tube/ParasiteTickable.hpp
class ParasiteTickable {
public:
    virtual ~ParasiteTickable() = default;
    virtual void tick() = 0;           // Execute one parasite cycle
    virtual bool is_paused() const = 0; // Debugger pause state
};
```

Implementation of `tick_parasite()`:

```cpp
void TubeSocket::tick_parasite() {
    if (!parasite_ || parasite_->is_paused()) return;

    // Fractional accumulator: add numerator each host cycle,
    // tick parasite once per denominator accumulated.
    parasite_phase_ += parasite_clock_num_;
    while (parasite_phase_ >= parasite_clock_den_) {
        parasite_phase_ -= parasite_clock_den_;
        parasite_->tick();
    }
}
```

For 3/2 ratio, over 2 host cycles:
- Cycle 0: phase 0+3=3, tick (3>=2), phase 3-2=1. One tick.
- Cycle 1: phase 1+3=4, tick (4>=2), phase 4-2=2, tick (2>=2), phase
  2-2=0. Two ticks.
- Total: 3 parasite ticks per 2 host cycles = 1.5x = 3 MHz/2 MHz.

`tick_parasite_stretch()` is identical but called during Tube bus
stretch cycles when the host CPU is halted.

#### Changes to Machine::step()

**`src/core/include/beebium/Machine.hpp`**

Add Tube bus stretch state:

```cpp
bool tube_stretch_active_ = false;
```

Insert into `step()`, before the existing 1 MHz stretch check at the
top:

```cpp
// Handle Tube bus stretch (host CPU halted, parasite + peripherals continue).
if (tube_stretch_active_) {
    state_.memory.tube_socket.tick_parasite_stretch();
    if (state_.memory.tube_socket.try_complete_tube_stretch()) {
        tube_stretch_active_ = false;
        // Fall through to normal step -- the deferred write completed,
        // host CPU can now proceed with the next cycle.
    } else {
        // Host still stretched. Tick peripherals (VIAs, video, sound)
        // but not the host CPU.
        tick_stretch_cycle();
        ++state_.cycle_count;
        ++sequence_;
        return;
    }
}
```

Insert parasite ticking before the host CPU tick (after the 1 MHz
stretch check, before `cpu_binding_.tick_rising/falling()`):

```cpp
// Tick parasite BEFORE host (B2 ordering).
// Parasite register writes are immediately visible to the host.
state_.memory.tube_socket.tick_parasite();
```

After the host CPU tick, after the existing IRQ/NMI handling, add Tube
stretch detection:

```cpp
// Check if the host's memory access triggered Tube bus stretching.
if (state_.memory.tube_socket.tube_stretched()) {
    tube_stretch_active_ = true;
}
```

Also add host Tube IRQ routing (the `poll_irq()` method should
already include the Tube socket's HIRQ via the existing IrqSource
interface, but verify this).

#### Tick Ordering Summary

Each `Machine::step()` call (one 2 MHz host cycle):

1. Check Tube bus stretch (if active, tick parasite + peripherals, skip host CPU)
2. Check 1 MHz bus stretch (if active, tick peripherals, skip host CPU)
3. **Tick parasite** (1 or 2 cycles via fractional accumulator)
4. Tick host CPU (CpuBinding rising/falling)
5. Tick VIAs, video, sound, Econet
6. Update host IRQ/NMI (including HIRQ from Tube)
7. Check for new Tube bus stretch

### Phase 4: Remove Threading from Extension

**`src/extensions/acorn-65c02-coprocessor/SecondProcessor65C02Extension.cpp`**

Remove:
- `std::thread parasite_thread_`
- `std::atomic<bool> running_`
- `PacingClock` and `PacingConfig`
- `run_parasite()` method
- `#include "beebium/PlatformSleep.hpp"` and `"beebium/SleepQuantum.hpp"`

Change `init()`:

```cpp
void SecondProcessor65C02Extension::init(ExtensionContext& ctx) {
    tube_socket_ = &ctx.get<TubeSocket>();

    std::array<uint8_t, 2048> rom{};
    if (!load_rom(rom))
        throw std::runtime_error("Failed to load Tube client ROM");

    tube_ula_ = std::make_unique<TubeUla>();
    runner_ = std::make_unique<ParasiteRunner>(*tube_ula_, rom);
    runner_->reset();

    // Install TubeUla as the host-side backend.
    tube_socket_->install_backend(tube_ula_.get());

    // Install the runner for single-threaded ticking from Machine::step().
    tube_socket_->install_parasite(runner_.get());
    tube_socket_->set_parasite_clock_ratio(3, 2);  // 3 MHz / 2 MHz

    // Debugger service (unchanged).
    debugger_service_ = std::make_unique<...>(*runner_);
    debugger_adapter_ = std::make_unique<...>(*debugger_service_);
}
```

Change `shutdown()`:

```cpp
void SecondProcessor65C02Extension::shutdown() {
    if (tube_socket_) {
        tube_socket_->remove_parasite();
        tube_socket_->install_backend(nullptr);
    }
    debugger_adapter_.reset();
    debugger_service_.reset();
    runner_.reset();
    tube_ula_.reset();
}
```

**`src/extensions/acorn-65c02-coprocessor/SecondProcessor65C02Extension.hpp`**

Remove `std::thread`, `std::atomic<bool>`, `PacingClock` members. Remove
`run_parasite()` declaration.

### Phase 5: Adapt Parasite Debugger

The parasite debugger currently works through `ParasiteRunner` which
has threading primitives (mutex, condition variable, `wait_if_paused`).
These need to be simplified for single-threaded operation.

#### ParasiteRunner Changes

**`src/core/include/beebium/tube/ParasiteRunner.hpp`**

Remove:
- `std::mutex` and `std::condition_variable` members
- `wait_if_paused()` method
- `in_run_` atomic flag (the RunGuard pattern)
- `request_shutdown()` and shutdown-related members

Add:
- Implement `ParasiteTickable` interface
- `bool is_paused() const` (checked by TubeSocket before ticking)

The `pause()` and `resume()` methods become simple boolean flag
operations:

```cpp
void pause() { paused_ = true; }
void resume() { paused_ = false; }
bool is_paused() const { return paused_; }
```

Keep:
- `tick()` method (one cycle)
- `step_instruction()` method (run until instruction boundary)
- `run(uint64_t cycles)` method (for unit tests)
- Breakpoint and watchpoint support
- Breakpoint hit callback (`on_breakpoint_hit_`)

#### Debugger Pause/Resume Semantics

**Parasite pause:** `TubeSocket::tick_parasite()` checks
`parasite_->is_paused()` and skips ticking. The host continues
running; the parasite appears unresponsive (host polls R2 status and
waits).

**Parasite step:** Both CPUs must be stopped first (the gRPC service
requires `is_paused()` before allowing `StepInstruction`). The
debugger calls `runner_->step_instruction()` directly, which ticks the
ParasiteCpu through the TubeUla. The host is not ticked during this
step, which is acceptable for debugging.

**Parasite resume:** Set `paused_ = false`. If the host is also
paused, the counterpart resume mechanism restarts it. On the next
`Machine::step()`, `TubeSocket::tick_parasite()` resumes ticking the
parasite.

**Breakpoint hit:** When the parasite hits a breakpoint during
`TubeSocket::tick_parasite()`, the runner sets `paused_ = true` and
fires the breakpoint callback. The callback (via the existing
`wire_counterpart_stop` mechanism) pauses the host Machine. Both CPUs
stop.

#### Counterpart Stop Wiring

The existing counterpart stop mechanism in
`SecondProcessor65C02Extension` remains:

```cpp
// Host breakpoint → pause parasite
server.debugger_service().set_counterpart_stop_callback(
    [runner = runner_.get()] { runner->pause(); });

// Parasite breakpoint → pause host
tube_ext->wire_counterpart_stop([&machine] { machine.pause(); });
```

No changes needed here, since the callbacks are already simple
function calls.

### Phase 6: Update Tests

#### Tests to Remove

- `tests/test_tube_r3_race.cpp` -- Tests concurrent R3 race
  conditions with multiple threads. No longer applicable.

#### Tests to Update

- `tests/test_tube_inprocess.cpp` -- Uses ParasiteRunner with
  threading. Update to use single-threaded tick pattern.
- `tests/test_tube_extension.cpp` -- Tests extension lifecycle
  including thread start/stop. Update for non-threaded extension.
- `tests/test_boot_tube.cpp` -- Full boot integration test. Should
  work with minor updates if it currently relies on ParasiteRunner
  threading.

#### Tests Expected to Pass Unchanged

All register-level and protocol tests that call TubeUla and ParasiteCpu
from a single thread:
- `test_tube_ula.cpp`
- `test_tube_r1_6502.cpp`, `test_tube_r2_6502.cpp`
- `test_tube_r4_6502.cpp`
- `test_tube_pirq_6502.cpp`, `test_tube_control_6502.cpp`
- `test_tube_r3_nmi_6502.cpp`, `test_tube_r3_p2h_6502.cpp`
- `test_tube_ce2023_trace.cpp`
- `test_tube_socket.cpp`

### Phase 7: Integration Testing

The acid test is the scenario from GitHub issue #24:

```bash
cd integration_tests/wfsinit
uv run pytest -m slow tests/test_adfs_select_tube_hang.py -v -s
```

All 6 tests should pass, especially
`test_osword_72_then_adfs_select_with_tube` which currently deadlocks.

Also verify:
- Manual boot: `./beebium-model-b-romram --tube-65c02` shows "Acorn
  TUBE 6502 64K" banner
- `*ADFS` then `*CAT` works with Tube and SCSI
- WFSINIT full sequence (as described in issue #24)

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Modify TubeUla in-place (not new class) | Old threaded TubeUla is the broken code being replaced |
| Keep 2 MHz base clock | Avoids changing VIA/video/sound timing |
| Fractional accumulator for clock ratio | Simple, deterministic, proven pattern |
| Parasite ticks before host | B2 ordering; parasite writes visible to host same cycle |
| Deferred-write bus stretching | Cannot spin-wait in single thread; matches real HW semantics |
| ParasiteTickable interface | Keeps Machine template-generic; TubeSocket orchestrates |
| Debugger pause = skip ticking | Simple, correct; host continues and polls Tube status |

## Critical Files

| File | Phase | Change |
|------|-------|--------|
| `src/core/include/beebium/tube/TubeUla.hpp` | 1, 2 | Remove atomics, add stretch flag, simplify structs |
| `src/core/src/TubeUla.cpp` | 1, 2 | Plain member access, single update_interrupts(), deferred stretch |
| `src/core/include/beebium/tube/TubeSocket.hpp` | 3 | Add parasite runner, clock ratio, tick methods |
| `src/core/include/beebium/tube/ParasiteTickable.hpp` | 3 | New file: minimal tick interface |
| `src/core/include/beebium/Machine.hpp` | 3 | Add parasite ticking and Tube stretch to step() |
| `src/extensions/acorn-65c02-coprocessor/SecondProcessor65C02Extension.cpp` | 4 | Remove thread, install parasite |
| `src/extensions/acorn-65c02-coprocessor/SecondProcessor65C02Extension.hpp` | 4 | Remove thread members |
| `src/core/include/beebium/tube/ParasiteRunner.hpp` | 5 | Remove threading primitives, implement ParasiteTickable |
| `src/core/src/ParasiteRunner.cpp` | 5 | Simplify for single-threaded use |

## Risks and Mitigations

**Risk: Bus stretch timing accuracy.** The deferred-write mechanism
stores the pending write and retries each cycle. The host CPU's M6502
state machine stays at the same cycle point (the write instruction).
This matches real hardware where the CPU is clock-held mid-write.
Mitigation: verify with the existing CE2023 trace test which is
sensitive to exact cycle timing.

**Risk: Fractional clock ratio jitter.** The 3:2 accumulator produces
a 1-2-1-2 tick pattern, not a perfectly uniform 1.5. This is
acceptable because the real hardware also has jitter between
independent clock domains. Mitigation: verify with timing-sensitive
tests.

**Risk: Debugger independence.** The single-threaded model cannot
truly run one CPU while the other is stopped (both tick in the same
loop). Mitigation: pause-skip mechanism provides effective
independence for debugging. For stepping, both CPUs stop, which is the
normal debugging workflow.

**Risk: Test breakage.** Phases 1-2 change TubeUla internals that all
Tube tests depend on. Mitigation: phase 1 preserves semantic
behaviour; run the full test suite after each phase.
