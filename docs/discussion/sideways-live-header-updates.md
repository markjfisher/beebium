# Live Header Updates for Sideways Slots

Status: proposed (not yet implemented)

## Why

The Memory sidebar exists to tell the user, truthfully, what is currently in
each sideways slot of the running BBC. Today it lies: the displayed slot name
is captured at load time (preset boot or `ConfigureSlot`) and never revisited.
If something inside the emulated machine changes a slot's contents -- the
canonical case is `*SRLOAD` writing a new ROM image into sideways RAM, but any
CPU-driven write to a RAM slot qualifies -- the sidebar keeps showing the old
title, the old version, the old kinds (language / service / romfs). The user
believes they are looking at one ROM and they are actually looking at another.

This proposal is the cheapest scheme that keeps the displayed information
honest, with eventual consistency on the order of a second.

## Goals

- Sidebar reflects the *current* parsed ROM header for every populated slot.
- Eventually consistent within ~1 s of the last write; not cycle-accurate.
- Server pays no cost when no client is watching.
- Mechanism works for every gRPC client (macOS, Python, TypeScript, future
  debugger) without being tied to any of their UI specifics.

## Non-goals

- Per-byte change notifications. A debugger that wants those will get its own
  memory-watch RPC.
- Detecting writes to ROM-typed slots. ROM bytes never change between
  `ConfigureSlot` calls, which already emit `SlotConfiguredEvent`.
- Driving updates from the 6502 write path. We don't instrument hot loops.

## Mechanism

### Wire change: one new event, one new request flag

`SidewaysEvent` today defines `SlotConfiguredEvent` (declared in the proto
but no server code emits it yet -- a separate piece of work). Add a second
variant for the live header signal:

```proto
message SidewaysEvent {
    // Tag 3 (BankSelectedEvent) is reserved -- the previous ROMSEL-delta
    // poller was removed because the underlying state turns over too fast
    // for a sampled stream to carry useful information.
    reserved 3;
    reserved "bank_selected";
    oneof event {
        SlotConfiguredEvent slot_configured = 2;
        SlotHeaderChangedEvent slot_header_changed = 4;
    }
}

message SlotHeaderChangedEvent {
    uint32 slot = 1;        // Slot whose header parse just changed
    RomHeader rom_header = 2;  // Same shape as SocketStatus.rom_header
}
```

Extend `SubscribeEventsRequest` with an opt-in flag:

```proto
message SubscribeEventsRequest {
    uint32 min_interval_ms = 1;
    // Default off: emit SlotHeaderChangedEvent at ~1 Hz while RAM-slot
    // header bytes change. Costs the server a periodic rescan, so opt in
    // only while you actually need live updates and drop the subscription
    // (or restart it without this flag) when you don't.
    bool monitor_header_changes = 2;
}
```

`SlotConfiguredEvent` keeps flowing unconditionally on every stream (once
its emitter is wired up). `SubscribeEvents` is today a block-until-cancelled
stream with no live emitters; this work makes it carry real events.

### Server-side: refcounted 1 Hz scanner

`SidewaysService` keeps an integer count of currently-open `SubscribeEvents`
streams that requested `monitor_header_changes = true`. The count is the
authoritative "is anyone watching" signal; no separate mechanism is needed.

While that count is >= 1, a single 1 Hz timer runs (one shared scanner, not
one per subscriber). On each tick:

1. For each socket / slot whose live `type()` is RAM, peek the bounded header
   region: the fixed bytes at `&8000..&800F` plus the copyright string
   addressed via `&8007`. Hash those bytes.
2. If the hash matches the last-seen hash for that slot, do nothing further
   for that slot.
3. Otherwise re-parse with `parse_sideways_rom_header()` over the full 16 KiB
   bank (cheap; needed for ROMFS detection). Compare the resulting `RomHeader`
   to the last-emitted one for that slot.
4. If the parse result differs, broadcast `SlotHeaderChangedEvent` to every
   open stream that has `monitor_header_changes = true`.

When the refcount drops to 0 the timer is cancelled. The scanner does no work
between visits, and a machine with no RAM slots (e.g. Model B+ in its standard
configuration) does no work even when watched.

Threading contract: `peek_bank` is already callable off the CPU thread.
Readers tolerate transient torn reads -- a hash mismatch caused by sampling
mid-write either resolves on the next tick (if writes have stopped) or keeps
producing fresh events at ~1 Hz (if writes continue). No mutex.

ROM-typed slots are not scanned. ROM bytes don't change between
`ConfigureSlot` calls, and `SlotConfiguredEvent` already covers those.

### Client policy

The policy is the same regardless of language: **open a `SubscribeEvents`
stream with `monitor_header_changes = true` only while you actually care, and
drop it as soon as you don't.** That string is the load-bearing piece of
documentation on the proto field.

How each client cashes that out today:

- **macOS sidebar.** `SidewaysClient.connect()` currently subscribes once at
  machine-connect time. Change it to subscribe lazily: `MemoryModeView`
  drives `subscribe()` in `onAppear` and `unsubscribe()` in `onDisappear`. On
  every re-subscribe the client also re-runs `GetSlotStatus` once so the view
  is correct after any gap. The other event types are folded in -- if the
  sidebar isn't visible, ConfigureSlot events are missed for the duration,
  but the re-fetch on next-visible covers them.
- **Python automation.** Most scripts call `GetSlotStatus` once and never
  subscribe. A long-running script that wants to observe `*SRLOAD` (e.g. a
  ROM-loader test harness) subscribes with the flag on for the test, then
  cancels.
- **TypeScript.** Same shape as Python or Swift depending on whether the
  consumer is a script or a UI.
- **Future debugger.** Typically subscribes with the flag *off*. The previous
  `BankSelectedEvent` that might have served breakpoint logic was removed
  (too lossy to be useful); when a debugger really needs to track bank
  selection it will need a different mechanism, not this stream. Flips the
  flag on only if it ever surfaces a live memory inspector.

The server doesn't know which kind of client is on the other end. It only
knows how many open streams asked for header monitoring.

## Why not...

- **A separate `WatchSlotHeaders` RPC.** Forces clients that want both
  `ConfigureSlot` and header changes to multiplex two streams, for no
  decoupling benefit. The events are already one oneof.
- **Always-on monitoring.** Cheap doesn't mean free, and a machine that nobody
  is looking at shouldn't be doing arithmetic on its banks every second.
- **A ROMSEL-write trigger.** Partial signal even if implemented as a real
  write hook: `*SRLOAD` doesn't have to flip ROMSEL during the write loop, so
  a ROMSEL-only design would miss steady writes. (A previous ROMSEL-delta
  *poller* existed and was removed for similar reasons -- ROMSEL turns over
  thousands of times per second during OS scans and the sampled stream was
  carrying no observational value. Any future ROMSEL trigger would need a
  real write hook, not a poll.) A 1 Hz periodic scan over the (small) set of
  RAM slots is so cheap that ROMSEL adds complexity without payoff. Revisit
  only if 1 Hz feels laggy in practice.
- **Per-slot subscription** (`watch_slots = [4, 12]`). Whole-machine
  monitoring is bounded by RAM-slot count, which is at most 16. Filtering
  client-side is fine.
- **Per-subscriber rate.** Hardcode 1000 ms. Expose a `interval_ms` field
  only if a real consumer asks for it.

## Risks and open questions

- **Mid-write samples.** During a `*SRLOAD`, the header bytes are partially
  written for a short window and the parse result is briefly nonsense
  (`recognised = false`, or wrong title). At 1 Hz this could mean one or two
  ticks of "wrong" before convergence. Acceptable; documented as eventual
  consistency.
- **Event volume during sustained writes.** A pathological loop that keeps
  changing the header region every tick produces ~1 event/s per affected
  slot. That's fine on the wire but worth knowing. If it ever matters, add
  an internal cooldown (e.g. coalesce to one event per slot per 2 s).
- **Hash quality.** A trivial XOR digest would let some byte permutations
  alias. Use a real 32-bit hash (FNV-1a or xxhash32 over <=300 bytes); the
  cost is negligible and the false-negative case is "we miss a change for
  one second" which is unrecoverable in any design without per-byte hooks.
- **Model B+ and similar fixed-ROM machines.** No RAM-typed slots in standard
  configurations, so the scanner has nothing to do. Verify that the iteration
  cleanly produces zero work rather than failing some constexpr branch.

## Integration test: prove the lie stops

The feature succeeds when, after the emulated BBC writes a new ROM image into
a sideways RAM slot at runtime, the gRPC stream emits a `SlotHeaderChangedEvent`
whose `rom_header.title` matches the freshly-loaded ROM. The most realistic way
to drive that on an emulated machine is to use the BBC's own `*SRLOAD` command,
which is exactly the user-facing path that motivated this work.

### Why model-b-plus

The `acorn-dfs_2_26.rom` shipped with the model-b-plus preset (at slot 11)
includes the SRAM utilities -- `*SRLOAD`, `*SRWRITE`, `*SRSAVE`, `*SRDATA`,
`*SRROM`, `*SRREAD` -- which let the emulated machine write a file from disc
into a sideways RAM bank directly. `*SRLOAD R.ANFS 8000 7 Q` loads the file
`R.ANFS` from the mounted DFS disc into bank 7 starting at &8000, using the
quick (memory-corrupting) transfer. (`R.` is a conventional prefix for ROM-image
files in DFS.) See
`acornaeology/library/books/advanced_sideways_ram_user_guide/notes/chapter_3_service_roms.md`
for the user-guide notes.

We need slot 7 to be configured as RAM at boot. The standard model-b-plus
preset has slot 7 as ROM (one of the user ROM sockets), so the test launches
the server with `--sideways 7:ram` to override.

### Shape of the test

This belongs alongside the other heavyweight, disc-building integration
suites under `integration_tests/`, not in `clients/python/tests/`. Suggested
location: `integration_tests/sideways-srload/`, with its own
`pyproject.toml` depending on `beebium`, `oaknut-dfs`, and `pytest` (same
pattern as `integration_tests/tube-save/`).

The disc-building side already has clean precedent: `oaknut-dfs` is on PyPI,
`oaknut.dfs.DFS` writes SSDs directly in Python (see
`integration_tests/adfs/src/adfs_test_support/disc_builder.py` and
`integration_tests/tube-save/tests/conftest.py`), and the keyboard helpers
needed to drive the BBC are already used by those suites.

Test flow:

1. **Build an SSD in a fixture.** Use `oaknut.dfs.DFS` to create a
   40-track single-sided DFS image holding one file `R.ANFS` whose contents
   are the bytes of `roms/acorn-anfs_4_18.rom`. Write to a tmp path.
2. **Launch model-b-plus with the disc mounted and slot 7 forced to RAM.**
   Extra args: `--fdc acorn-1770`, `--disc 0:<ssd_path>`,
   `--sideways 7:ram`. (The stock model-b-plus preset has slot 7 as ROM;
   the override puts RAM there so `*SRLOAD ... 7` has somewhere to land.)
3. **Wait for the boot prompt.** Same pattern as the tube-save suite:
   `bbc.run_until_or_timeout` against a screen-contains predicate.
4. **Confirm slot 7 is empty / unrecognised.** Call `GetSlotStatus`, find
   the socket whose `aliased_slots` contains 7, assert `type=RAM` and
   `rom_header` either absent or `recognised=False`.
5. **Subscribe to events with `monitor_header_changes=True`.** Drain the
   stream into a thread-safe queue on a worker thread.
6. **Type the command.** `bbc.keyboard.type("*SRLOAD R.ANFS 8000 7 Q")` +
   `bbc.keyboard.press_return()`.
7. **Wait up to ~5 emulated seconds for an event matching slot 7** whose
   `rom_header.title` equals what `parse_sideways_rom_header` produces from
   `acorn-anfs_4_18.rom` (parse the ROM in the test rather than hardcoding
   the title, so the test survives a ROM-version bump).
8. **Sanity-check via `GetSlotStatus`** that the post-state agrees with
   the live event.

This test is the strongest possible refutation of the original "sidebar
lies" failure mode: a real OS, a real `*SRLOAD`, a real disc, and the gRPC
contract observed end-to-end.

### Prerequisites the test depends on

- **Python `SidewaysClient`.** `clients/python/src/beebium/_proto/` has no
  `sideways_pb2.py` today; the Sideways service has never been wrapped in
  Python. The wrapper has to land before (or alongside) this test. Following
  the pattern of `econet_transport.py` / `aun.py`, expose `get_slot_status()`,
  `configure_slot()`, and a `subscribe_events(monitor_header_changes=...)`
  generator. The proto file should be added to whatever build step generates
  the other `_pb2.py` modules.
- **`oaknut-dfs` test dep** in the new
  `integration_tests/sideways-srload/pyproject.toml`. No new tooling beyond
  what `tube-save` and `adfs` already pull in.
- **Slot-7-as-RAM override.** Just a CLI arg at server launch; no code work.

### Cost / pace

The 1 Hz scanner means the event arrives within at most one second of the
`*SRLOAD` completing; `*SRLOAD ... Q` itself takes well under a second for
a 16 KiB file. A 5 s emulated-time timeout is generous. Total test
wall-clock is dominated by server startup and disc mount (~2-3 s), not the
feature.

## Future extensions

- ROMSEL write as an event accelerator: hook the actual write path
  (not a poll) and, when the OS flips ROMSEL away from a RAM slot,
  prioritise that slot in the next tick. Same protocol; pure server-side
  optimisation.
- `interval_ms` field on `SubscribeEventsRequest` if a slow client wants
  fewer events.
- Generalise to other live state that nobody currently asks for live (banked
  memory in non-sideways regions, paged RAM, etc.). Same flag pattern.
