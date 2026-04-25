# Piconet device-path editor via a `ModalEditor` primitive

Status: Shipped on branch `piconet-device-path-editor`. The design
below records the conversation that ratified the approach; see
[Implementation notes](#implementation-notes) at the bottom for what
landed, where reality deviated from the original plan, and what is
still deferred.

This document supersedes
[`serial-port-selector-control.md`](serial-port-selector-control.md)
(which proposed a domain-specific eighth primitive; we are instead
adding a general primitive and composing existing primitives inside
it). It is a concrete near-term implementation of one slice of the
broader design space in
[`piconet-device-discovery.md`](piconet-device-discovery.md) —
specifically the "manual path edit while disabled" slice, without
automatic re-attachment.

---

## The problem

The Piconet USB-CDC device path is only settable at server launch, via
`--piconet device_path=...` or the equivalent preset key. When the
path is wrong — user typoed it, the device was unplugged at startup,
macOS renumbered it (`usbmodem101` -> `usbmodem14101`) — the only
recovery is to relaunch the server. Hot-unplug is *detected* (the
reader thread closes the serial port, the Indicator goes red, the
Enable button disappears) but re-attachment is not implemented.

The user should be able to pick a new serial port from inside the
Piconet panel while Piconet is disabled, and have the change take
effect immediately on commit. Not editable while Piconet is enabled.

## User-facing behaviour

Piconet panel when **live** (serial open, mode=Listen):

```
Piconet
  Device: /dev/tty.usbmodem101          <-- plain read-only label
  (OK) Adapter responsive
  [Disable]
```

Piconet panel when **disabled** (any of: mode=Stop, serial closed
after hot-unplug, serial never opened because path was wrong at
startup):

```
Piconet
  Device: /dev/tty.usbmodem101  [pencil]   <-- pencil opens popover
  (RED) Adapter offline: No such file or directory
  [Enable]        <-- only when serial is open
```

Clicking the pencil on macOS opens a popover with a single combobox
(NSComboBox; the editor body is one `EditableChoice` — see
[Implementation notes](#implementation-notes) for the evolution from
the original Choice + TextInput sketch):

```
  Serial port
  [ /dev/tty.usbmodem101                      v ]
                  ^ enumerated ports as dropdown items;
                    the field is freely editable for paths
                    the enumerator did not return.

            [ Cancel ]  [ Save ]
```

Save commits the new path; the Piconet panel redraws with the new
value shown in the anchor. On a successful open the Indicator flips
to OK and the Enable button appears. On failure the Indicator shows
the OS-level error message (same path as the startup-failed-open
case today) and the editor remains available for another try.

Cancel discards the local popover state — the server never hears
about an aborted edit.

## Design

### 1. `ModalEditor` as an eighth primitive

The Extension UI framework's existing seven primitives (`Label`,
`Indicator`, `Toggle`, `Button`, `Choice`, `TextInput`, `Group`) are
all live-dispatch controls: any interaction fires a Dispatch event
immediately. There is no way to express "open an editor, edit freely,
commit atomically or cancel", which is the popover-with-confirmation
interaction the station-number editor already uses in the Econet
sidebar header.

We add one general structural primitive — `ModalEditor` — whose
editor body is composed of existing primitives. Named for the
interaction model ("separate interaction scope with deferred commit")
not presentation: each frontend renders it idiomatically — SwiftUI
popover, Win32 flyout, TUI sub-panel, future HTML `<dialog>`. The
proto comment explicitly disclaims "modal in the HIG-strict
input-blocking sense".

```protobuf
message ModalEditor {
    Control anchor   = 1;   // always-visible display; typically a Label
    Control editor   = 2;   // tree shown on activation; typically a Group
    bool    editable = 3;

    enum CommitRole {
        SAVE = 0;  // "Save" / "OK" / "Done" per platform
        ADD  = 1;  // "Add" / "Create" per platform
    }
    CommitRole commit_role = 4;
    bool       show_cancel = 5;
}
```

Added to `Control.control` oneof as variant 9.

### 2. Client-buffered commit via `EditorCommit`

Sub-controls inside the editor tree (`TextInput`, `Choice`, etc.)
hold local frontend state while the popover is open. Typing or
selecting does **not** fire a Dispatch. On confirm the frontend
bundles all sub-control values into a single `EditorCommit` payload
delivered as one atomic Dispatch to the `ModalEditor`'s control id.

```protobuf
message EditorFieldValue {
    string field_id = 1;
    oneof value {
        bool   bool_value   = 2;
        string string_value = 3;
        uint32 index_value  = 4;
    }
}
message EditorCommit {
    repeated EditorFieldValue fields = 1;
}
```

Added to `DispatchRequest.payload` oneof as variant 7.

Cancel is a pure client-side discard. The server has no state to
roll back because it never saw intermediate values. This matches the
existing station-number popover's semantics.

### 3. Semantic `CommitRole` enum; cancel as a bool

Server-provided string labels for the commit button are a leaky
abstraction — "Save" reads idiomatically on macOS, Windows prefers
"OK", iOS prefers "Done", GNOME prefers "Apply", and every platform
has its own conventions for destructive vs constructive commits that
a string can't capture. Strings also block localisation: the
frontend knows the user's locale, the server does not.

`CommitRole::SAVE` is the default (editing an existing value);
`CommitRole::ADD` marks creation of a new entity. Frontends pick
platform-idiomatic wording from the semantic role. No `DESTRUCTIVE`
role — destructive operations are almost always modeless
confirmations ("Delete peer?" Yes/No), a different primitive if we
ever need one.

`show_cancel` is a bool because every platform's dismiss-without-
applying affordance has the same semantic. macOS popovers dismiss
natively on click-outside / escape; `show_cancel=true` adds an
explicit button for platforms that want it.

### 4. `editable` gate

The server declares the `ModalEditor` unconditionally based on its
state, and flips `editable` based on whether interaction is allowed.
`editable=false` causes the frontend to render the anchor as a plain
read-only display (no edit affordance). Symmetric to `Button.enabled`
— the server retains the authoritative "can this be edited?" rule;
the frontend does not need to know *why*.

For Piconet: `editable = !backend || !backend->is_serial_open() ||
backend->mode() != Listen`.

### 5. In-place `SerialPort` reopen inside `PiconetBackend`

On commit, **we do not construct a new `PiconetBackend`**. The
backend is a stable object for the machine's lifetime. What needs to
change is the `SerialPort` *inside* the backend. `EconetSocket`,
`FourWayHandshake`, and `Mc6854` all keep their `NetworkBackend&`
references bound to the same `PiconetBackend` instance, unchanged.
The `rx_queue_`, backend-status-sequence, async callback, and config
identity are all preserved across the reopen.

```cpp
class PiconetBackend {
public:
    // gRPC thread posts a reopen request. Returns immediately; the
    // actual reopen runs on the emulation thread.
    void request_reopen(std::string new_path);

private:
    // Called by the emulation thread at the top of receive_frame().
    // Closes the old SerialPort, joins the reader, opens the new
    // path, sends SET_STATION + SET_MODE STOP, starts a new reader.
    // On open failure records open_error_message_ and leaves the
    // backend in the closed state. Fires on_async_state_change_.
    void process_pending_reopen();
};
```

### 6. Emulation-thread-owned mutation via a pending-reopen slot

The emulation thread reads `serial_` every tick (`is_serial_open`,
`send_frame`, `receive_frame`, etc.). The gRPC handler thread would
be the one mutating it on a user-initiated reconfig. We keep the
emulation thread as the sole writer of `serial_` by posting reopen
requests from gRPC to a `std::atomic<std::string*>
pending_reopen_path_` slot; the emulation thread picks up the slot
at the top of `receive_frame()` and performs the close/open/thread-
restart serially with its own reads.

No locks, no machine pause, no coordination primitives beyond the
atomic pointer. The reopen can take a few milliseconds (reader-
thread join + serial open + SET_STATION + SET_MODE STOP) which runs
inline on the emulation thread — imperceptible to the BBC for a
user-initiated action that only fires when Piconet is already
disabled.

### 7. The ADLC is never hot-unplugged from a running machine

When Piconet is disabled (`mode=Stop`), the ADLC is still fitted,
still ticked by `EconetSocket::tick_rising`, and still called by the
emulation thread via `send_frame`/`receive_frame`. "Disabled" at the
Piconet level means the wire is muted (firmware in STOP drops TX and
forwards no RX), not that the ADLC is removed. The header
indicator's "Disconnected" label reflects `is_connected()` (serial
open AND mode=Listen), which is a weaker property than ADLC-fitted.

This invariant — ADLC fitted at startup, never removed mid-session —
is deliberate. It mirrors the BBC itself: the Econet module is a
solder-in upgrade, not a runtime-switchable capability. Dynamically
unfitting the ADLC would be an un-BBC-like gesture and is out of
scope for this or any related work.

## Implementation plan

Branch: `piconet-device-path-editor`.

| # | Item | Status |
|---|------|--------|
| A | Extend `extension_ui.proto` with `ModalEditor` + `EditorCommit`; regen stubs | Done (commit `5a99529`) |
| B | Dispatcher validation for `EditorCommit` payloads | Done (commit `f244b36`) |
| C | `beebium::serial::enumerate_ports` helper (POSIX + Win32 + tests) | Done (commit `e02e632`) |
| D | `PiconetBackend::request_reopen` + `process_pending_reopen` | Done (commit `cb9b614`) |
| E | `PiconetUi`: emit `ModalEditor` when disabled; handle `EditorCommit` | Done (commit `cb9b614`) |
| F | Swift renderer: `.modalEditor` arm with `.popover(...)` + per-sub-control `@State` buffers | Done (commit `cb9b614`) |
| G | Swift client: `.editorCommit` case in `ExtensionDispatchPayload` and `ExtensionUiClient` | Done (commit `cb9b614`) |
| H | Tests: proto round-trip, dispatcher validation, `PiconetBackend` reopen, `PiconetUi` state matrix | Done (commits `cb9b614` + `17a8333`) |
| I | Manual end-to-end on macOS and Slioch (Windows) | macOS done; Slioch deferred |
| J | Python and TypeScript renderers | Deferred (follow-up commit) |
| K | `EditableChoice` primitive; migrate from `TextInput.suggestions` | Done (commit `17a8333`); see [Implementation notes](#implementation-notes) |

Critical files by area:

- Proto and framework: `src/core/extension-api/proto/extension_ui.proto`,
  `src/service/include/beebium/service/ExtensionUiService.hpp`
- Serial enumeration: `src/core/include/beebium/serial/EnumeratePorts.hpp`,
  `src/core/src/EnumeratePorts{Posix,Win32}.cpp`
- Piconet backend: `src/extensions/piconet/include/beebium/econet/PiconetBackend.hpp`,
  `src/extensions/piconet/src/PiconetBackend.cpp`
- Piconet UI: `src/extensions/piconet/PiconetUi.{hpp,cpp}`
- Swift: `clients/macos/Beebium/Beebium/ExtensionViewRenderer.swift`,
  `clients/macos/Beebium/Beebium/ExtensionUiClient.swift`

Notably **unchanged**:

- `EconetSocket` — the backend reference stays bound to the same
  `PiconetBackend` object for its lifetime
- `FourWayHandshake`, `Mc6854` — ditto; no reference-to-pointer
  refactor
- `Machine` — no pause plumbing needed
- `PiconetEconetTransportExtension` — no `reconfigure_device_path`
  method; `PiconetUi::handle_event` reaches the backend directly via
  the extension's existing `backend()` accessor

## Current state

See [Implementation notes](#implementation-notes) below for the full
landed picture: seven commits on `piconet-device-path-editor`
(including the rejected `8f3d3cb` and its revert), the EditableChoice
promotion, and the closed-state-backend recovery deviation.

## Rejected alternatives (and why)

### R1. `SerialPortSelector` as a domain-specific eighth primitive

The first draft of this work (captured in
[`serial-port-selector-control.md`](serial-port-selector-control.md))
proposed adding `SerialPortSelector` as a bespoke primitive wrapping
the platform-specific enumeration helper. Justification at the time:
two consumers (Piconet now, RS423 forwarding later), a dynamic
option list, a desire to separate display label from stable
identifier.

The reframing: the interaction the user actually wants is "pick
something from a list *or* type it in, then confirm". That shape
exists for many operations — add a peer, rename a disc, set a
breakpoint address — not just picking a serial port. The general
affordance is "deferred, atomic commit of a form", not "pick a
serial port". Reaching for a general primitive (`ModalEditor`) whose
editor tree is composed of existing primitives (`Choice` for the
enumerated list, `TextInput` for the custom path) covers
`SerialPortSelector`'s use case and every future one, while
extending the vocabulary by just one element rather than N.

Consequence: the enumerator stays as an ordinary library function
(`beebium::serial::enumerate_ports`), and `PiconetUi` composes the
editor body itself. No SerialPortSelector primitive, no per-domain
primitive creep.

### R2. `EconetSocket::replace_backend` with a `BackendSlot` forwarder

The initial-draft plan added a `replace_backend` seam on
`EconetSocket` that would destroy the old `PiconetBackend` and
install a new one built via `create_backend(new_path)`. That entails:

- A new `BackendSlot` indirection implementing `NetworkBackend` and
  forwarding to a pointer that can be swapped, so that
  `FourWayHandshake` and `Mc6854` (which hold `NetworkBackend&`,
  not pointers) continue to work across the swap.
- Either a reference-to-pointer refactor across FWH and Mc6854 (~25
  call sites), or the BackendSlot as a derived-class forwarder.
- `Machine::pause` / `wait_until_idle` / `resume` plumbing from the
  gRPC handler thread — requiring `Machine*` to be reachable from
  the piconet plugin.
- Deciding whether to preserve ADLC register state (by keeping the
  `Mc6854` instance) or accepting state loss (by rebuilding it).

That is an enormous amount of surgery for what is actually a small
user-visible change: "talk to a different serial port."

Reframing: **the thing that needs to change is the `SerialPort`
inside the `PiconetBackend`, not the `PiconetBackend` itself.** The
backend is a stable, stateful object that happens to own a
`SerialPort` member. Swap the member, keep the object. Everything
upstream is untouched: FWH / Mc6854 / EconetSocket keep their
references, the `rx_queue_` and counters and callbacks survive, no
ADLC state loss question exists, no `Machine::pause` plumbing is
needed.

### R3. `Machine::pause` around the reopen

Even after committing to the smaller-surgery shape in R2's reframe,
an early version of the plan assumed the gRPC handler thread would
mutate `serial_` directly, protected by a brief `Machine::pause` /
`wait_until_idle` / `resume` window.

Reframing: **the emulation thread is the natural owner of
`serial_`** (it already reads it on every tick; every writer today
is on the emulation thread). The clean way to keep "single writer"
is to have the gRPC thread post a request into an atomic slot, and
have the emulation thread consume the request at a known-safe point
(top of `receive_frame()`, which is already its natural entry into
the backend each tick). No machine pause; no cross-thread
coordination beyond the atomic pointer.

### R4. `PiconetBackend` initial-mode ctor parameter

Committed in `8f3d3cb` as a preparatory step for R2 (a replacement
backend built by `create_backend` needed to come up in `Mode::Stop`,
not the ctor's default `Mode::Listen`). Once R2 was rejected, the
parameter had no caller that wanted anything but the default —
startup is the only constructor call, and startup always wants
`Listen`. Reverted in `ab8d028`. The reopen path sets `Mode::Stop`
directly via `write_to_serial` + `current_mode_.store`, not via
construction.

## Design missteps — directions for avoiding them next time

These are the actual moments in the design session where the wrong
turn was taken, with the correction pattern explicit.

### M1. Picking the domain-specific primitive before asking whether the general one suffices

**What happened.** When the user asked for a device-path picker, the
first proposal reached for `SerialPortSelector` as a new primitive —
justified by "two consumers", "dynamic list", "stable-id distinct
from display label". All true, none sufficient to justify bespoke
framework surface.

**Corrective direction.** Before proposing a new primitive for a new
use case, ask: *"can the interaction be expressed as composition of
existing primitives inside a new structural wrapper?"* If yes, the
new primitive is a structural container, not a domain-specific leaf.
Here, `ModalEditor` is the structural container; a `Choice` and a
`TextInput` inside it are the composition; no domain knowledge of
serial ports leaks into the framework.

The seven-primitive discipline's cost function should be read as
*"each new primitive must justify itself across all plausible use
cases"*, not *"each new primitive must justify itself on one use
case."* A general structural primitive clears the higher bar;
`SerialPortSelector` did not.

### M2. Mechanical inference from "X is a ctor arg" to "must rebuild the containing object"

**What happened.** The reasoning chain went: "`device_path` is a
field of `PiconetConfig`; `PiconetConfig` is passed to
`PiconetBackend`'s ctor; therefore to change the device path we
construct a new `PiconetBackend`; therefore we need
`EconetSocket::replace_backend`; therefore we need a
`BackendSlot` forwarder so `Mc6854`'s `NetworkBackend&` can be
rebound; therefore we need `Machine::pause` so the emulation thread
is not dereferencing the old backend during the swap." Each link in
that chain was defensible locally; the chain as a whole was massive
overreach for a feature whose user-visible surface is one `path`
string.

**Corrective direction.** When a ctor parameter needs to change at
runtime, ask *"what does this parameter ultimately configure?"*
rather than *"how do I construct a fresh instance?"* Here, the
parameter configures a `SerialPort` owned as a member. The
narrowest change is swapping the member; the backend itself is the
stable frame of reference. The same reframe applies to any
"reconfigure a live object" request: identify the minimal member
whose mutation achieves the goal, and constrain the blast radius
there.

A short form: **don't rebuild the aircraft carrier when you mean to
change the plate on the deck.**

### M3. Reaching for synchronisation primitives before trying single-writer

**What happened.** The early plan for the swap assumed the gRPC
handler thread would write `serial_`, and therefore we needed
either `Machine::pause` plumbing (heavy) or a mutex on `serial_`
(cheap but taxes the hot path forever).

**Corrective direction.** Before adding synchronisation, ask *"can
the system be arranged so that only one thread ever writes this
field?"* If yes, the solution is structural (post-a-request + single
consumer) and requires no locks. The emulation thread was already
the sole writer of every `serial_` mutation today (`send_frame`,
`on_station_id_changed`); keeping that property — by having the
gRPC thread post requests rather than write directly — was cheaper
than adding any cross-thread mechanism.

This is the pattern already used by `AunBackend` for its connection
toggle (one writer; status-sequence counter for observers) and by
the hot-unplug path in `PiconetBackend` today (reader thread
detects, fires a callback; observers react without touching the
serial port). Follow that pattern before reaching for locks or
pauses.

### M4. Working around a UI-vs-implementation smell rather than fixing it

**What happened.** During the cost analysis for R2, the question
came up of whether the header Indicator saying "Disconnected" when
Piconet is in `Mode::Stop` reflected the full truth (ADLC unfit +
backend torn down) or just the wire state (`is_connected` = serial
open AND mode=Listen). It turned out to be the latter, and the gap
was briefly framed as a "design smell" that could be fixed by
extending `PiconetUi`'s Disable button to also unfit the ADLC.

**Corrective direction.** Not every UI-vs-implementation gap is a
bug. The question to ask is *"what is the correct user-facing
meaning of this label?"* not *"does this label match every
implementation detail underneath?"* For the BBC, the user-meaningful
question is "is my wire live?", not "is the ADLC chip soldered
in?". The Indicator's "Disconnected" label was correct for the
former and genuinely weaker than the latter, and the user-facing
semantic was the right one to keep. The ADLC-never-hot-unplugged
invariant is consistent with the BBC's own physical reality.

When a UI-vs-implementation gap is flagged, verify the
user-meaningful semantic first; only widen the implementation if
the semantic demands it.

### M5. Writing planning artefacts in `.claude/plans/` when the artefact was actually repo-owned

**What happened.** Initial design work landed in
`~/.claude/plans/parallel-drifting-nebula.md`, a transient per-
session plan file. When the user asked for a design doc to review
with a fresh context, they explicitly asked for it to be
*in-repo* — because a design agreed between user and assistant
should be reviewable, diffable, and referable from the git history,
not buried in a session cache.

**Corrective direction.** If a design discussion produces agreements
that downstream sessions / reviewers will need to consult, the
output is an in-repo document under `docs/discussion/` (or
equivalent). The ephemeral plan file is for in-flight scratch work,
not ratified design. When a design conversation reaches a stable
point, the default should be to propose an in-repo home for the
artefact even without being asked.

## Implementation notes

Brief walkthrough of where the as-built implementation matches the
design and where it deviates. Written 2026-04-25 after the two
landing commits on `piconet-device-path-editor` were verified
end-to-end on macOS.

### Landed commits

Branch `piconet-device-path-editor` (relative to `master`):

| Commit | Subject |
|---|---|
| `5a99529` | Extension UI: add `ModalEditor` primitive and `EditorCommit` payload |
| `f244b36` | Extension UI dispatcher: validate `ModalEditor` `EditorCommit` payloads |
| `8f3d3cb` | PiconetBackend: add `initial_mode` ctor parameter (reverted) |
| `e02e632` | Add `beebium::serial::enumerate_ports` host-serial enumerator |
| `ab8d028` | Revert "PiconetBackend: add `initial_mode` ctor parameter" |
| `cb9b614` | Piconet device-path editor via ModalEditor + TextInput.suggestions |
| `17a8333` | Extension UI: add EditableChoice primitive; drop TextInput.suggestions |

Net useful commits: `5a99529`, `f244b36`, `e02e632`, `cb9b614`,
`17a8333`. Commits `8f3d3cb` / `ab8d028` were a preparatory step for
the rejected R2 direction.

### What shipped (matches the design)

- **ModalEditor primitive + EditorCommit payload.** Nine controls now
  in the vocabulary: the original seven plus `ModalEditor` (8th) and
  `EditableChoice` (9th, see deviations below).
- **Reopen path on the emulation thread via an atomic-pointer slot.**
  `PiconetBackend::request_reopen` (gRPC thread) posts; `process_pending_reopen`
  (emulation thread, top of `receive_frame`) consumes. `SerialFactory`
  injection makes the reopen mockable in unit tests. Reopens always
  land in `Mode::Stop`; the user re-Enables explicitly.
- **No EconetSocket / FourWayHandshake / Mc6854 / Machine changes.**
  The R2 / R3 reframes held: backend identity is stable, the
  `SerialPort` member is the moving part.
- **macOS frontend: ModalEditor as `.popover` with deferred commit.**
  Anchor + 14pt pencil button + popover containing the editor body
  and Save/Cancel; sub-control values held in a per-popover
  `[String: FieldBuffer]` until commit.

### Where the design deviated

- **The editor body collapsed from `Choice + TextInput` (in a Group)
  to a single `EditableChoice`.** The original sketch in this doc
  composed the editor body of two existing primitives — a `Choice`
  with the enumerated port list and a `TextInput` for a custom path.
  Implementing that surfaced an ambiguity: with both controls
  visible and editable, the user couldn't tell which one would take
  effect on Save. We tried a halfway fix (extending `TextInput` with
  `repeated string suggestions = 4`, single source of truth, list as
  a "pick to fill" affordance), shipped that as `cb9b614`, then
  promoted it to a first-class `EditableChoice` primitive in
  `17a8333`. See R5 / M6 below.

- **The closed-state-backend recovery path.** The original design
  assumed a backend would only exist if the initial open succeeded.
  When testing the wrong-path-at-startup scenario end-to-end, the
  ModalEditor's reopen path needed something to call
  `request_reopen` against; with `create_backend` returning
  `nullptr` on open failure, the editor was reachable but Save was
  inert. Fix: `PiconetEconetTransportExtension::create_backend` now
  always returns a `PiconetBackend` (even when the SerialPort failed
  to open), populated with the OS-level error in
  `PiconetBackend::open_error_message_`. The Indicator surfaces the
  same "Cannot open device: …" text either way; the editor's Save
  now always has a backend to drive.

- **`SerialPort::open_error()` lifted to the abstract base.** The
  reopen path needs a uniform way to recover the OS-level error
  string regardless of which SerialPort implementation the factory
  returned. Promoted from a concrete `PosixSerialPort` /
  `Win32SerialPort` accessor to a virtual on the base class with an
  empty-string default; both production implementations override.

- **Manual verification was macOS-only.** The branch was verified
  end-to-end on macOS (wrong path at startup → editor → reopen →
  Enable → BBC sees the wire). Slioch (Windows) verification
  remains deferred.

### What was deferred

- **Python and TypeScript renderers.** Item J on the original plan;
  needs `EditableChoice` support in
  `clients/python/src/beebium/extension_ui.py` and the TS client.
- **Manual verification on Slioch (Windows).** Item I, partial.
- **Reconnect button + auto-reconnect timer.** Out of scope per the
  parent doc
  [`piconet-device-discovery.md`](piconet-device-discovery.md);
  manual path edit covers the renumbering case for the common
  single-Pico setup.

### R5. `TextInput.suggestions` as a poor-man's combobox

After R1's reframe (general `ModalEditor` primitive, editor body
composed of existing primitives), the first-pass editor body used a
`Choice` + `TextInput` pair. Field testing surfaced an ambiguity:
the user couldn't tell whether the Save would apply the Choice's
selected option or the TextInput's typed value. We considered three
fixes:

1. Extend `TextInput` with a `repeated string suggestions = 4` field
   and render TextField + always-visible list, single source of
   truth in the field, list highlights the matching entry. Shipped
   as `cb9b614`.
2. Link the Choice and TextInput inside the popover renderer with an
   ad-hoc convention. Rejected — fragile, undocumentable in the
   framework's type system.
3. Add a first-class `EditableChoice` primitive. Picked up in the
   next iteration (`17a8333`).

(1) shipped as the working state because it unblocked the user's
real scenario quickly. Once the pattern was validated end-to-end,
(3) was the cleaner long-term home: the role "pick from a known set
or enter a custom value" deserves its own name, parallels `Choice`
on the closed/open axis (`Choice` = closed list, `EditableChoice` =
open list), and maps directly to NSComboBox / GtkComboBox(has_entry)
/ HTML `<input list>` — no novel rendering code required.

The `TextInput.suggestions` field was removed in `17a8333` rather
than kept as a deprecated alias. No downstream consumers existed
besides PiconetUi, and per project convention there are no
backwards-compatibility shims between client and server.

### M6. Bolting a side-feature onto an existing primitive when the role deserves its own name

**What happened.** When the Choice + TextInput ambiguity surfaced,
the first instinct was to fix it inside `TextInput` — adding a
`suggestions` field, special-casing the renderer when it was
non-empty. The field worked, but it felt vestigial: every reader of
the proto had to learn that "TextInput with non-empty suggestions"
behaved fundamentally differently from a plain TextInput, and
nothing in the type signature surfaced that.

**Corrective direction.** When extending an existing primitive with
a feature flag fundamentally changes its interaction model, prefer
a new role-named primitive. The forcing question is: *"does the
new behaviour deserve its own name?"* If a competent reader looking
at the Control oneof should be able to tell at a glance that this
shape exists, the answer is yes. `EditableChoice` clears that bar;
`TextInput-with-suggestions` did not.

This is the inverse of M1's lesson: M1 says "don't reach for a
domain-specific primitive when a general one suffices"; M6 says
"don't extend an existing primitive when the role is general enough
to deserve its own name". Both rules are about keeping the
vocabulary small *and* honest — primitives should describe
interaction roles, not carry feature flags that mutate their
behaviour.
