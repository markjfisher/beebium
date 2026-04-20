# Piconet Device Discovery and Re-attachment

What happens when the USB cable is unplugged and plugged back in?

This document captures the design space for handling USB CDC device renumbering and hot re-attachment for the Piconet adapter. The current Beebium implementation detects unplug events but does not re-attach when the device returns. The use case is low-frequency — most users plug in once and leave the adapter alone — but the failure mode is unpleasant when it happens, so a focused fix is warranted when convenient.

Status: Design proposal. Not committed to implementation.

---

## The Problem

USB CDC enumeration on macOS is not stable across plug/replug events or reboots. The same Raspberry Pi Pico can be `/dev/tty.usbmodem101` on one boot and `/dev/tty.usbmodem1101` on the next, with the kernel picking the next free number in its namespace each time the device enumerates. Linux is more deterministic for an unmoved device but exhibits the same issue when devices are unplugged and replugged in different orders.

Two consequences for Beebium:

1. **Initial configuration friction.** The user has to know the current path at server-launch time. After a reboot, that path may have changed; restarting the server with the old path fails with "No such file or directory". Currently the user discovers this by reading the server's startup log and figuring out the correct path with `ls /dev/tty.usbmodem*`.

2. **Mid-session loss.** When the cable is unplugged during a session, the Piconet panel correctly transitions to "Adapter offline" (the reader thread closes the serial port on read error and notifies the UI via the `on_async_state_change` callback wired into `PiconetEconetTransportExtension`). But when the user plugs the cable back in, *nothing happens*. The path may have changed; even if it has not, Beebium has no logic to retry `open()`. Recovery requires restarting the server.

## Current Implementation

Hot-unplug *detection* works (`src/extensions/piconet/src/PiconetBackend.cpp`):

- The reader thread `select()`s on the serial fd with a 100 ms timeout. On read error or hangup it calls `serial_->close()` and invokes `on_async_state_change_`, which `PiconetEconetTransportExtension` wires to `PiconetUi::mark_dirty()`. The Extension UI framework's poll loop notices the revision change and pushes a fresh `View` so the panel updates within ~50 ms.
- For the case where the device is unplugged while the firmware is silent (e.g. `SET_MODE STOP`, no events arriving), the same loop also `stat()`s the device path once per second. `ENOENT` / `ENODEV` triggers the same close-and-notify path.

Hot-attach is not implemented at all. Once `is_serial_open()` returns false, the backend stays in that state for the lifetime of the process. There is no "Reconnect" affordance on the panel and no automatic retry timer.

## Design Space

Three discovery strategies, in increasing order of robustness and implementation cost. Two reconnection triggers, orthogonal to the discovery strategy.

### Discovery Strategies

#### 1. Glob expansion in `device_path` (~30 LOC)

User writes `--piconet device_path=/dev/tty.usbmodem*`. `PosixSerialPort` (or a thin layer above it) resolves the glob at every open attempt, picking the first matching entry.

- **Pros:** Trivial implementation. No platform-specific code or new dependencies. Explainable in one sentence in the documentation.
- **Cons:** Ambiguous when multiple devices match. If the user has two Picos plugged in, the first match is whichever the OS lists first — which might be the wrong one.
- **Compatibility:** Wildcard syntax fits naturally into the existing `extension arg parser` since `*` is not currently an inner separator. No CLI quoting issues beyond the existing shell concerns documented in [networking.md](../networking.md).

#### 2. VID/PID-based USB enumeration (~150 LOC, platform-specific)

Beebium enumerates connected USB devices and picks one matching the Pico's vendor and product IDs. The IDs come either hardcoded in the Piconet extension (Pi Pico has a known VID/PID range) or as parameters in the manifest. Optional disambiguation by serial number for multi-Pico setups.

Three platform implementations behind a common `UsbDeviceEnumerator` abstraction:

- **macOS:** IOKit (`IOUSBDeviceInterface`, `IORegistryEntryCreateCFProperty`).
- **Linux:** libudev (preferred) or walk `/sys/bus/usb/devices`. Most distributions ship libudev with development headers in a small package.
- **Windows:** SetupAPI (`SetupDiGetClassDevs`, `SetupDiEnumDeviceInterfaces`). Win32SerialPort doesn't yet exist in the codebase, so this implementation lands alongside whatever future Win32 port work happens.

- **Pros:** Robust against renumbering. Robust against multiple devices when serial numbers are used. Identifies the *Pico* rather than "whatever USB CDC device shows up at this path", which avoids the "user plugged in their Arduino, Beebium tried to talk to it" failure mode.
- **Cons:** Three platform implementations to write and maintain. Adds platform-framework dependencies (IOKit on macOS, libudev-dev on Linux, SetupAPI on Windows).

#### 3. Stable symlinks via OS conventions

Linux already provides `/dev/serial/by-id/usb-Raspberry_Pi_Pico_E66...` automatically when the Pi Pico is plugged in. The path persists across reconnects because it encodes the device's iSerial. macOS has no kernel-level equivalent, though similar functionality exists via third-party tools.

- **Pros:** Zero Beebium code changes for Linux. Documentation-only fix.
- **Cons:** macOS coverage gap. The user-experience degrades to "read documentation about by-id paths" rather than "Beebium handles it".

### Reconnection Triggers

Independent of which discovery strategy is chosen.

#### Manual: Reconnect Button on the Panel

Add a `Button(id="reconnect_action", label="Reconnect")` to `PiconetUi::build_view` when `backend()->is_serial_open()` is false. The dispatch handler calls a new `PiconetEconetTransportExtension::reconnect()` method that re-runs `create_backend`'s open path (resolving the glob / re-enumerating USB / re-reading the by-id symlink). On success, the backend stores the new SerialPort and `mark_dirty`s; on failure, sets a transient error message that the panel surfaces beneath the Indicator.

- **Pros:** Predictable. User has explicit control. Easy to debug — the user knows when they triggered a reconnect attempt.
- **Cons:** User has to remember to click. For users who plug back in expecting things to "just work", this is friction.

This affordance also gives a natural home to the second interactive control on the Piconet panel, which has been deferred since Stage 5b (the original "Toggle.enabled vs hide" decision noted that a Reconnect button would be the natural next step).

#### Automatic: Background Re-attempt Timer

When the backend is in the closed state, a low-frequency timer (~5 s) re-attempts the open. Bounded back-off if the path is genuinely gone (don't hammer the OS forever).

- **Pros:** Matches USB-device user expectations. The Piconet panel transitions: "Adapter responsive" → "Adapter offline" → "Adapter responsive" without user intervention.
- **Cons:** Magical. Harder to debug when it doesn't work ("why is Beebium retrying so often / not retrying?"). Need care around resource use and back-off scheduling.

## Recommended Sequencing

When picking this work up:

1. **Glob + Reconnect button**, as one focused branch. Cheapest implementation, exercises a new framework pattern (Button as one-shot recovery action on a per-extension panel), fixes the renumbering case for the common single-Pico setup. Likely two commits: one to add glob support to `PosixSerialPort` (or a `resolve_device_path()` helper), one to add `PiconetEconetTransportExtension::reconnect()` plus the Button + dispatch handler.

2. **Automatic re-attempt** layered on top, once we have feedback on whether manual or automatic feels better in practice. May end up as both ("auto-retry by default, but the Reconnect button is always there as an escape hatch when the auto-retry has given up").

3. **VID/PID enumeration** deferred until either (a) someone has a documented multi-Pico use case, or (b) someone reports "Beebium picked the wrong USB device" because the glob match was ambiguous. The cost-benefit doesn't justify it for a single-adapter scenario.

## Scoping Note

This work is independent of, and should not be bundled into, other branches. The Extension UI framework branch (`extension-ui-framework`) deliberately does not touch USB device management; that work has its own verification surface (cross-platform USB enumeration, plug/unplug timing, multi-device disambiguation) that deserves a focused diff.

## Out of Scope

- A general "device-loss-and-recovery" abstraction for other USB-based extensions. The future Pi Econet HAT and any other USB-attached extension would have similar concerns; if a pattern emerges across two or three extensions, factor it then. Don't speculate a framework for one consumer.
- Hot-plug detection of *new* devices the user did not specify (e.g. "I plugged in a Pico, Beebium suddenly knows about it"). That's a different feature — discovery vs reconnection. Out of scope here.
- Linux udev rules that produce stable application-specific symlinks. Documentation-level fix; if it becomes a recurring suggestion, a `docs/piconet-on-linux.md` can capture the rule snippet.
