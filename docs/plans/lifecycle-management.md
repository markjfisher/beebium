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

## Phase 9: Graceful Window Close & App Quit

**Goal**: Lifecycle-aware window close and app quit with no-nag casual use, power user control via unlink.

See [window-close-and-quit.md](window-close-and-quit.md) for detailed design.

---

## Phase 10: Welcome Window

**Goal**: Provide a welcoming startup experience with quick access to presets and recent states.

See [welcome-window.md](welcome-window.md) for detailed design.

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

## Phase 12: Lifecycle Refactoring ✓

**Goal**: Clean up the lifecycle management code once all behaviour is correct and tested.

**Status**: Complete.

### 12.1 Client Collection Protocol ✓

Introduced `Disconnectable` protocol with `disconnect()` and `ClientGroup` class in
`Disconnectable.swift`. All seven gRPC client wrappers conform to `Disconnectable`. `ClientGroup`
manages them as a unit with `disconnectAll()` (all clients, VideoClient last) and
`disconnectNonVideoClients()` (for unexpected server disconnections). VideoClient is registered
separately because it owns the shared GRPCChannel. `WindowCloseCoordinator` now takes a
`ClientGroup` directly instead of a closure with seven weak captures.

### 12.2 Remove Dead onDisappear Code ✓

Removed the entire `onDisappear` block from ContentView. All cleanup is handled by
`WindowCloseCoordinator`, which intercepts both close-button clicks and Cmd+W (via `performClose:`
routing through the close button's redirected action).

### 12.3 Remove Diagnostic Logging ✓

Stripped ~20 diagnostic `NSLog` statements from ContentView (lifecycle tracing, state dumps,
coordinator verification), VideoClient (per-second frame stats), MetalRenderer (per-second update
stats), and BeebiumApp (startup marker). Retained operational logs in MachineManager (shutdown
decisions and actions), SystemClient (shutdown RPC results), ConnectionRegistry
(register/unregister), and WindowCloseCoordinator (close handler decisions and warnings).

### 12.4 Review tearingDown Flag ✓

Removed. The flag was only ever set inside `onDisappear` (which never fires for WindowGroup
windows), so it was never true, making its `!tearingDown` guard in `onChange` a permanent no-op.
All seven clients are idempotent for `disconnect()`, so double-disconnect from the `onChange`
handler firing after `WindowCloseCoordinator` calls `disconnectAll()` is harmless.

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
