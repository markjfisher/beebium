# Serial subsystem: architecture review and design direction

Status: working notes, on the `feature/serial` integration branch.
Context: PR #44 (Mark Fisher, `feature/mc6850`) adds an MC6850 ACIA + Serial ULA
(SERPROC) plus a host-side serial transport. We have accepted it onto
`feature/serial` (merged with `--no-ff`, preserving the contributor's history)
and intend to evolve it toward what we want before it reaches `master`.

These notes capture (a) what the PR does well, (b) two corrections/decisions
that change the design, and (c) the remaining gaps and a plan. They exist so we
can deal with the issues systematically rather than from memory.

## What the PR gets right

- The emulated chips (MC6850 ACIA, Serial ULA / SERPROC) live in core and
  mirror the `EconetSocket` pattern closely: memory-mapped regions
  (`&FE08-&FE0F` ACIA, `&FE10-&FE17` ULA), `tick_rising`/`tick_falling`,
  IRQ-aggregator binding, hard/soft reset, open-bus pointer. The ACIA/ULA are
  integral host-side hardware, so modelling them in core (not the extension
  framework) is correct, consistent with the MC6854 ADLC.
- A clean byte-level transport seam already exists:
  `SerialDataSource` / `SerialDataSink` (see `serial/SerialDevice.hpp`). This is
  the seam we want; the disagreement below is only about *what plugs into it*
  and *where that lives*.
- Faithful bit-level timing at an emulator-friendly cadence (`CPU_HZ/baud`
  ticks per bit), honestly documented as not cycle-accurate to the 16x sampling
  clock - consistent with our MC6854 rationale.
- Good test coverage (Catch2 for ACIA/ULA/socket/PTY; Python unit +
  integration). Builds and passes on macOS including the PTY round trip, so the
  POSIX transport is already Mac-clean.

## Correction 1: the serial port is NOT always fitted

The PR (and my first-pass review) assumed the serial socket is always present.
That is wrong:

- Model A: no serial hardware at all (not yet implemented in Beebium).
- Master Compact: serial is optional (not yet implemented).
- Model B / B+ / Master 128: fitted.

Design implications:

- Presence must be a per-variant property, not a constant. The
  `HasSerialSocket<Memory>` concept the PR already uses is the right mechanism -
  we must make sure every variant opts in/out correctly, and that the Compact
  (when it arrives) can express "fitted or not" as a configuration choice, not a
  compile-time fact baked into the variant.
- The whole stack above core must tolerate absence gracefully:
  - `SerialService.GetSerialStatus` already returns `has_serial_socket=false` -
    good; keep that contract and make the clients honour it.
  - Any future UI (panel/indicator) must hide or disable itself when the
    machine has no serial port, the same way Econet UI keys off station config.
  - `--serial` on a machine with no serial port should be a clear error, not a
    silent no-op.
- This also weakens the main argument for keeping serial out of the extension
  framework ("it's always there, so it's not pluggable"). It is in fact optional
  hardware on some variants - closer to a fitted option than to RAM.

## Correction 2: the host-serial bridge belongs in an extension

The PR bakes the *host transport* into core: `PtyMaster`, `HostSerialEndpoint`,
`PosixSerialPort`, `Win32SerialPort`, plus a bespoke `SerialService` gRPC
registered directly in `Server.hpp`. We do not want "bridge the BBC serial port
to a real host serial device" to be a core responsibility.

Reasoning:

- The BBC serial port is an *external interface*. What sits on the other end of
  it is open-ended. Connecting it to a real host serial device (via PTY or an
  opened device path) is only ONE option.
- A very common case is to emulate a serial-connected *device* directly, inside
  Beebium, never touching a real serial interface: e.g. a FujiNet device, a
  modem, a serial printer, a mouse, or another emulated machine. These want to
  speak to the ACIA/ULA over the same `SerialDataSource`/`SerialDataSink` seam,
  but they are not "a host serial port".
- This is exactly the core-vs-extension boundary we already drew for Econet: the
  ADLC is core; the *backend* (AUN, Piconet, ...) is an extension behind a
  `NetworkBackend` seam. Serial should follow suit.

Target shape:

- Core owns: the ACIA, the ULA, the `SerialSocket`, and the
  `SerialDataSource`/`SerialDataSink` seam. Nothing about PTYs, device paths, or
  host OS serial APIs.
- A serial transport/device is selected via an extension (very likely a
  built-in one for the common cases), analogous to `EconetTransportExtension`.
  Sketch:
  - A `SerialEndpointExtension` (name TBD) base class that produces something
    implementing the source/sink seam, given configuration.
  - A built-in "host serial" extension providing today's `pty` / `device` /
    `loopback` modes (wrapping `PosixSerialPort` / `Win32SerialPort` /
    `PtyMaster` / `HostSerialEndpoint`).
  - Room for additional extensions that are *emulated devices* (FujiNet, modem,
    etc.) plugging into the same seam without any host serial I/O.
- The OS-level primitives (`SerialPort`, `PosixSerialPort`, `Win32SerialPort`,
  `PtyMaster`) do not need to live in core just to be shared. The PR promoted
  them from the Piconet extension to core to get a single source of truth; a
  cleaner home may be a small shared library that both the Piconet extension and
  the host-serial extension depend on, so neither core nor either extension owns
  the other's transport. Decide where that shared code lands when we cut the
  extension.
- Configuration and UI then come "for free" via the extension framework and
  `ExtensionUiService` (see `docs/discussion/extension-ui-architecture.md`),
  which is how AUN/Piconet get macOS + TypeScript client panels and indicators.
  See also `docs/discussion/serial-port-selector-control.md` for a proposed
  domain-specific control we could reuse for device-path selection.

Open question: the ACIA/ULA are genuinely core hardware and must stay core even
when no transport extension is loaded (the BBC can still read/write the
registers, get framing/parity/overrun, drive /DCD from the cassette/RS423
select, etc.). So the seam must have a well-defined "nothing attached" behaviour
(RX idle, TX discarded) independent of any extension. That is the NONE endpoint;
keep it in core, push everything else out to extensions.

## Other gaps (independent of the two corrections)

- Status is polling, not streaming. `SerialService` only offers a
  `GetSerialStatus` snapshot. We already moved Econet to a server-pushed
  `WatchEconetStatus` stream and treat streaming as the convention. A new
  interface should ship with `WatchSerialStatus` from the start.
- Client parity. Python client only. No TypeScript client (we just completed the
  TS cutover for AUN/Piconet/Econet) and no macOS Swift client. Proto stubs are
  Python-only.
- Not presettable. `--serial` is CLI/RPC only; `serial_spec` is not in the
  preset schema. A serial setup (e.g. a FujiNet ROM in a sideways slot plus a
  transport) is a natural preset. Once transport selection moves to an
  extension, this should fold into the existing extension-configuration-in-preset
  mechanism rather than being a special case.
- Unbounded TX growth in the default mode. `SerialServiceImpl` attaches a
  `ScriptableSerialEndpoint` by default, whose `tx_` is an unbounded
  `std::deque`. A ROM that streams serial out while nothing drains it grows
  memory without bound. Prefer defaulting to NONE (observably identical: TX
  discarded) and only attaching scriptable on request, or cap the queue.
- CLI separator heuristic. `device:PATH:baud` splits on the last colon and
  guesses the tail is baud "if numeric". Fragile and inconsistent with our
  shell-safe separator direction (AUN moved to `@`; is_list consumers own their
  inner separator). Tidy when the extension owns its own argument parsing.
- Duplicated spec parsing. The `--serial` string-to-mode parsing is in
  `ServerMain.hpp`; the mode-to-action logic is in `SerialService`. Parse the
  spec in one place.
- Windows. `Win32SerialPort.cpp` exists but is unexercised (contributor is on
  Linux); PTY mode is reasonably unsupported on Windows. Device mode needs
  validation on the Windows dev machine (Slioch).
- The reader-thread mutex in `HostSerialEndpoint` is a genuine cross-thread
  queue (reader thread vs emulation thread), so it is justified, not a defensive
  mutex - acceptable under our threading principle.

## Proposed plan (on feature/serial)

1. Settle the extension boundary (Correction 2). This is the load-bearing
   decision; it changes where the transport code lives, how it is configured,
   and how the GUI clients see it.
2. Make presence per-variant and absence-tolerant end to end (Correction 1).
3. Replace the snapshot RPC with `WatchSerialStatus` streaming.
4. Add TypeScript and macOS clients once the extension/UI shape is fixed.
5. Fix the unbounded-TX default; consolidate spec parsing; fold serial config
   into preset/extension configuration.
6. Validate the Win32 device path on Slioch.

## References

- PR #44: https://github.com/rob-smallshire/beebium/pull/44
- Contributor use case: fn-rom (BBC FujiNet ROM) end-to-end serial tests.
- `docs/serial-acia.md` (contributor's reference for registers/timing/transport).
- `docs/discussion/extension-ui-architecture.md`
- `docs/discussion/serial-port-selector-control.md`
- `docs/econet-integration.md` (the ADLC-core / backend-extension precedent).
