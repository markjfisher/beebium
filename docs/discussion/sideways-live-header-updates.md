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

Extend the existing `SidewaysEvent` oneof with a third variant:

```proto
message SidewaysEvent {
    oneof event {
        SlotConfiguredEvent slot_configured = 1;
        BankSelectedEvent bank_selected = 2;
        SlotHeaderChangedEvent slot_header_changed = 3;
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

`SlotConfiguredEvent` and `BankSelectedEvent` keep flowing unconditionally on
every stream, as today.

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
- **Future debugger.** Typically subscribes with the flag *off* -- it wants
  `BankSelectedEvent` for breakpoint logic, not slot headers. It flips the
  flag on only if it ever surfaces a live memory inspector.

The server doesn't know which kind of client is on the other end. It only
knows how many open streams asked for header monitoring.

## Why not...

- **A separate `WatchSlotHeaders` RPC.** Forces clients that want both
  `ConfigureSlot` and header changes to multiplex two streams, for no
  decoupling benefit. The events are already one oneof.
- **Always-on monitoring.** Cheap doesn't mean free, and a machine that nobody
  is looking at shouldn't be doing arithmetic on its banks every second.
- **A ROMSEL-write trigger.** Partial signal: `*SRLOAD` doesn't have to flip
  ROMSEL during the write loop, so a ROMSEL-only design would miss steady
  writes. A 1 Hz periodic scan over the (small) set of RAM slots is so cheap
  that ROMSEL adds complexity without payoff. Revisit only if 1 Hz feels
  laggy in practice.
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

## Future extensions

- ROMSEL write as an event accelerator: when the OS flips ROMSEL away from a
  RAM slot, prioritise that slot in the next tick. Same protocol; pure
  server-side optimisation.
- `interval_ms` field on `SubscribeEventsRequest` if a slow client wants
  fewer events.
- Generalise to other live state that nobody currently asks for live (banked
  memory in non-sideways regions, paged RAM, etc.). Same flag pattern.
