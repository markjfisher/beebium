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

## Phase 2: Client Connection Tracking

**Goal**: Cores track connected clients and report connection count.

### 2.1 Protocol Extension

```protobuf
message ConnectionInfo {
  int32 client_count = 1;
  bool has_video_subscriber = 2;
  bool has_audio_subscriber = 3;
}

message GetSystemInfoResponse {
  // existing fields...
  ConnectionInfo connections = 4;  // NEW
}

// Or a dedicated RPC
rpc GetConnectionInfo(Empty) returns (ConnectionInfo);
```

### 2.2 Server-Side Tracking

In gRPC Server class:
- Track active stream counts per service type
- Increment on stream start, decrement on stream end
- Expose via SystemService

### 2.3 Connection Events (Optional Stretch)

```protobuf
message ConnectionEvent {
  enum Type {
    CLIENT_CONNECTED = 0;
    CLIENT_DISCONNECTED = 1;
  }
  Type type = 1;
  int32 client_count = 2;
}

rpc WatchConnections(Empty) returns (stream ConnectionEvent);
```

### Files to modify:
- `src/service/proto/system.proto`
- `src/service/Server.hpp` / `.cpp` (track connections)
- `src/service/SystemService.hpp` / `.cpp`

### Verification:
- Connect two Python clients, verify `client_count == 2`
- Disconnect one, verify count decrements

---

## Phase 3: Graceful Disconnect vs Power Off

**Goal**: Distinguish between "disconnect UI" and "stop machine".

### 3.1 Explicit Shutdown RPC

```protobuf
enum ShutdownMode {
  SHUTDOWN_GRACEFUL = 0;    // Normal shutdown with grace period
  SHUTDOWN_IMMEDIATE = 1;   // Stop now
}

message ShutdownRequest {
  ShutdownMode mode = 1;
  int32 grace_period_ms = 2;  // 0 = use default (5000ms)
}

message ShutdownResponse {
  bool accepted = 1;
  string message = 2;  // e.g., "Shutdown refused: multiple clients connected"
}

rpc RequestShutdown(ShutdownRequest) returns (ShutdownResponse);
```

### 3.2 Shutdown Policy

Server accepts shutdown request if:
- Provenance allows it (launched by requesting client type), OR
- `--allow-remote-shutdown` flag was set, OR
- Only one client connected

If refused, return `accepted = false` with explanation.

### 3.3 Python Client Updates

```python
# In Beebium class
def request_shutdown(self, graceful=True, grace_period_ms=5000):
    """Request server shutdown. Returns (accepted, message)."""

# In ServerProcess
def stop(self, timeout=5.0):
    # Try graceful gRPC shutdown first
    # Fall back to SIGTERM if connection lost
```

### Files to modify:
- `src/service/proto/system.proto`
- `src/service/SystemService.hpp` / `.cpp`
- `clients/python/src/beebium/system.py`
- `clients/python/src/beebium/server.py`

### Verification:
- Python-launched server: `request_shutdown()` succeeds
- Externally-launched server: `request_shutdown()` returns `accepted=False` (unless allowed)

---

## Phase 4: Service Advertisement (Bonjour/mDNS)

**Goal**: Cores advertise themselves for discovery.

### 4.1 DNS-SD Service Type

Service type: `_beebium._tcp`

TXT records:
- `machine=BBC Model B`
- `provenance=macos-gui`
- `version=0.4.1`

### 4.2 Server-Side Advertisement

Add `--advertise` flag (default: off for TTY, on for GUI-launched):
```
--advertise              Enable mDNS advertisement
--advertise-name <name>  Override advertised name
```

Use platform-appropriate library:
- macOS: `dns_sd.h` (built-in)
- Linux: Avahi
- Cross-platform fallback: skip advertisement

### 4.3 Discovery Client Library

Create `src/discovery/` with:
- `Discovery.hpp` - abstract interface
- `BonjourDiscovery.cpp` - macOS implementation
- `AvahiDiscovery.cpp` - Linux implementation
- `NullDiscovery.cpp` - fallback

Python bindings via zeroconf library or custom gRPC service.

### Files to create/modify:
- `src/discovery/` (new directory)
- `src/server/ServerMain.hpp` (add `--advertise` handling)
- `clients/python/src/beebium/discovery.py` (new)

### Verification:
- Start server with `--advertise`, discover via `dns-sd -B _beebium._tcp`
- Python: `beebium.discovery.browse()` returns list of machines

---

## Phase 5: macOS Frontend - Machines Menu

**Goal**: Implement the Machines menu as designed in menus-and-machines.md.

### 5.1 MachineManager Class (Swift)

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

### 5.2 Machines Menu Structure

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

### 5.3 Menu Implementation

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

## Phase 6: Connect Dialog

**Goal**: File > Connect to Machine… dialog with discovery.

### 6.1 Dialog UI

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

### 6.2 Recent Connections

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

### 6.3 Error Handling

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

## Phase 7: Quit Dialog

**Goal**: Aggregated quit dialog per menus-and-machines.md design.

### 7.1 Dialog Design

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

### 7.2 Default Actions

- Locally-launched: default to Power Off (resource concern)
- Connected: Disconnect only (cannot power off)
- Show warning for "Keep Running" about resource usage

### 7.3 "Don't Ask Again" Preference

UserDefaults key: `quitBehavior`:
- `ask` (default)
- `alwaysPowerOff`
- `alwaysKeepRunning`

### 7.4 Trigger Points

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

## Phase 8: New Machine Dialog

**Goal**: File > New Machine… with preset selection and configuration.

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

## Dependency Graph

```
Phase 1 (Provenance)
    │
    v
Phase 2 (Connection Tracking)
    │
    v
Phase 3 (Shutdown RPC) ──────────────────┐
    │                                     │
    v                                     v
Phase 4 (Discovery)               Phase 5 (Machines Menu)
    │                                     │
    └──────────────┬──────────────────────┘
                   v
            Phase 6 (Connect Dialog)
                   │
                   v
            Phase 7 (Quit Dialog)
                   │
                   v
            Phase 8 (New Machine Dialog)
```

Phases 1-3 are backend/protocol work.
Phases 4-8 are primarily frontend.
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
