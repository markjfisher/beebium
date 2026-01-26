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
- No Connect dialog or Machines menu infrastructure

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

## Phase 6: macOS Frontend - Machines Menu

**Goal**: Implement the Machines menu as designed in menus-and-machines.md.

### 6.1 MachineManager Class (Swift)

```swift
class MachineManager: ObservableObject {
    enum MachineRelationship {
        case launchedLocally   // We spawned it
        case connected         // We connected to existing
        case discovered        // Seen via Bonjour, not connected
    }

    struct ManagedMachine: Identifiable {
        let id: UUID
        var connection: Connection?
        var provenance: LaunchProvenance?
        var relationship: MachineRelationship
        var displayName: String
        var host: String
        var port: Int
    }

    @Published var machines: [ManagedMachine] = []

    func launchMachine(preset: Preset) async throws -> ManagedMachine
    func connectTo(host: String, port: Int) async throws -> ManagedMachine
    func disconnect(machine: ManagedMachine)
    func requestPowerOff(machine: ManagedMachine) async throws -> Bool
}
```

### 6.2 Machines Menu Structure

```
Machines
  ├─ Running Locally
  │    ├─ BBC Model B (DFS)        → bring to front / connect
  │    └─ Master 128               → bring to front / connect
  ├─ Connected
  │    └─ BBC Model B @ devbox     → bring to front
  ├─ Available on Network
  │    └─ Master 128 – lab.local   → connect
  ├─ ─────────────
  └─ Power Off All Local Machines…
```

### 6.3 Menu Implementation

Use SwiftUI `Commands` modifier:
```swift
@main
struct BeebiumApp: App {
    @StateObject var machineManager = MachineManager()

    var body: some Scene {
        WindowGroup { ... }
            .commands {
                MachinesCommands(manager: machineManager)
            }
    }
}
```

### Files to create/modify:
- `clients/macos/Beebium/Beebium/MachineManager.swift` (new)
- `clients/macos/Beebium/Beebium/MachinesCommands.swift` (new)
- `clients/macos/Beebium/Beebium/BeebiumApp.swift`

### Verification:
- Menu appears with correct grouping
- Selecting discovered machine opens connection
- Selecting connected machine brings window to front

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

## Phase 8: Quit Dialog

**Goal**: Aggregated quit dialog per menus-and-machines.md design.

### 8.1 Dialog Design

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

### 8.2 Default Actions

- Locally-launched: default to Power Off (resource concern)
- Connected: Disconnect only (cannot power off)
- Show warning for "Keep Running" about resource usage

### 8.3 "Don't Ask Again" Preference

UserDefaults key: `quitBehavior`:
- `ask` (default)
- `alwaysPowerOff`
- `alwaysKeepRunning`

### 8.4 Trigger Points

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

## Phase 9: New Machine Dialog

**Goal**: File > New Machine… with preset selection and configuration.

### 9.1 Preset System

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

### 9.2 Dialog Flow

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
Phase 4 (Shutdown RPC) ──────────────────┐
    │                                     │
    v                                     v
Phase 5 (Discovery)               Phase 6 (Machines Menu)
    │                                     │
    └──────────────┬──────────────────────┘
                   v
            Phase 7 (Connect Dialog)
                   │
                   v
            Phase 8 (Quit Dialog)
                   │
                   v
            Phase 9 (New Machine Dialog)
```

Phases 1-4 are backend/protocol work.
Phases 5-9 are primarily frontend.
Phases can be parallelized where dependencies allow.

---

## Critical Files Summary

### New Files
- `src/discovery/Discovery.hpp`, `BonjourDiscovery.cpp`, etc.
- `clients/macos/Beebium/Beebium/MachineManager.swift`
- `clients/macos/Beebium/Beebium/MachinesCommands.swift`
- `clients/macos/Beebium/Beebium/ConnectDialog.swift`
- `clients/macos/Beebium/Beebium/QuitDialog.swift`
- `clients/macos/Beebium/Beebium/NewMachineDialog.swift`
- `clients/macos/Beebium/Beebium/PresetManager.swift`
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
- Machines menu updates dynamically
- Connect dialog discovers machines
- Quit dialog shows correct machines and actions
- Power Off actually terminates cores
- New Machine creates functional machine

---

## Open Questions

1. **Throttling**: When no clients connected, should cores throttle CPU? Pause? This affects resource usage but requires careful design.

2. **State Persistence**: Should machine state be saveable/restorable independently of connections? (Save State / Load State feature)

3. **Multi-window per machine**: Should multiple windows be able to view the same machine? If so, how does this affect the Machines menu and quit behavior?
