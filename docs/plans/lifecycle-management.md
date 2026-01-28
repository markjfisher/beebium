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

### 6.1 Menu Structure

```
File
 ├─ New…                       ⌘N
 │    (Create a new machine; opens configuration dialog)
 ├─ New Window                 ⌘⇧N
 │    (Open a new window for the currently selected machine)
 ├─ Open…                      ⌘O
 │    (Open saved machine state)
 ├─ Open Recent               ▶
 │    (List of recently opened machine state files)
 ├─ Connect…
 │    (Connect to an already-running machine; local or remote)
 ├─ ─────────────
 ├─ Save                       ⌘S
 │    (Save current machine state)
 ├─ Save As…                   ⇧⌘S
 │    (Save current machine state under a new name/path)
 ├─ Revert To                 ▶
 │    (Revert to last saved state)
 ├─ ─────────────
 └─ Close                      ⌘W
      (Close the current window; does not necessarily stop the machine)
```

### 6.2 Design Rationale

| Item | Purpose | macOS Convention |
|------|---------|------------------|
| New… | Create a new machine via configuration dialog | Standard ⌘N for "new document" |
| New Window | Open additional window for current machine | Standard ⌘⇧N for multi-window apps |
| Open… | Load saved machine state from file | Standard ⌘O |
| Open Recent | Quick access to recent state files | Standard submenu |
| Connect… | Attach to already-running core (local or remote) | No shortcut (⌘K used for keyboard switching) |
| Save / Save As… | Persist machine state | Standard ⌘S / ⇧⌘S |
| Revert To | Restore previous saved state | Standard submenu |
| Close | Close window only; machine may keep running | Standard ⌘W |

This menu treats **machine state** as the "document", which aligns with how users think about emulator sessions.

### 6.3 Initial Implementation

All items present but most disabled or showing placeholder alerts:

```swift
struct FileCommands: Commands {
    @ObservedObject var machineManager: MachineManager

    var body: some Commands {
        CommandGroup(replacing: .newItem) {
            Button("New…") {
                // TODO: Phase 8 - New Machine Dialog
                showNotImplementedAlert("New Machine")
            }
            .keyboardShortcut("n", modifiers: .command)

            Button("New Window") {
                // TODO: Future - Multi-window support
                showNotImplementedAlert("New Window")
            }
            .keyboardShortcut("n", modifiers: [.command, .shift])
            .disabled(true)  // Disabled until multi-window implemented

            Divider()

            Button("Open…") {
                // TODO: Future - State persistence
                showNotImplementedAlert("Open State")
            }
            .keyboardShortcut("o", modifiers: .command)
            .disabled(true)

            // Open Recent submenu - disabled stub
            Menu("Open Recent") {
                Text("No Recent Items")
                    .disabled(true)
            }
            .disabled(true)

            Divider()

            Button("Connect…") {
                // TODO: Phase 7 - Connect Dialog
                showNotImplementedAlert("Connect")
            }
        }

        CommandGroup(replacing: .saveItem) {
            Button("Save") {
                // TODO: Future - State persistence
            }
            .keyboardShortcut("s", modifiers: .command)
            .disabled(true)

            Button("Save As…") {
                // TODO: Future - State persistence
            }
            .keyboardShortcut("s", modifiers: [.command, .shift])
            .disabled(true)

            Menu("Revert To") {
                Text("No Saved Versions")
                    .disabled(true)
            }
            .disabled(true)
        }
    }
}
```

### 6.4 Enablement Roadmap

| Item | Enabled In |
|------|------------|
| New… | Phase 8 (New Machine Dialog) |
| New Window | Future (Multi-window support) |
| Open… | Future (State persistence) |
| Open Recent | Future (State persistence) |
| Connect… | Phase 7 (Connect Dialog) |
| Save | Future (State persistence) |
| Save As… | Future (State persistence) |
| Revert To | Future (State persistence) |
| Close | Phase 6 (window management) |

### Files to create/modify:
- `clients/macos/Beebium/Beebium/FileCommands.swift` (new)
- `clients/macos/Beebium/Beebium/BeebiumApp.swift` (integrate FileCommands)

### Verification:
- All menu items appear with correct labels and shortcuts
- Disabled items are visually greyed out
- Enabled stubs show placeholder alerts
- Close (⌘W) works for window management

---

## Phase 7: Connect Dialog

**Goal**: File > Connect to Machine… dialog with discovery.

### 7.1 Dialog UI

```swift
struct ConnectDialog: View {
    @State var discoveredMachines: [DiscoveredMachine] = []
    @State var selectedMachine: DiscoveredMachine?
    @State var manualHost: String = "localhost"
    @State var manualPort: String = "48875"
    @State var useManual: Bool = false

    // Two modes: discovered list or manual entry
}
```

### 7.2 Recent Connections

Store in UserDefaults:
```swift
struct RecentConnection: Codable {
    let host: String
    let port: Int
    let displayName: String?
    let lastUsed: Date
}
```

Show in dialog as quick-access list.

### 7.3 Error Handling

On connection failure:
- Stay in dialog
- Show inline error: "Couldn't connect to localhost:48875"
- Allow retry or different selection

### Files to create/modify:
- `clients/macos/Beebium/Beebium/ConnectDialog.swift` (new)
- `clients/macos/Beebium/Beebium/RecentConnections.swift` (new)

### Verification:
- Dialog shows discovered machines
- Manual entry works
- Recent connections remembered across app launches

---

## Phase 8: New Machine Dialog

**Goal**: File > New… with preset selection and configuration.

### 8.1 Preset System

```swift
struct MachinePreset: Codable, Identifiable {
    let id: UUID
    let name: String
    let coreExecutable: String  // "beebium-model-b"
    let configuration: [String: AnyCodable]
    let isDefault: Bool  // Default presets are read-only
}

class PresetManager: ObservableObject {
    @Published var defaultPresets: [MachinePreset] = []
    @Published var userPresets: [MachinePreset] = []

    func loadDefaultPresets()  // Enumerate core executables
    func duplicatePreset(_ preset: MachinePreset) -> MachinePreset
    func deletePreset(_ preset: MachinePreset)
}
```

### 8.2 Dialog Flow

1. Show preset picker (dropdown at top)
2. Show model info (from selected preset)
3. Show editable configuration (from core's describe-configuration)
4. Cancel / Create buttons
5. Create launches core with configuration, connects, opens window

### Files to create/modify:
- `clients/macos/Beebium/Beebium/PresetManager.swift` (new)
- `clients/macos/Beebium/Beebium/NewMachineDialog.swift` (new)
- `clients/macos/Beebium/Beebium/MachineConfigurationView.swift` (new)

### Verification:
- Default presets appear from discovered core executables
- Duplicating preset creates editable copy
- New Machine creates and connects to core

---

## Phase 9: Quit Dialog

**Goal**: Aggregated quit dialog per menus-and-machines.md design.

### 9.1 Dialog Design

```swift
struct QuitDialog: View {
    let machines: [ManagedMachine]
    @State var actions: [UUID: QuitAction] = [:]

    enum QuitAction {
        case powerOff
        case keepRunning
        case disconnect  // For connected-only machines
    }
}
```

### 9.2 Default Actions

- Locally-launched: default to Power Off (resource concern)
- Connected: Disconnect only (cannot power off)
- Show warning for "Keep Running" about resource usage

### 9.3 "Don't Ask Again" Preference

UserDefaults key: `quitBehavior`:
- `ask` (default)
- `alwaysPowerOff`
- `alwaysKeepRunning`

### 9.4 Trigger Points

- `applicationShouldTerminate:` in AppDelegate
- Show dialog only if locally-launched machines exist

### Files to create/modify:
- `clients/macos/Beebium/Beebium/QuitDialog.swift` (new)
- `clients/macos/Beebium/Beebium/BeebiumApp.swift` (quit handling)

### Verification:
- Quit with running machines shows dialog
- Power Off actually terminates cores
- Keep Running leaves cores running (verify with `ps`)

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
- Phase 8 (New Machine Dialog) for preset system
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
    ├───────────────────────────────┐
    v                               v
Phase 7 (Connect Dialog)    Phase 8 (New Machine Dialog)
    │                               │
    └───────────┬───────────────────┘
                v
        Phase 9 (Quit Dialog)
                │
                v
        Phase 10 (Welcome Window)
                │
                v
        Phase 11 (Machines Menu) [deferred]
```

Phases 1-5 are backend/protocol work.
Phases 6-11 are primarily frontend.
Phases 7 and 8 can be developed in parallel.
Phase 10 depends on Phase 8's preset system.

---

## Critical Files Summary

### New Files
- `src/discovery/Discovery.hpp`, `BonjourDiscovery.cpp`, etc.
- `clients/macos/Beebium/Beebium/FileCommands.swift`
- `clients/macos/Beebium/Beebium/MachineManager.swift`
- `clients/macos/Beebium/Beebium/ConnectDialog.swift`
- `clients/macos/Beebium/Beebium/QuitDialog.swift`
- `clients/macos/Beebium/Beebium/NewMachineDialog.swift`
- `clients/macos/Beebium/Beebium/PresetManager.swift`
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
- New Machine dialog launches cores with selected configuration
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
