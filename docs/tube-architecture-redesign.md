# Tube Architecture Redesign: From Dual-Process to Extension-Based

Date: 2026-04-07

## Motivation

The dual-process Tube architecture cannot meet R3 NMI transfer latency
requirements. The cross-process timing gap (1-2 ms pacing quanta vs 10-26 us
per-byte transfer windows) causes byte-doubling during SAVE operations and
cannot be fixed with atomics alone. See `tube-r3-pacing-investigation.md` for
the full analysis and five failed fix attempts.

The new architecture moves to single-process, dual-threaded operation, with
coprocessor second processors implemented as Peripheral Extension plugins. The
extension owns everything on the parasite side of the Tube cable -- including
the bridging hardware (Tube ULA for Acorn coprocessors; VIAs, PIAs, or other
bridging for third-party designs), the parasite CPU, memory map, and boot ROM.

## Design Principles

1. **The host sees only the Tube connector.** Eight bytes of I/O at &FEE0-&FEEF.
   What implements the other end is the extension's concern.

2. **The bridging hardware belongs to the extension.** Acorn used the Tube ULA;
   third parties used back-to-back VIAs, VIA+PIA combinations, etc. The
   extension instantiates and owns whatever bridging model it needs.

3. **The extension owns its thread.** The host run loop and parasite execution
   are fully asynchronous, synchronised only through the bridging hardware's
   register interface -- just like real hardware.

4. **`TubeHostBackend` is the contract.** The extension presents the host-facing
   register interface through this existing abstraction. The host's
   `TubeSocket` dispatches to it, exactly as it does today.

5. **Correctness first, then performance.** Get R3 transfers working correctly
   with a simple synchronisation model (mutex), then optimise if profiling
   shows it matters.

## Architecture Overview

```
Machine<Hardware>  (host thread)
    |
    +-- TubeSocket  (&FEE0-&FEEF, IrqBinding bit 2)
    |       |
    |       +-- TubeHostBackend*  (owned by extension)
    |
    +-- ExtensionRegistry
            |
            +-- SecondProcessor65C02Extension  (Peripheral Extension)
                    |
                    +-- owns TubeUla  (thread-safe bridging hardware)
                    +-- owns ParasiteRunner  (CPU, memory map, boot ROM)
                    +-- owns std::thread  (parasite execution)
                    +-- owns PacingClock  (3 MHz parasite pacing)
                    +-- provides gRPC DebuggerService  (parasite debugger)
                    +-- provides gRPC TubeService  (diagnostics)
```

The extension `attaches_to("tube")` and receives a reference to the host's
`TubeSocket` through the `ExtensionContext`. It plugs its `TubeUla` (via
`TubeHostBackend*`) into the socket. The host's memory map and IRQ aggregator
continue to work unchanged -- `TubeSocket::read/write` dispatches to the
backend, and `TubeSocket::irq_pending()` polls `backend_->hirq()`.

---

## Phase 0: R3 Status Flag Hysteresis Fix (independent, near-term)

**Goal:** Fix the R3 status register semantics in the cross-process model,
independent of the architecture redesign. This is a correctness bug that exists
today.

**Problem:** `TubeHostPort` and `TubeParasitePort` recompute R3 status from
`count >= threshold` on every read. In V=1 (two-byte) mode, after reading byte
1 of a pair (count 2 to 1), `count < threshold(2)` falsely indicates "space
available" / "no data". The real ULA and `TubeUla` maintain sticky status bits.

**Approach:** Pack `count` and `data_available` into a single
`std::atomic<uint16_t>` in `TubeReg3`. Use CAS to update both atomically.
Status reads extract both fields from a single atomic load.

**Files to modify:**
- `src/core/include/beebium/tube/TubeShared.hpp` -- `TubeReg3` struct
- `src/core/src/TubeHostPort.cpp` -- R3 status reads and `dequeue_r3_p2h`
- `src/core/src/TubeParasitePort.cpp` -- R3 status reads, `enqueue_r3_p2h`,
  `dequeue_r3_h2p`, `pnmi_level()`

**Verification:**
- All existing Tube register tests pass
- All existing Tube boot/CE2023 tests pass
- The `integration_tests/tube-save/` test (on the `l3fs-econet-and-tube-r3-fix`
  branch) should show improved status register behaviour, though it may still
  fail due to the timing gap (root cause 2)

**Note:** This fix has value regardless of the architecture direction. It makes
the cross-process model as correct as it can be for cases where it continues to
be used (e.g. if someone runs the old parasite executable manually).

---

## Phase 1: Thread-Safe TubeUla

**Goal:** Make `TubeUla` safe for concurrent access from two threads (host
thread calling `host_read`/`host_write`/`hirq`, extension thread calling
`parasite_read`/`parasite_write`/`pirq`/`pnmi`).

**Current state:** `TubeUla` uses plain `uint8_t`, `bool`, and array members
with no synchronisation. It is a single-threaded reference model. Its register
semantics (including R3 pending flags and PNMI edge detection) are correct.

### 1.1. Synchronisation model: mutex

Use a single `std::mutex` protecting all register state. Every public method
(`host_read`, `host_write`, `parasite_read`, `parasite_write`, `hirq`, `pirq`,
`pnmi`, `pnmi_level`, `reset`) acquires the lock.

**Rationale:** The register interface is not on the hot path in the way the CPU
or video subsystem is. A host-side Tube register access happens perhaps once
every few hundred CPU cycles (R2 polling loops, R3 NMI transfers). The parasite
side is similar. Even at R3 NMI transfer rates (~one access per 10 us emulated
time), that is ~100,000 accesses/second -- a mutex acquisition every 10 us is
negligible. The hot path is the CPU instruction loop itself, which does not
touch the mutex.

If profiling later shows contention, we can explore finer-grained locking or
lock-free techniques, but a mutex is the correct starting point.

### 1.2. Bus stretching model: spin-wait with yield

Replace the current `stretched()` / `pending_write_` / `complete_pending_write`
mechanism with a spin-wait inside `host_write`. When the host thread writes to
a full register (R1 H-to-P, R3 H-to-P, R4 H-to-P), it releases the mutex,
yields, re-acquires, and checks again. The extension's thread is running
concurrently and will drain the register.

```
host_write (offset, value):
    lock(mutex)
    if register is full:
        while register is full:
            unlock(mutex)
            yield()
            if bus_stretch_cancel:
                record pending write
                return
            lock(mutex)
    write value to register
    update interrupts
    unlock(mutex)
```

This replaces the current single-threaded cooperative pattern where the caller
manually steps the parasite after detecting `stretched()`. With concurrent
threads, the parasite drains the register independently.

The `bus_stretch_cancel` mechanism for debugger integration is preserved: the
debugger sets a flag, the spin-wait exits, and the write is deferred. On
resume, `complete_pending_write()` retries.

### 1.3. Interrupt output thread safety

`hirq()`, `pirq()`, `pnmi()`, and `pnmi_level()` must be safe to call from
their respective threads. Since they read cached flags that are updated under
the mutex in `update_interrupts()`, they can either:

- Acquire the mutex (simple, low contention), or
- Read `std::atomic<bool>` cached values updated under the mutex (avoids lock
  acquisition on the polling path)

The second option is preferable for `hirq()` since the host's `IrqAggregator`
polls it every cycle. Making `hirq_`, `pirq_`, `pnmi_level_`, and `pnmi_edge_`
atomic booleans and storing them under the mutex after `update_interrupts()` is
straightforward.

### 1.4. `peek` methods

`host_peek` and `parasite_peek` are used by the debugger for side-effect-free
inspection. They should acquire the mutex (shared lock if using
`std::shared_mutex`, exclusive otherwise). Since debugging is not
performance-critical, the simplest approach is exclusive lock.

### 1.5. Files to modify

- `src/core/include/beebium/tube/TubeUla.hpp` -- add mutex, make interrupt
  outputs atomic, adjust `stretched()` interface
- `src/core/src/TubeUla.cpp` -- add lock acquisition to all public methods,
  replace `stretched()`/`pending_write_` with spin-wait

### 1.6. Verification

- All existing `test_tube_ula.cpp` tests pass (they call from a single thread,
  so the mutex is uncontended)
- All existing `test_tube_end_to_end.cpp` tests pass
- New test: concurrent host and parasite register access stress test
- New test: R3 NMI transfer with concurrent threads (verifies no byte doubling)

---

## Phase 2: Tube Extension Point

**Goal:** Register "tube" as an extension point in the framework and make
`TubeSocket` accessible to extensions through `ExtensionContext`.

### 2.1. Register the extension point

In `ServerMain`, alongside the existing extension points:

```cpp
extension_registry.register_extension_point("1mhz-bus");
extension_registry.register_extension_point("user-port");
extension_registry.register_extension_point("tube");       // NEW
```

### 2.2. Expose TubeSocket through ExtensionContext

`ExtensionContext` currently provides access to `OneMHzBusPort` and `UserPort`.
Add a `TubeSocket*` member:

```cpp
class ExtensionContext {
    OneMHzBusPort* one_mhz_bus_;
    UserPort* user_port_;
    TubeSocket* tube_socket_;      // NEW
public:
    // ...
    TubeSocket* tube_socket() const { return tube_socket_; }
};
```

The `TubeSocket` is obtained from `machine.state().memory.tube_socket` (or
equivalent accessor on the hardware policy).

### 2.3. TubeSocket backend installation

Add a method to `TubeSocket` for extensions to install their backend:

```cpp
void install_backend(std::unique_ptr<TubeHostBackend> backend) {
    backend_ = std::move(backend);
}
```

This replaces the current `enable()` / `enable(TubeShared*)` methods, which
were specific to the old in-process and cross-process models. The extension
creates its own `TubeUla` (or other backend) and installs it.

Alternatively, since the extension owns the backend's lifetime and the backend
must outlive the socket, use a non-owning pointer:

```cpp
void install_backend(TubeHostBackend* backend) {
    backend_ = backend;   // non-owning; extension owns the TubeUla
    owns_backend_ = false;
}
```

This avoids ownership ambiguity. The extension is responsible for keeping the
backend alive while installed.

### 2.4. Files to modify

- `src/core/include/beebium/extension/ExtensionContext.hpp` -- add TubeSocket
- `src/core/include/beebium/tube/TubeSocket.hpp` -- add `install_backend`
- `src/server/include/beebium/server/ServerMain.hpp` -- register "tube"
  extension point, pass TubeSocket to ExtensionContext

### 2.5. Verification

- Existing extension tests pass
- New test: extension can install a mock TubeHostBackend and receive
  host_read/host_write calls

---

## Phase 3: 65C02 Second Processor Extension

**Goal:** Implement the Acorn 65C02 3 MHz second processor as a Peripheral
Extension plugin.

### 3.1. Extension class

```cpp
class SecondProcessor65C02Extension : public PeripheralExtension {
    std::unique_ptr<TubeUla> tube_ula_;
    std::unique_ptr<ParasiteRunner> runner_;
    std::unique_ptr<PacingClock> pacing_clock_;
    std::thread parasite_thread_;
    std::atomic<bool> running_{false};

    // gRPC services
    std::unique_ptr<DebuggerServiceImpl> debugger_service_;
    std::unique_ptr<TubeServiceImpl> tube_service_;

public:
    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"tube"};
        return deps;
    }

    std::span<const std::string_view> provides() const override {
        return {};  // Terminal extension, doesn't provide extension points
    }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;
    std::vector<grpc::Service*> grpc_services() override;
};
```

### 3.2. Manifest

```json
{
    "name": "acorn-65c02-coprocessor",
    "description": "Acorn 65C02 3 MHz second processor",
    "library": "acorn-65c02-coprocessor",
    "cli": "tube-65c02",
    "parameters": [
        {
            "key": "rom",
            "type": "filepath",
            "description": "Path to 2KB Tube client ROM image",
            "required": false
        },
        {
            "key": "clock-mhz",
            "type": "integer",
            "default": "3",
            "description": "Parasite clock speed in MHz (3 or 4)"
        }
    ]
}
```

### 3.3. init() sequence

1. Load the Tube client ROM (from explicit path, or discover in standard
   locations)
2. Create `TubeUla` (thread-safe, from Phase 1)
3. Create `ParasiteRunner` with a reference to `TubeUla`'s parasite-side
   interface (see Phase 3.4)
4. Install `TubeUla` as the backend in `ctx.tube_socket()`
5. Create gRPC services (DebuggerService for parasite, TubeService for
   diagnostics)
6. Start the parasite thread

### 3.4. ParasiteRunner adaptation

`ParasiteRunner` currently takes a `TubeShared*` and creates its own
`TubeParasitePort`. It needs to be adapted to work with `TubeUla` instead:

**Option A: Adapt ParasiteRunner to accept a parasite-side interface**

Define a concept or interface for the parasite's view of the Tube registers:

```cpp
// Concept for the parasite side of a Tube bridge
template <typename T>
concept TubeParasiteInterface = requires(T& t, uint8_t offset, uint8_t value) {
    { t.parasite_read(offset) } -> std::same_as<uint8_t>;
    { t.parasite_peek(offset) } -> std::same_as<uint8_t>;
    { t.parasite_write(offset, value) } -> std::same_as<void>;
    { t.pirq() } -> std::same_as<bool>;
    { t.pnmi_level() } -> std::same_as<bool>;
};
```

Both `TubeUla` and `TubeParasitePort` already satisfy this interface. Template
`ParasiteRunner` on the parasite interface, or use runtime polymorphism with a
new `TubeParasiteBackend` abstract class.

**Option B: Template ParasiteRunner**

```cpp
template <typename ParasitePort>
class ParasiteRunner {
    ParasitePort& tube_port_;
    // ...
};
```

This avoids virtual dispatch on every register access. The extension
instantiates `ParasiteRunner<TubeUla>` and passes a reference to its `TubeUla`.

**Recommendation:** Option B (template) for performance. The parasite accesses
Tube registers in tight NMI handlers; virtual dispatch overhead is
disproportionate there.

### 3.5. ParasiteMemoryMap adaptation

`ParasiteMemoryMap` currently takes a `TubeParasitePort&` and routes addresses
&FEF8-&FEFF to it. It needs to accept whatever parasite-side interface the
runner uses. Template it on the same `TubeParasiteInterface` concept, or pass
a reference to the same type.

### 3.6. Thread lifecycle

The extension starts its thread in `init()` and stops it in `shutdown()`:

```cpp
void init(ExtensionContext& ctx) override {
    // ... create TubeUla, ParasiteRunner, PacingClock ...
    ctx.tube_socket()->install_backend(tube_ula_.get());
    running_.store(true);
    parasite_thread_ = std::thread([this] { run_parasite(); });
}

void shutdown() override {
    running_.store(false);
    runner_->request_shutdown();
    if (parasite_thread_.joinable())
        parasite_thread_.join();
    // Unplug from TubeSocket (install EmptyTubeBackend or nullptr)
}

void run_parasite() {
    runner_->reset();
    while (running_.load(std::memory_order_acquire)) {
        auto budget = pacing_clock_->wait_for_budget();
        runner_->run(budget);
    }
}
```

### 3.7. Debugger integration

The extension provides a `DebuggerServiceImpl` for the parasite, with the same
RPC interface as the host debugger but operating on `ParasiteRunner` instead of
`Machine`. Key differences from the cross-process model:

- No shared memory lifecycle mailbox needed (direct method calls on
  `ParasiteRunner`)
- Pause/resume: `runner_->pause()` / `runner_->resume()`
- Cross-processor stop: when a breakpoint fires on one side, both sides need to
  stop. The extension can directly call the host Machine's pause, and vice
  versa. No `debugger_stop_signal` atomic in shared memory needed.

### 3.8. Files to create

- `src/extensions/acorn-65c02-coprocessor/SecondProcessor65C02Extension.hpp`
- `src/extensions/acorn-65c02-coprocessor/SecondProcessor65C02Extension.cpp`
- `src/extensions/acorn-65c02-coprocessor/manifest.json`
- `src/extensions/acorn-65c02-coprocessor/CMakeLists.txt`

### 3.9. Files to modify

- `src/core/include/beebium/tube/ParasiteRunner.hpp` -- template on parasite
  port type, or add a concept-based interface
- `src/core/include/beebium/tube/ParasiteMemoryMap.hpp` -- same adaptation
- `src/core/include/beebium/tube/ParasiteCpu.hpp` -- adapt to new port type
- `CMakeLists.txt` -- add extension build target

### 3.10. Verification

- Boot test: `test_boot_tube.cpp` adapted to use the extension instead of
  shared memory
- CE2023 test: `test_tube_ce2023_trace.cpp` adapted for in-process model
- R3 SAVE test: `integration_tests/tube-save/` should now pass
- All existing Tube register protocol tests adapted
- Extension lifecycle test: install, boot, shutdown, reinstall

---

## Phase 4: Remove Cross-Process Infrastructure

**Goal:** Remove the now-superseded dual-process infrastructure.

### 4.1. Code to remove or deprecate

- `src/core/include/beebium/tube/TubeShared.hpp` -- remove
- `src/core/include/beebium/tube/TubeSharedMemory.hpp` -- remove
- `src/core/src/TubeSharedMemory.cpp` -- remove
- `src/core/include/beebium/tube/TubeHostPort.hpp` -- remove
- `src/core/src/TubeHostPort.cpp` -- remove
- `src/core/include/beebium/tube/TubeParasitePort.hpp` -- remove
- `src/core/src/TubeParasitePort.cpp` -- remove
- `src/server/main_tube_65C02_3MHz.cpp` -- remove (the standalone parasite
  executable)
- `TubeSocket::enable(TubeShared*)` -- remove this overload
- `TubeSocket::enable()` -- remove (extensions use `install_backend`)

### 4.2. Code to keep

- `TubeUla` -- now the canonical Tube bridge implementation (thread-safe)
- `TubeHostBackend` -- the extension contract interface
- `TubeSocket` -- the host-side connector (dispatches to backend)
- `ParasiteRunner` -- the parasite execution engine (now templated)
- `ParasiteMemoryMap`, `ParasiteCpu` -- parasite CPU infrastructure
- `EmptyTubeBackend` -- for when no coprocessor is attached

### 4.3. Machine.hpp cleanup

Remove Tube-specific shared-memory code from `Machine`:
- `set_tube_shared(TubeShared*)` method
- `tube_shared_` member pointer
- References to `bus_stretch_cancel` and `debugger_stop_signal` in shared
  memory (these become direct method calls through the extension)

### 4.4. ServerMain cleanup

Remove:
- Shared memory creation/mapping for Tube
- Parasite process spawning (`fork`/`posix_spawn` of
  `beebium-tube-65C02-3MHz`)
- Parasite process lifecycle management (wait for connection, PID tracking)
- The `--tube` CLI argument (replaced by `--tube-65c02` extension argument)

### 4.5. Test migration

Tests that use `TubeShared` / `TubeHostPort` / `TubeParasitePort` directly need
updating:

- `test_tube_host_port.cpp` -- remove or convert to `TubeUla` tests
- `test_tube_parasite_port.cpp` -- remove or convert
- `test_tube_shared_threads.cpp` -- remove (cross-process synchronisation
  tests are no longer relevant)
- `test_tube_end_to_end.cpp` -- adapt to use extension-based setup
- `test_grpc_tube.cpp` -- adapt to use extension's TubeService

Tests that use `TubeUla` directly continue to work unchanged.

### 4.6. Verification

- Full test suite passes
- No references to removed files in CMakeLists.txt
- Clean build on all platforms (macOS, Windows, Linux)
- `beebium-tube-65C02-3MHz` executable no longer built

---

## Phase 5: Integration and Polish

**Goal:** Ensure the new architecture works end-to-end with the macOS frontend
and handles all edge cases.

### 5.1. macOS frontend adaptation

The Swift frontend currently connects to a `TubeService` gRPC endpoint. With
the extension model, the TubeService is now provided by the extension and
registered with the host's gRPC server. The frontend should not need changes
if the service proto and RPCs remain the same.

Verify:
- Tube status display works
- Parasite debugger connects and can set breakpoints
- Boot with Tube-equipped configuration

### 5.2. CLI argument migration

Old: `--tube 65C02-3MHz`
New: `--tube-65c02` (or `--tube-65c02 --tube-65c02.rom /path/to/rom`)

The extension framework's argument parsing handles this. The extension manifest
defines the `cli` key and parameters.

### 5.3. Cross-processor debugger breakpoints

With both processors in the same process, cross-processor stop becomes simpler:

- When a breakpoint fires on the host, the extension's `ParasiteRunner` is
  paused directly
- When a breakpoint fires on the parasite, the host `Machine` is paused
  directly
- No `debugger_stop_signal` atomic in shared memory needed
- The DebuggerService can coordinate both via a shared "stop coordinator"
  object

### 5.4. Orphaned process cleanup

The orphaned parasite process problem (`project_orphaned_tube_processes.md`)
disappears entirely -- there is no parasite process.

### 5.5. Pacing interaction

The extension's thread has its own `PacingClock` at the parasite's clock speed
(3 MHz for the standard 65C02). The host's pacing is unchanged. The two pacing
clocks are independent, just like the real hardware's independent oscillators.

The key difference from the old dual-process model: both threads share the same
OS process, so `io_pending` wakeup is no longer needed. The `TubeUla`'s mutex
ensures that register state is immediately visible across threads. When the
parasite writes a byte, the host sees it on its next register access -- no
pacing quantum delay.

### 5.6. Verification

- End-to-end boot and BASIC prompt with Tube
- SAVE/LOAD files over DFS with Tube active (the bug that started this)
- CE2023 loads and runs
- Debugger: set breakpoint in parasite, hit it, inspect state, continue
- Debugger: set breakpoint in host, verify parasite also stops
- Performance: release build meets 2 MHz host / 3 MHz parasite pacing targets
- CPU usage: should be lower than dual-process (no shared-memory polling, no
  io_pending wakeup)

---

## Migration Strategy

The phases are designed to be independently testable and committable:

- **Phase 0** is a standalone bug fix on the existing architecture. It can be
  merged to master immediately.
- **Phase 1** modifies `TubeUla` in a backward-compatible way. Single-threaded
  callers are unaffected (the mutex is uncontended). It can be merged to master
  independently.
- **Phase 2** adds the extension point without changing existing behaviour. No
  extension uses it yet. Can be merged independently.
- **Phase 3** adds the new extension. At this point both the old (dual-process)
  and new (extension) paths exist. The old `--tube` flag continues to work.
  Testing can compare both paths.
- **Phase 4** removes the old path. This is the irreversible step. Only do this
  after Phase 3 is thoroughly tested.
- **Phase 5** is polish and integration testing.

Phases 0-2 can proceed in parallel. Phase 3 depends on 1 and 2. Phase 4
depends on 3. Phase 5 depends on 4.

---

## Risk Assessment

**Low risk:**
- Phase 0 (packed atomic) -- well-understood change, good test coverage
- Phase 2 (extension point) -- additive change, no behaviour modification
- Phase 4 (removal) -- straightforward deletion after Phase 3 proves out

**Medium risk:**
- Phase 1 (thread-safe TubeUla) -- mutex adds complexity to a previously
  simple class. The spin-wait bus stretching model needs careful testing for
  livelock (parasite thread must make progress to drain the register).
  Interrupt output atomics need correct memory ordering.
- Phase 5 (integration) -- cross-processor debugger coordination is a new
  pattern. Frontend adaptation may surface unexpected assumptions.

**Higher risk:**
- Phase 3 (extension implementation) -- the largest phase. Templating
  `ParasiteRunner` on the port type is a significant refactor. The thread
  lifecycle (start, pause, resume, shutdown, restart) has many edge cases.
  The gRPC service registration timing (extension init happens before server
  start) needs to align with the existing startup sequence.

---

## Open Questions

1. **Should `TubeUla` use `std::mutex` or `std::shared_mutex`?** Status reads
   (`host_read` even offsets) are much more frequent than data reads/writes
   (odd offsets). A shared_mutex would allow concurrent status reads. But the
   overhead of shared_mutex on macOS (pthread_rwlock) may negate the benefit
   for such short critical sections. Start with `std::mutex`, measure later.

2. **Should the extension be a dynamic plugin (.dylib/.dll/.so) or statically
   linked?** The SCSI extensions are dynamically loaded. For the first Tube
   extension, static linking may be simpler. Dynamic loading can come later
   when the interface is stable.

3. **How does the extension discover the ROM?** The current parasite executable
   searches sibling directories and share paths. The extension could use the
   same discovery logic, or the ROM path could be a required manifest
   parameter. The latter is more explicit but less convenient.

4. **Should `ParasiteRunner` be templated or use runtime polymorphism for the
   parasite port?** Templates avoid virtual dispatch in the NMI handler hot
   path but make the class harder to test in isolation. A type-erased wrapper
   (like `TubeSocket`'s `std::unique_ptr<TubeHostBackend>`) is an alternative.

5. **What happens to the integration tests?** The TypeScript integration tests
   in `clients/python/` and `integration_tests/` launch the server as a
   subprocess and interact via gRPC. The `--tube` flag needs to be replaced
   with the extension argument. The orphaned-process problem disappears.
