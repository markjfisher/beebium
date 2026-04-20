# SerialPortSelector: a domain-specific control for the Extension UI framework

A proposed addition to the Extension UI framework's control vocabulary, specifically for picking a host serial port from those currently available on the machine. Beebium-specific, not general-purpose.

Status: Design proposal. Not committed to implementation.

---

## Why a dedicated control

The Extension UI framework's seven-control alphabet (`Label`, `Indicator`, `Toggle`, `Button`, `Choice`, `TextInput`, `Group`) was deliberately constrained — the stated design principle is "stretching the alphabet should hurt slightly each time". That principle made sense when we were not yet sure how many control types would be needed and wanted a forcing function against accidental growth.

Having now implemented the framework and shipped two real extensions using it, the principle still applies in spirit but the scope question is clearer. The framework is **Beebium-specific**, not a general-purpose UI toolkit. It exists to give Beebium extensions (peripheral and transport) symmetric reach into Beebium frontends. It does not need to service arbitrary applications. When a *domain-specific* control saves every affected extension from open-coding the same awkward approximation via a generic primitive, that control earns its place even though the vocabulary grows.

Serial-port selection is exactly such a case.

## The problem

Beebium has at least two consumers of host-serial-port enumeration today or imminently:

1. **Piconet transport extension.** User passes `--piconet device_path=/dev/tty.usbmodem1101` at startup. The path is awkward to discover (`ls /dev/tty.usbmodem*`) and unstable across reconnects on macOS (USB CDC renumbering). See [`piconet-device-discovery.md`](piconet-device-discovery.md) for the broader re-attachment design space.

2. **RS423 serial-port forwarding** (future). The BBC Micro's RS423 serial port is implemented in Beebium's core; it is not yet exposed to the host. When we do implement host-port forwarding (so the BBC can talk to, e.g., a modem or a serial printer attached to the host), the user needs a way to pick which host serial port to bind.

Both consumers want the same answer to the same question: *"what serial ports does the host currently have, and which one do you want?"* Today Piconet's answer is "type the full path into the config"; a future RS423 panel would presumably do the same.

Approximating this with the existing `Choice` primitive works but is ugly in every direction:

- The list of options is dynamic (USB devices come and go) but `Choice` options are served as `repeated string` — tolerable but not ergonomic for re-rendering every push.
- The display label and the stable identifier want to differ (display: "Raspberry Pi Pico (SN: E66...)", identifier: `/dev/tty.usbmodem1101`). `Choice` flattens both into one string per option.
- Each extension would independently open-code the platform-specific enumeration (`/dev` walking on POSIX, `SetupAPI` on Windows) rather than sharing one implementation.

## The proposal

Add `SerialPortSelector` as an eighth control primitive to `extension_ui.proto`:

```protobuf
message SerialPortInfo {
    // Stable identifier the selector dispatches back on the wire.
    // Typically the device path on POSIX ("/dev/tty.usbmodem1101")
    // or the COM name on Windows ("COM3"); treated as opaque by the
    // framework.
    string id = 1;

    // Short human-readable label the frontend shows to the user,
    // e.g. "Raspberry Pi Pico" or "FTDI USB Serial" derived from
    // the device's product string. Falls back to the id when no
    // product information is available.
    string label = 2;

    // Optional secondary detail (serial number, bus position, VID/PID)
    // frontends may render as a subtitle or tooltip. Empty when not
    // known.
    string description = 3;
}

message SerialPortSelector {
    // Label for the whole selector control, e.g. "Device" or "Port".
    string label = 1;

    // Currently-available serial ports. The list is server-authoritative:
    // the extension calls into a shared enumerate_serial_ports() helper
    // and populates this each build_view(). No ports -> empty list ->
    // client renders "No serial ports available".
    repeated SerialPortInfo ports = 2;

    // Identifier of the selected port, matching SerialPortInfo.id.
    // Empty when no port is selected. When the previously-selected
    // id is no longer present in ports (device was unplugged), the
    // selector stays showing that id so the user sees which port
    // they had selected and why it is now unavailable; the client
    // renders it as a stale / error state.
    string selected_id = 3;
}
```

Dispatch payload: `string_value` carrying the chosen id (same as `TextInput`). The control joins the `Control` oneof as the eighth case.

## Server-side shared enumeration

A single platform-abstracted helper lives in `beebium_core` (or a new `beebium_serial` library if weight justifies it):

```cpp
namespace beebium::serial {

struct PortInfo {
    std::string id;           // "/dev/tty.usbmodem1101" or "COM3"
    std::string label;        // "Raspberry Pi Pico"
    std::string description;  // "SN: E66..." or "USB VID:PID 2E8A:000A"
};

// Enumerate currently-attached serial ports. Thread-safe; returns a
// snapshot at call time. Implementations:
//   POSIX: glob /dev/tty.usbmodem*, /dev/tty.usbserial*, /dev/ttyACM*,
//          /dev/ttyUSB* (Linux) and cross-reference with IOKit (macOS)
//          or udev (Linux) for labels.
//   Windows: SetupAPI to enumerate GUID_DEVINTERFACE_COMPORT.
std::vector<PortInfo> enumerate_ports();

}  // namespace beebium::serial
```

Both PiconetUi and a future RS423Ui call the same helper and plug its result into their respective `SerialPortSelector` controls. No duplicated platform code.

## Client-side rendering

The macOS `ExtensionViewRenderer` gains a case for `SerialPortSelector` that produces a `Picker` with `Text(port.label)` / `.tag(port.id)` per option, plus a `Text(description).font(.caption)` subtitle for the current selection if present. Stale-selection handling: if `selected_id` is not in `ports`, render the selection as a distinct "stale" entry at the top ("⚠ Last: /dev/tty.usbmodem1101 (not present)") that the user can dismiss by picking something else.

Windows and Linux renderers, when they land, produce their platform-native equivalent.

The Python client wraps the control as a dataclass mirroring the proto; no special-case behaviour beyond what every other control type gets.

## Refresh semantics

Three options for keeping the list current:

1. **Pushed by the extension on a timer.** Extension's backend has an internal thread that periodically re-enumerates and calls `mark_dirty()` if the list changed. Rescan every few seconds. Matches the pattern used for hot-unplug detection in `PiconetBackend::reader_loop`. Cheap but adds a thread per extension.

2. **Pushed on demand via an adjacent `Refresh` Button.** User clicks Refresh, server re-enumerates and pushes. Simpler, explicit. Fine for "set it once at startup" use cases.

3. **OS-event driven** (IOKit `IOServiceAddMatchingNotification`, udev, `WM_DEVICECHANGE`). Most accurate, most platform-specific code. Probably overkill for the first cut.

Recommend **#2 (manual Refresh) for the first implementation**, with **#1 (timer-driven)** layered on top when it becomes clear users want the "I plugged it in, it just appears" experience. #3 only if #1 proves too coarse.

## Interaction with piconet-device-discovery.md

`SerialPortSelector` subsumes most of the Piconet re-attachment design problem. Instead of the user typing `device_path=/dev/tty.usbmodem*` (glob) or Beebium enumerating by VID/PID under the hood, the user simply picks the Pico from a dropdown. When the Pico is unplugged, the selector's list updates on the next refresh; when plugged back in, it reappears. The `--piconet` CLI option can remain (for startup configuration and scripting) but the GUI path becomes "open the panel, pick the Pico".

The `Reconnect` button idea from `piconet-device-discovery.md` stays useful as a separate affordance — selecting a port in the picker implicitly triggers a reconnect attempt, but the user may also want an explicit "retry the current selection" that doesn't require re-selecting.

### UX constraint: port selection is gated on the disabled / disconnected state

For Piconet specifically, the `SerialPortSelector` is only present in the panel's View when the transport is **not actively running** — either the user has clicked Disable (mode=STOP) or the adapter has hot-unplugged / never opened (serial closed). While the transport is live (serial open, mode=LISTEN), the current device is shown only as a read-only Label.

To switch to a different Piconet at runtime, the user must first:

* click the Disable button, or
* physically unplug the current adapter.

Either action transitions the panel into the "not running" state; the SerialPortSelector then appears in the next pushed View, and the user can pick a different port (or the same one again). Clicking Enable (re-)opens the selected port in LISTEN mode.

The reasoning behind the gate:

* There is no meaningful soft-cutover between two Pico adapters. Switching is fundamentally "stop using one, start using another", so forcing the user through a Disable step before Select reflects the real underlying operation.
* An active PiconetBackend owns a reader thread, a mode-switch callback, and a potentially open four-way handshake. Allowing port swaps while any of that is in flight adds failure modes (what if the new port's open fails? do we roll back the mode? what happens to frames queued for the old port?) that aren't worth modelling when the "disable first" workflow is cheap for the user.
* The rule is trivially expressible in the server-side `PiconetUi::build_view`: if `backend && backend->is_serial_open() && backend->mode() == Mode::Listen`, emit only the Label; otherwise emit both the Label (as "last selected" context) and the SerialPortSelector. The framework's conditional-inclusion machinery already supports this — no new primitive or UI concept is required beyond the SerialPortSelector itself.

Parallel structure in AUN: there's no analogous control today. If AUN ever gains runtime reconfiguration (e.g. changing the bound UDP port), the same "disable first, reconfigure, enable" pattern would apply — gate the port editor on the Disconnect state.

## Out of scope

- **Baud rate / parity / data-bits configuration.** That belongs on the extension's own panel as separate controls (probably a `Choice` for baud rate plus a few more). The `SerialPortSelector` answers the "which port?" question only.

- **Generic "device picker" for non-serial hardware.** A future extension selecting a USB MIDI device, a joystick, or anything else is not served by this control. If three such need-a-device-picker cases appear, the pattern may justify a more general `DeviceSelector<DeviceType>` but speculating that shape now is the thing we are explicitly *not* doing when we justify `SerialPortSelector` as domain-specific.

- **In-process serial port abstraction.** Beebium's existing `PosixSerialPort` and (future) `Win32SerialPort` are the I/O layer. This proposal is about *user interface for selecting* a serial port, not about the I/O abstraction. The two layers communicate via the port id string (typically a path or COM name).

## Recommended path

When picked up:

1. Write `beebium::serial::enumerate_ports()` with POSIX implementation first. Unit-testable (point it at a fake /dev tree). Land as its own commit; adds no UI machinery.

2. Add the `SerialPortSelector` message to `extension_ui.proto`. Server framework changes: extend the Dispatch payload-type validator, extend the renderer. Pure additions — existing extensions keep working.

3. Wire `SerialPortSelector` into `PiconetUi` behind a feature flag (or just as an additional control) alongside the existing device-path Label. Manual Refresh button. Verify on macOS.

4. Add a Windows implementation of `enumerate_ports()` when the Win32SerialPort work happens.

5. Consume the control from the future RS423 forwarding panel.

Scope for step 1–3: a focused ~300 LOC branch with its own verification story (enumeration correctness, stale-selection UX, refresh semantics).
