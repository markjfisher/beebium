# Beebium Lifecycle Management Implementation Plan

## Overview

This plan implements machine lifecycle management for the Beebium emulator, enabling multiple clients to connect to emulator cores, proper provenance tracking, and graceful shutdown coordination. The design derives from the discussion in `docs/discussion/menus-and-machines.md`.

## Current State

The codebase already has solid foundations:

- **gRPC services** defined in `src/service/proto/` (video, system, debugger, disc, keyboard, audio, indicator, sideways)
- **SystemService** already includes `WatchServerStatus()` for shutdown notifications
- **Python client** with `ServerProcess` for launching/managing cores, connection management
- **macOS frontend** with SwiftUI, video/audio clients, keyboard input
- **Graceful shutdown** via SIGINT/SIGTERM handling, `g_notify_clients_shutdown` callback

Key gaps for lifecycle management:
- No provenance reporting (who launched the core)
- No multi-client awareness (how many clients connected)
- No machine enumeration/discovery
- Frontend doesn't track machine relationships
- No Connect dialog or New Machine dialog infrastructure

---

## Phase 1: Provenance Reporting

**Goal**: Cores know and report how they were launched.

See [provenance-reporting.md](provenance-reporting.md) for detailed design.

---

## Phase 2: Machine Identity

**Goal**: Cores have stable identity (UUID) and human-readable names.

See [machine-identity.md](machine-identity.md) for detailed design.

---

## Phase 3: Client Connection Tracking

**Goal**: Cores track connected clients and report connection count.

See [client-connection-tracking.md](client-connection-tracking.md) for detailed design.

---

## Phase 4: Shutdown RPC

**Goal**: Distinguish between "disconnect UI" and "stop machine" via an explicit shutdown RPC.

See [shutdown-rpc.md](shutdown-rpc.md) for detailed design.

---

## Phase 5: Service Advertisement (Bonjour/mDNS)

**Goal**: Cores advertise themselves for discovery.

See [service-advertisement.md](service-advertisement.md) for detailed design.

---

## Phase 6: File Menu Skeleton

**Goal**: Establish the complete File menu structure with stub implementations.

See [file-menu-skeleton.md](file-menu-skeleton.md) for detailed design.

---

## Phase 7: Connect Dialog

**Goal**: File > Connect to Machine… dialog with discovery.

See [connect-dialog.md](connect-dialog.md) for detailed design.

---

## Phase 7.5: Settings Infrastructure

**Goal**: Establish the Preferences window skeleton with placeholder panes.

See [settings-infrastructure.md](settings-infrastructure.md) for detailed design.

---

## Phase 7.6: Preset System

**Goal**: Define machine presets and preset management UI.

See [preset-system.md](preset-system.md) for detailed design.

---

## Phase 8: New Machine Dialog

**Goal**: File > New… with preset selection and configuration.

See [new-machine-dialog.md](new-machine-dialog.md) for detailed design.

---

fGraceful Window Close & App Quit

**Goal**: Lifecycle-aware window close and app quit with no-nag casual use, power user control via unlink.

### 9.1 Window Close Behavior

Three behaviors based on machine lifecycle state:
1. **Launched core + sole client**: Auto-shutdown via `RequestShutdown` RPC. No dialog.
2. **Externally-connected core**: Silent disconnect. No dialog.
3. **Launched core + other clients**: Alert with "Shut Down" / "Leave Running" / "Cancel".

### 9.2 App Quit Behavior

Quit rule: shut down a core only if BOTH (a) this app launched it AND (b) it's the sole client.
Everything else is silently disconnected. No quit dialog ever.

### 9.3 MachineManager

Central `@MainActor` singleton (`MachineManager.shared`) tracking launched server processes, their
provenance UUIDs, connection targets, lifetime-linked state, and cached client count.

### 9.4 Provenance Fix

`PresetManager.launchCore()` now passes `--provenance-type macos-gui --provenance-uuid <uuid>` to the
server, enabling `RequestShutdown` authorization via the `ShutdownPolicyEvaluator`.

### 9.5 SystemClient Enhancements

- `WatchServerStatus` streaming (counted by server's `ConnectionTracker`)
- `RequestShutdown` RPC with provenance UUID in `x-beebium-instance-uuid` metadata
- `fetchClientCount()` for sole-client detection at close time

### 9.6 Status Bar Lifetime Indicator

`link` SF Symbol in `StatusBarView` when connected to a lifetime-linked core. Clickable to unlink
(machine keeps running but closing the window becomes a silent disconnect).

### 9.7 WindowCloseCoordinator

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

### Files created:
- `clients/macos/Beebium/Beebium/MachineManager.swift`

### Files modified:
- `clients/macos/Beebium/Beebium/Generated/system.{pb,grpc}.swift` (regenerated)
- `clients/macos/Beebium/Beebium/Presets/PresetManager.swift` (provenance args + LaunchedCore field)
- `clients/macos/Beebium/Beebium/NewMachineDialog.swift` (register with MachineManager)
- `clients/macos/Beebium/Beebium/ConnectDialog.swift` (pendingProvenanceUUID)
- `clients/macos/Beebium/Beebium/SystemClient.swift` (WatchServerStatus, RequestShutdown, client count)
- `clients/macos/Beebium/Beebium/ContentView.swift` (WindowCloseCoordinator, provenance plumbing)
- `clients/macos/Beebium/Beebium/StatusBarView.swift` (link indicator)
- `clients/macos/Beebium/Beebium/BeebiumApp.swift` (AppDelegate quit handler)

### Verification:
- Launch core, close window -> core shuts down (verify with `ps`)
- Launch core, connect Python client, close window -> multi-client alert
- Connect to external core, close window -> silent disconnect
- Status bar link icon visible, click to unlink
- Cmd+Q shuts down sole-client cores, leaves multi-client cores running

---

## Phase 10: Welcome Window

**Goal**: Provide a welcoming startup experience with quick access to presets and recent states.

### 10.1 Window Design

```
┌─────────────────────────────────────────────────┐
│  Beebium                                        │
│                                                 │
│  Get Started                                    │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐           │
│  │ [thumb] │ │ [thumb] │ │ [thumb] │  New...   │
│  │ Model B │ │Model B+ │ │Master128│           │
│  │  (DFS)  │ │         │ │         │           │
│  └─────────┘ └─────────┘ └─────────┘           │
│                                                 │
│  Recent                                    ▼    │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐           │
│  │ [thumb] │ │ [thumb] │ │ [thumb] │           │
│  │ Elite   │ │ Chuckie │ │ Testing │           │
│  │ 2h ago  │ │yesterday│ │ 3 days  │           │
│  └─────────┘ └─────────┘ └─────────┘           │
│                                                 │
│  ☐ Show this window when Beebium opens         │
└─────────────────────────────────────────────────┘
```

### 10.2 Behaviour

- **On app launch**: Show welcome window (unless preference disabled)
- **Preset thumbnails**: Default boot screen for each model
- **Recent thumbnails**: Last framebuffer capture from saved state
- **Pre-selection**: First preset selected by default; user can just press Return to launch
- **New... button**: Opens full New Machine dialog for custom configuration
- **Re-open**: Window > Welcome to Beebium

### 10.3 Empty State (First Launch)

With no recent states, window shows only presets — still useful for quick launch.

### Files to create/modify:
- `clients/macos/Beebium/Beebium/WelcomeWindow.swift` (new)
- `clients/macos/Beebium/Beebium/BeebiumApp.swift` (show on launch)
- Window menu additions

### Dependencies:
- Phase 7.6 (Preset System) for preset data and PresetManager
- Phase 8 (New Machine Dialog) for machine creation flow
- State persistence (future) for recent states with thumbnails

### Verification:
- Window appears on launch
- Selecting preset and pressing Return launches machine
- "Show this window" preference persists
- Window > Welcome to Beebium re-opens it

---

## Phase 11: Machines Menu (Deferred)

**Goal**: Implement the Machines menu as designed in menus-and-machines.md.

**Status**: Deferred until earlier phases are complete. The need for a separate Machines menu (vs relying on the Window menu) will be reassessed once we have working Connect and New Machine dialogs.

The original design envisioned a Machines menu showing:
- Running Locally (machines this frontend launched)
- Connected (machines we connected to)
- Available on Network (discovered via mDNS, not yet connected)

This may overlap with the standard Window menu, which lists open windows. The interaction between Machines menu and Window menu — especially when multiple windows can view the same machine — needs further design work.

---

## Phase 12: Lifecycle Refactoring

**Goal**: Clean up the lifecycle management code once all behaviour is correct and tested.

### 12.1 Client Collection Protocol

Currently, each gRPC client (`VideoClient`, `KeyboardClient`, `SystemClient`, `IndicatorClient`,
`DiscClient`, `AudioClient`, `DebuggerClient`) is managed individually. Connecting and disconnecting
requires enumerating all seven clients by name in multiple places (ContentView's `onAppear`,
`onChange(of: connectionState)`, `onDisappear`, and the coordinator's `disconnectClients` callback).

Introduce a protocol (e.g., `GRPCClient`) with a `disconnect()` method that all clients conform to,
and a collection type (e.g., `ClientGroup`) that manages them as a unit. This would replace the
seven individual `disconnect()` calls with a single `clientGroup.disconnectAll()`, and make it
impossible to forget a client when adding new ones.

### 12.2 Remove Dead onDisappear Code

`onDisappear` does not fire for SwiftUI `WindowGroup` windows. The cleanup code in `onDisappear`
is dead code in practice. Once the coordinator-based cleanup is proven reliable, remove the
`onDisappear` handler (or reduce it to a diagnostic log) to avoid confusion about which code path
actually runs.

### 12.3 Remove Diagnostic Logging

Strip the diagnostic `NSLog` statements added during Phase 9 debugging (WindowAccessor logging,
MachineManager state dumps at connect time, coordinator verification logging). Keep the
operationally useful logs (coordinator action decisions, SIGTERM sends, registration/unregistration).

### 12.4 Review tearingDown Flag

The `tearingDown` flag was introduced to prevent `onChange(of: connectionState)` from racing with
`onDisappear`. Since `onDisappear` doesn't fire, the flag may be unnecessary. Investigate whether
it can be removed or whether it guards against other races.

---

## Deferred: Multi-Window Support

The File menu skeleton includes "New Window" (⌘⇧N) but it is disabled initially. The current architecture allows multiple windows to view the same machine (useful for viewing different sidebar tabs simultaneously), but enabling this adds complexity to:
- Window menu semantics
- Quit dialog (which windows to close?)
- Potential future Machines menu

The menu item is present so the full structure is visible, but the implementation is deferred until the core lifecycle management is solid.

---

## Dependency Graph

```
Phase 1 (Provenance)
    │
    v
Phase 2 (Machine Identity)
    │
    v
Phase 3 (Connection Tracking)
    │
    v
Phase 4 (Shutdown RPC)
    │
    v
Phase 5 (Discovery)
    │
    v
Phase 6 (File Menu Skeleton)
    │
    ├───────────────────────────────────────────────────┐
    v                                                   v
Phase 7 (Connect Dialog)                    Phase 7.5 (Settings Infrastructure)
    │                                                   │
    │                                                   v
    │                                       Phase 7.6 (Preset System)
    │                                                   │
    │                                                   v
    │                                       Phase 8 (New Machine Dialog)
    │                                                   │
    └───────────────────────┬───────────────────────────┘
                            v
                    Phase 9 (Window Close & Quit)
                            │
                            ├──────────────────────────┐
                            v                          v
                    Phase 10 (Welcome Window)  Phase 12 (Lifecycle Refactoring)
                            │
                            v
                    Phase 11 (Machines Menu) [deferred]
```

Phases 1-5 are backend/protocol work.
Phases 6-11 are primarily frontend.
Phase 7 (Connect Dialog) and Phase 7.5-8 (Settings/Presets/New Machine) can be developed in parallel.
Phase 7.6 (Preset System) requires Phase 7.5 (Settings Infrastructure).
Phase 8 (New Machine Dialog) requires Phase 7.6 (Preset System).
Phase 10 (Welcome Window) depends on Phase 7.6's preset system.

---

## Critical Files Summary

### New Files
- `src/discovery/Discovery.hpp`, `BonjourDiscovery.cpp`, etc.
- `clients/macos/Beebium/Beebium/FileCommands.swift`
- `clients/macos/Beebium/Beebium/MachineManager.swift`
- `clients/macos/Beebium/Beebium/ConnectDialog.swift`
- `clients/macos/Beebium/Beebium/SettingsView.swift`
- `clients/macos/Beebium/Beebium/Settings/GeneralSettingsPane.swift`
- `clients/macos/Beebium/Beebium/Settings/MachinesSettingsPane.swift`
- `clients/macos/Beebium/Beebium/Settings/KeyboardSettingsPane.swift`
- `clients/macos/Beebium/Beebium/AppSettings.swift`
- `clients/macos/Beebium/Beebium/PresetManager.swift`
- `clients/macos/Beebium/Beebium/MachinePreset.swift`
- `clients/macos/Beebium/Beebium/ConfigurationEditor.swift`
- `clients/macos/Beebium/Beebium/QuitDialog.swift`
- `clients/macos/Beebium/Beebium/NewMachineDialog.swift`
- `clients/macos/Beebium/Beebium/WelcomeWindow.swift`
- `clients/python/src/beebium/discovery.py`

### Modified Files
- `src/service/proto/system.proto`
- `src/server/ServerMain.hpp`
- `src/service/SystemService.hpp` / `.cpp`
- `src/service/Server.hpp` / `.cpp`
- `clients/python/src/beebium/server.py`
- `clients/python/src/beebium/system.py`
- `clients/macos/Beebium/Beebium/BeebiumApp.swift`

---

## Testing Strategy

### Unit Tests
- Provenance parsing and reporting
- Connection counting
- Shutdown policy logic

### Integration Tests (Python)
- Launch with provenance, verify via gRPC
- Multi-client connection counting
- Shutdown request acceptance/rejection
- Discovery (if Bonjour available)

### Manual Tests (macOS)
- File menu skeleton shows all items with correct shortcuts
- Disabled items are greyed out appropriately
- Connect dialog discovers machines and allows manual entry
- Settings window opens with ⌘, and shows all panes
- Presets pane shows default presets from discovered cores
- Duplicating a preset creates editable user preset
- User presets persist across app restarts
- New Machine dialog shows all presets and allows configuration
- New Machine creates and connects to core
- Quit dialog shows correct machines and actions
- Power Off actually terminates cores
- Welcome window appears on launch, can be re-opened from Window menu
- Selecting preset and pressing Return launches machine

---

## Open Questions

1. **Throttling**: When no clients connected, should cores throttle CPU? Pause? This affects resource usage but requires careful design.

2. **State Persistence**: Should machine state be saveable/restorable independently of connections? (Save State / Load State feature)

3. **Machines menu necessity**: Once Connect and New Machine dialogs are working, do we actually need a separate Machines menu? Or does the standard Window menu suffice? (Phase 11)

4. **Multiple windows per machine**: Currently deferred. When re-enabled, how should it interact with Window menu, quit behaviour, and potential Machines menu?
