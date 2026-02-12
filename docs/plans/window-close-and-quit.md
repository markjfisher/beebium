# Phase 9: Graceful Window Close & App Quit

**Goal**: Lifecycle-aware window close and app quit with no-nag casual use, power user control via unlink.

## 9.1 Window Close Behavior

Three behaviors based on machine lifecycle state:
1. **Launched core + sole client**: Auto-shutdown via `RequestShutdown` RPC. No dialog.
2. **Externally-connected core**: Silent disconnect. No dialog.
3. **Launched core + other clients**: Alert with "Shut Down" / "Leave Running" / "Cancel".

## 9.2 App Quit Behavior

Quit rule: shut down a core only if BOTH (a) this app launched it AND (b) it's the sole client.
Everything else is silently disconnected. No quit dialog ever.

## 9.3 MachineManager

Central `@MainActor` singleton (`MachineManager.shared`) tracking launched server processes, their
provenance UUIDs, connection targets, lifetime-linked state, and cached client count.

## 9.4 Provenance Fix

`PresetManager.launchCore()` now passes `--provenance-type macos-gui --provenance-uuid <uuid>` to the
server, enabling `RequestShutdown` authorization via the `ShutdownPolicyEvaluator`.

## 9.5 SystemClient Enhancements

- `WatchServerStatus` streaming (counted by server's `ConnectionTracker`)
- `RequestShutdown` RPC with provenance UUID in `x-beebium-instance-uuid` metadata
- `fetchClientCount()` for sole-client detection at close time

## 9.6 Status Bar Lifetime Indicator

`link` SF Symbol in `StatusBarView` when connected to a lifetime-linked core. Clickable to unlink
(machine keeps running but closing the window becomes a silent disconnect).

## 9.7 WindowCloseCoordinator

Intercepts the close button's target/action (NOT `NSWindowDelegate` — SwiftUI's `WindowGroup`
manages its own delegate and overrides `windowShouldClose`). Installed via `WindowAccessor` which
captures the `NSWindow` reference.

**Critical finding**: `onDisappear` does NOT fire for SwiftUI `WindowGroup` windows when
they close. This means gRPC client cleanup (`videoClient.disconnect()`, `audioClient.disconnect()`,
etc.) must be done by the coordinator, not by `onDisappear`. The coordinator takes a
`disconnectClients` callback from ContentView and calls it before sending SIGTERM and before
`window.close()`. Without disconnecting gRPC streams first, the server's graceful shutdown hangs
waiting for active streams to close, and SIGTERM alone is insufficient because the server's signal
handler initiates a graceful shutdown that respects open connections.

## 9.8 Async-Signal-Safe Shutdown (Server Side)

The POSIX signal handler now only sets an atomic flag (`g_signal_received`). The main emulation loop
polls `dispatch_pending_signal()` from a normal (non-signal) context where mutex operations are safe.
Bounded waits (100ms) in `Machine::wait_if_paused()` and `PacingClock::wait_for_tick()` ensure the
loop can poll for shutdown signals even when blocked. On Windows, `dispatch_pending_signal()` is a
no-op since the console control handler dispatches directly from its own thread.

## Files created
- `clients/macos/Beebium/Beebium/MachineManager.swift`
- `clients/macos/Beebium/BeebiumTests/MachineManagerTests.swift`

## Files modified
- `clients/macos/Beebium/Beebium/Generated/system.{pb,grpc}.swift` (regenerated)
- `clients/macos/Beebium/Beebium/Presets/PresetManager.swift` (provenance args + LaunchedCore field)
- `clients/macos/Beebium/Beebium/NewMachineDialog.swift` (register with MachineManager)
- `clients/macos/Beebium/Beebium/ConnectDialog.swift` (pendingProvenanceUUID)
- `clients/macos/Beebium/Beebium/SystemClient.swift` (WatchServerStatus, RequestShutdown, client count)
- `clients/macos/Beebium/Beebium/ContentView.swift` (WindowCloseCoordinator, provenance plumbing)
- `clients/macos/Beebium/Beebium/StatusBarView.swift` (link indicator)
- `clients/macos/Beebium/Beebium/BeebiumApp.swift` (AppDelegate quit handler)
- `src/server/include/beebium/server/Platform.hpp` (async-signal-safe handler, dispatch_pending_signal)
- `src/server/include/beebium/server/ServerMain.hpp` (poll dispatch_pending_signal in emulation loop)
- `src/core/include/beebium/Machine.hpp` (bounded wait_if_paused)
- `src/core/include/beebium/PacingClock.hpp` (bounded wait_for_tick)
- `tests/test_server_main.cpp` (signal handler tests)

## Verification
- Launch core, close window -> core shuts down (verify with `ps`)
- Launch core, connect Python client, close window -> multi-client alert
- Connect to external core, close window -> silent disconnect
- Status bar link icon visible, click to unlink
- Cmd+Q shuts down sole-client cores, leaves multi-client cores running
