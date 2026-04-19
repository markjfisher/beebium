# Extension UI Architecture

A server-driven, message-passing approach to giving extensions
symmetric reach into every Beebium frontend (macOS / Windows / Linux /
web / Python diagnostics) without per-frontend code per extension.

Status: Design proposal. Not committed to implementation.

---

## Problem

Beebium's extension framework lets the C++ server load arbitrary
peripherals and Econet transports at runtime. The CLI surface for
configuring them is already manifest-driven and uniform across
extensions. The runtime surface is not: each frontend needs hand-written
UI for each extension it wants to expose, and there is no path for an
extension built outside the Beebium tree to ship a control panel that
the macOS / Windows / Linux frontends can render.

Concretely, when Piconet ships a `PiconetService` with a `device_path`
field, an `is_connected` boolean, and (proposed) a `SetMode` RPC, every
frontend that wants to surface those needs:

- A hand-written view in its native UI toolkit
- A hand-written gRPC client wiring
- A coordinated release with the extension

That breaks the asymmetry the extension framework exists to remove.
Plug-in extensions should be discoverable and operable the same way
built-in ones are, in every frontend.

## The Shape

Adopt a server-driven UI model with explicit message passing,
modelled on the Elm Architecture (TEA) and on Phoenix LiveView's
production refinement of it:

- **The server is the source of truth.** It holds the extension's
  state, computes a *view* (a tree of controls) from that state, and
  streams the view to subscribed frontends.
- **The client is a thin renderer.** Each frontend translates the
  view tree into native widgets using its own toolkit. It holds no
  business state of its own.
- **All user actions are messages.** A control id plus a typed
  payload, posted back to the server, which mutates state and emits a
  new view.

```
+-------------------+     SubscribeView (stream View)    +-------------+
|                   | ---------------------------------> |             |
|  Extension on     |                                    |  Frontend   |
|  Beebium server   | <--------------------------------- |  (any UI)   |
|                   |     Dispatch(id, payload)          |             |
+-------------------+                                    +-------------+
```

The frontend never decides what an extension's UI looks like. The
extension declares it; the frontend renders it.

## Reference Points

Two existing systems anchor the design.

### Phoenix LiveView (primary analogue)

LiveView has been in production since 2019 and is the closest match
to what we'd build:

- Server holds session state and renders an HTML view tree from it
- The first response is a static fingerprint plus the dynamic slots;
  subsequent updates are *diffs* of the dynamic slots only
- Client mounts the static template once and patches the DOM in place
  using the morphdom library
- User actions arrive as named events (`phx-click`, `phx-change`)
  with optional payload; the server runs an `handle_event/3` callback,
  mutates state, and the view re-renders

What we should borrow:

- Server holds the model. View is a function of state. Always.
- Stream of view updates, not request/response per render.
- Diff-based updates eventually; full-tree updates first (these UIs
  are tiny — a screen or two of controls per extension).
- Stable client-side ids per control, not coordinate-based addressing.
- Versioned views so stale events from a previous render can be
  detected and rejected.

What we should not borrow:

- HTML as the wire format. Ours is proto and the renderer is native.
- Heredocs and macros. We pay a different language tax.
- LiveView's `phx-change` / `phx-blur` / `phx-debounce` flexibility
  in the first cut. Pick one input semantics and ship it.

### Elm (the discipline)

The Elm Architecture's contribution is the typed message:
`Html msg`, where `msg` is the type of every event the view can
produce. The compiler checks that every dispatched message can be
handled. We can't reproduce that in protobuf, but we can emulate it:

- Every control carries a stable string id assigned by the extension
- All events route through one RPC: `Dispatch(extension, id, payload)`
- The server validates the id is known for the current view revision
  and rejects unknown / stale ids cleanly

The discipline is: *one extension, one update function, every event
flows through it*.

## Control Vocabulary

A constrained alphabet, deliberately small. Seven primitives cover the
concrete extensions on the table (Piconet, AUN, future Pi Econet HAT,
peripheral debug surfaces) and probably 90% of anything else.

| Primitive    | Reads as            | Writes as                   |
|--------------|---------------------|-----------------------------|
| `Label`      | text                | -                           |
| `Indicator`  | semantic state + text | -                         |
| `Toggle`     | bool                | bool                        |
| `Button`     | label, enabled flag | (no payload — fire only)    |
| `Choice`     | options + selected  | selected index              |
| `TextInput`  | string + placeholder | string                     |
| `Group`      | optional label, children | -                      |

`Indicator` carries a semantic state — `OK / WARN / ERROR / UNKNOWN` —
not a colour. The frontend chooses how to render that for its
platform's accessibility conventions.

`Group` is the only structural primitive. Nested groups with optional
labels are sufficient for the layouts we need; the precedent is solid
(HTML `<fieldset><legend>`, SwiftUI `Form/Section`, GTK `Frame`). No
explicit grid / stack / spacing primitives in the first cut — render
groups as the toolkit's natural vertical flow with the toolkit's
default spacing.

The vocabulary expands only when an extension needs something the
existing alphabet cannot express. Stretching the alphabet should hurt
slightly each time — that's the forcing function for keeping it small.

## Wire Schema (Sketch)

A new proto, owned by the core service layer, not by any one extension:

```protobuf
service ExtensionUiService {
    // One stream per extension. Server pushes full views (initially)
    // or diffs (later) as state changes.
    rpc SubscribeView(SubscribeViewRequest) returns (stream View);

    // Client-initiated event. Replies indicate whether the event was
    // accepted; the resulting state change shows up on the SubscribeView
    // stream, not in the response.
    rpc Dispatch(DispatchRequest) returns (DispatchResponse);
}

message SubscribeViewRequest {
    string extension_name = 1;  // e.g. "piconet"
}

message View {
    string extension_name = 1;
    uint64 view_revision = 2;   // monotonic per extension; bumped per render
    Control root = 3;            // usually a Group
}

message Control {
    string id = 1;               // stable across renders where semantics persist
    oneof control {
        Label       label       = 2;
        Indicator   indicator   = 3;
        Toggle      toggle      = 4;
        Button      button      = 5;
        Choice      choice      = 6;
        TextInput   text_input  = 7;
        Group       group       = 8;
    }
}

message Label      { string text = 1; }

message Indicator {
    enum State { UNKNOWN = 0; OK = 1; WARN = 2; ERROR = 3; }
    State state = 1;
    string text = 2;
}

message Toggle     { string label = 1; bool value = 2; }
message Button     { string label = 1; bool enabled = 2; }

message Choice {
    string label = 1;
    repeated string options = 2;
    uint32 selected_index = 3;
}

message TextInput {
    string label = 1;
    string value = 2;
    string placeholder = 3;
}

message Group {
    optional string label = 1;
    repeated Control controls = 2;
}

message DispatchRequest {
    string extension_name = 1;
    string control_id = 2;
    uint64 view_revision = 3;   // server rejects if stale
    oneof payload {
        bool   bool_value   = 4;
        string string_value = 5;
        uint32 index_value  = 6;
        google.protobuf.Empty empty = 7;  // buttons
    }
}

message DispatchResponse {
    bool   accepted = 1;
    string error    = 2;
}
```

A discovery RPC on the same service (or on `EconetTransportService` /
`PeripheralExtensionService`) reports which extensions expose a UI, so
a frontend can decide which subscriptions to open.

## Server-Side: Extension API

The `Extension` base gains an optional UI hook:

```cpp
class Extension {
public:
    // Returns the extension's UI provider, or nullptr if it has no UI.
    virtual ExtensionUi* ui() { return nullptr; }
};

class ExtensionUi {
public:
    // Build the current view from the extension's state. Pure function
    // of state -- no side effects, no I/O.
    virtual View build_view() const = 0;

    // Handle an event from a frontend. May mutate extension state.
    // After returning, the framework calls build_view() and pushes the
    // result to all subscribers.
    virtual void handle_event(const std::string& control_id,
                              const Payload& payload) = 0;
};
```

The framework owns the rest: subscription bookkeeping, view-revision
counters, diffing (eventually), and the `Dispatch` validation
gauntlet (extension exists, control id is known for the current view,
payload type matches). Extensions do not touch gRPC directly.

A small `mark_dirty()` helper on `ExtensionUi` lets an extension
notify the framework that state has changed for reasons other than a
dispatched event (e.g. background USB-CDC traffic on Piconet flipping
`is_connected`). The framework debounces dirty notifications and
issues at most one rebuild per tick.

## Client-Side: Renderer

Each frontend implements one `ExtensionViewRenderer` per toolkit:

- Subscribe to `ExtensionUiService.SubscribeView` for each extension
  the frontend wants to surface (typically all of them, or all whose
  manifest declares a UI)
- On each `View` received, walk the tree and produce native widgets.
  Use a stable mapping from `control_id` to widget so updates patch
  in place rather than rebuilding the panel
- Wire each widget's user-action callback to construct a
  `DispatchRequest` carrying the control id, the current view
  revision, and the appropriate payload, then call `Dispatch`
- Display the result: extension panels can live in a sidebar, a
  Settings tab, a separate window, or be inlined in a status pane.
  That choice is the frontend's, not the extension's

The renderer is the only extension-aware code in each frontend, and
it is per-toolkit, not per-extension. Adding a new extension adds zero
client code.

## Pilot: Piconet First

Piconet's UI is small enough to validate the architecture without
fighting layout corner cases:

```
Group(label = "Piconet")
  Label     { text = "Device: /dev/tty.usbmodem101" }
  Indicator { state = OK,  text = "Adapter responsive" }   // serial_open
  Toggle    { label = "Enabled", value = true }            // LISTEN <-> STOP
```

This exercises every leg of the loop:

- **Read-only state** → Label, Indicator (driven by `mark_dirty()`
  when a STATUS event arrives or the serial port closes)
- **State mutation** → Toggle, dispatched as a bool, mapped on the
  server to `SET_MODE LISTEN` or `SET_MODE STOP`
- **Round-trip closure** → after `handle_event`, the extension's
  cached mode changes, `build_view` reflects it, the next `View` push
  carries the updated `Toggle.value`. Clients render the new state
  without round-trip-specific code.

If a frontend can render Piconet's UI and toggling it actually changes
the firmware mode, the architecture is proven.

## Pilot Stretch: AUN

After Piconet, the AUN built-in extension is the natural second test.
Its UI exercises pieces Piconet does not:

- Nested groups (Network / Peers)
- TextInput (peer station, host, port)
- Button + form-style state (Add Peer collects three text inputs)
- Choice (eventually: connection state if we expand beyond bool)

If AUN's UI composes from the seven primitives without forcing new
ones, the vocabulary is correctly sized. If it does not, the gap
identified is the next primitive to add.

## Open Questions

- **Diffs vs. full tree.** Start with full-tree pushes; switch to
  diffs only if measurement shows a problem. These views are tens of
  controls at most, not thousands.
- **TextInput dispatch timing.** Per-keystroke dispatch is chatty;
  on-blur dispatch feels laggy if validation lives server-side. First
  cut: dispatch on blur, accept the latency, revisit if a real
  use-case demands per-keystroke.
- **View ownership during reconfiguration.** When a transport is
  unloaded mid-session (does that ever happen?), what does the client
  see? Probably: the SubscribeView stream ends with `NOT_FOUND`. The
  frontend treats that as "panel disappears."
- **Discovery surface.** A new `ListExtensionUis()` RPC, or a flag
  on the existing `ListExtensions` /  `ListTransports` RPCs? Lean
  toward the flag — fewer round trips, no new service.
- **Validation.** Does the server validate `TextInput` values
  (e.g. station number is a uint8)? Probably yes, with the rejection
  surfaced via a transient `Indicator` rendered next to the input.
  Defer the exact mechanism until the AUN pilot needs it.
- **Action confirmation.** A "Restart adapter" button is destructive.
  Does the protocol express "this control needs confirmation," or is
  that the frontend's call? Lean toward a boolean flag on `Button`
  (`requires_confirmation`); cheaper than a dedicated control.
- **Internationalisation.** All labels are bare strings today. If
  the frontend wants to localise, it needs either translatable keys
  or a way to opt out. Defer; current Beebium UI is English-only.

## Out of Scope (For This Pilot)

- Streaming output (e.g. Piconet's `SubscribeMonitorFrames` packet
  capture). That's not a control panel; it belongs in its own
  RPC and its own frontend window.
- Custom rendering primitives (graphs, scopes, spectrograms).
  Out of vocabulary; if a debug surface needs one, it builds its own
  service.
- Theming, layout direction, sizing hints. The frontend owns
  presentation; the extension owns content.
- Server-pushed dialogs / modals. The protocol stays one-way for
  view, one-way for events. Anything that wants a dialog can render
  one client-side from a `Button` press.

## Verification

Architecture is proven when:

1. Piconet's three-control panel renders identically (modulo native
   styling) in the macOS frontend and the Python diagnostics client,
   driven from the same proto schema.
2. Toggling the panel's `Enabled` switch on either client changes the
   Piconet firmware mode and the change is visible on the other client
   within one stream tick.
3. AUN's panel composes from the same seven primitives with no
   schema changes.
4. Adding a new extension to the tree (or as an out-of-tree plugin)
   adds zero lines of code to any frontend; its panel appears as soon
   as the extension is loaded.

If all four hold, ship it.
