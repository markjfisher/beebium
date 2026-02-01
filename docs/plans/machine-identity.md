# Machine Identity Design

## Overview

Machine identity gives each emulator core a stable UUID and a human-readable name. This allows frontends to track, identify, and meaningfully label machines across connections, sessions, and multi-client scenarios.

Together with provenance (Phase 1), machine identity completes the core's self-knowledge:

| Concept | Purpose | Visible to Users? |
|---------|---------|-------------------|
| UUID | Stable identity for reconnection, logs, coordination | No (diagnostics only) |
| Name | Human meaning ("Teletext Server", "Level 3 Fileserver") | Yes |
| Provenance | How it was launched | Softly (supporting detail) |

## Design Principles

1. **Separation of concerns**: UUID for machines, name for humans
2. **Name ownership by core**: All clients see the same name
3. **Editability**: Names can be changed at any time without affecting identity
4. **Sensible defaults**: Machines have useful names even if never explicitly named

## Machine UUID

### Properties

- **Opaque**: Never shown to users except in diagnostics
- **Stable**: Remains constant for the lifetime of the machine instance
- **Generated**: Created at machine startup if not provided via CLI
- **Format**: RFC 4122 UUID (same format as provenance `instance_uuid`)

### Use Cases

| Use Case | How UUID Helps |
|----------|----------------|
| Reconnection | Client can find "the same machine" after disconnect |
| Multi-client | Distinguish between two "BBC Model B" instances |
| Logging | Correlate events across sessions |
| Persisted references | Templates, history, bookmarks can reference specific machines |
| Bonjour | TXT record includes UUID for unambiguous identification |

### UUID vs Provenance UUID

These are different concepts:

| Field | Belongs To | Lifetime | Purpose |
|-------|------------|----------|---------|
| Machine UUID | The machine | Machine lifetime | Identity |
| Provenance `instance_uuid` | The launcher | Launch session | Session tracking |

A machine keeps its UUID even if reconnected by a different client. The provenance UUID identifies who launched it and when.

## Machine Name (Label)

### Properties

- **Human-readable**: Descriptive text meaningful to users
- **Editable**: Can be changed at any time via gRPC
- **Non-unique**: Multiple machines can have the same name
- **Contextual**: Describes what the machine is *for*, not what it *is*

### Examples

Good names describe purpose or role:
- "Level 3 Fileserver"
- "Print Server"
- "Teletext Server"
- "Master 128 – Econet"
- "Test Machine (DFS)"

Avoid names that just repeat technical info:
- "BBC Model B" (redundant with model_type)
- "Machine 1" (meaningless)
- "localhost:48875" (connection details, not identity)

### Default Naming Strategy

On creation, derive a sensible default from available context:

| Source | Example Default |
|--------|-----------------|
| Model only | "BBC Model B" |
| Model + DFS | "BBC Model B (DFS)" |
| Model + preset | "Master 128 – Econet" |

Users can rename at any time but are never forced to.

## Protocol Design

### Message Definition (`system.proto`)

```protobuf
message MachineIdentity {
  // RFC 4122 UUID, stable for machine lifetime
  string uuid = 1;

  // Human-readable label, editable
  string name = 2;

  // Machine type (e.g., "model-b", "master-128")
  // Immutable, set at creation
  string model_type = 3;

  // Full model name for UI (e.g., "BBC Model B")
  // Immutable, set at creation
  string model_name = 4;
}

// SystemInfo response (old fields removed)
message SystemInfo {
  reserved 1, 2;
  reserved "machine_type", "machine_display_name";
  LaunchProvenance provenance = 3;
  MachineIdentity identity = 4;
}

// Rename RPC
message SetMachineNameRequest {
  string name = 1;
}

message SetMachineNameResponse {
  MachineIdentity identity = 1;  // Returns updated identity
}

rpc SetMachineName(SetMachineNameRequest) returns (SetMachineNameResponse);
```

### Name Change Notification

For multi-client scenarios, clients are notified when the name changes via the existing `WatchServerStatus` stream:

```protobuf
enum ServerStatusType {
  SERVER_STATUS_READY = 0;
  SERVER_STATUS_SHUTTING_DOWN = 1;
  SERVER_STATUS_IDENTITY_CHANGED = 2;  // NEW
}

message ServerStatusEvent {
  ServerStatusType status = 1;
  string message = 2;
  uint32 shutdown_grace_ms = 3;
  MachineIdentity identity = 4;  // Populated for IDENTITY_CHANGED events
}
```

When a client calls `SetMachineName`, all `WatchServerStatus` subscribers receive an `IDENTITY_CHANGED` event containing the updated identity.

## CLI Interface

```
--machine-uuid <uuid>    Override machine UUID (default: auto-generated)
--machine-name <name>    Set initial machine name (default: from model)
```

Example:
```bash
beebium-model-b start \
  --machine-uuid 12345678-1234-1234-1234-123456789abc \
  --machine-name "Teletext Server"
```

### Validation

- `--machine-uuid`: Must be valid RFC 4122 UUID (same validation as provenance)
- `--machine-name`: May have length limits (e.g., max 100 chars), but generally permissive

## Name Ownership

The machine name **belongs to the core**, not any frontend:

```
                    ┌─────────────────┐
                    │  Emulator Core  │
                    │                 │
                    │  name: "Tele-   │
                    │   text Server"  │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
        ┌──────────┐  ┌──────────┐  ┌──────────┐
        │  macOS   │  │  Python  │  │  Web UI  │
        │   GUI    │  │  Client  │  │ (future) │
        └──────────┘  └──────────┘  └──────────┘
              │              │              │
              └──────────────┴──────────────┘
                      All see:
                 "Teletext Server"
```

This ensures:
- Multi-client setups see the same name
- Renaming propagates to all clients
- Headless servers have meaningful names
- Bonjour advertisements match what clients see

## Renaming Semantics

When a machine is renamed:

1. **UUID unchanged**: Identity is stable
2. **Immediate propagation**: All connected clients see the new name
3. **Bonjour update**: Advertisement refreshed with new name
4. **No confirmation needed**: Rename is instant (like renaming a file)

Example flow:
```
Client A: SetMachineName("Print Server")
  → Core updates name
  → Client A receives success
  → Client B's next GetSystemInfo returns "Print Server"
  → Bonjour re-advertises with new name
```

## Implementation

### Files Modified

- `src/service/proto/system.proto` - Add `MachineIdentity` message, `SetMachineName` RPC, extend `ServerStatusType`
- `src/service/include/beebium/service/SystemService.hpp` - Add `MachineIdentity` struct, `SetMachineName` impl
- `src/service/include/beebium/service/Server.hpp` - Pass identity to SystemService
- `src/server/include/beebium/server/ServerMain.hpp` - Add CLI flag parsing, UUID generation
- `clients/python/src/beebium/system.py` - Add `MachineIdentity` class with name property setter
- `clients/python/src/beebium/__init__.py` - Export `MachineIdentity`, `ServerStatus`, `ServerStatusEvent`
- `clients/macos/Beebium/Beebium/Generated/system.pb.swift` - Regenerated proto bindings
- `clients/macos/Beebium/Beebium/SystemClient.swift` - Use identity, add `setMachineName()`
- `oracle/src/types.ts` - Add `MachineIdentity`, `IDENTITY_CHANGED` status
- `oracle/src/beebium-client.ts` - Handle identity in `ServerStatusWatcher`

### C++ Server Side

```cpp
struct MachineIdentity {
    std::string uuid;
    std::string name;
    std::string model_type;
    std::string model_name;
};

// In SystemService:
class SystemServiceImpl {
    MachineIdentity identity_;
    std::mutex watchers_mutex_;  // Protects identity and watchers

public:
    grpc::Status SetMachineName(grpc::ServerContext* context,
                                const SetMachineNameRequest* request,
                                SetMachineNameResponse* response) {
        std::lock_guard<std::mutex> lock(watchers_mutex_);
        identity_.name = request->name();
        // Populate response with updated identity
        auto* id = response->mutable_identity();
        id->set_uuid(identity_.uuid);
        id->set_name(identity_.name);
        id->set_model_type(identity_.model_type);
        id->set_model_name(identity_.model_name);
        // Notify watchers of identity change
        notify_identity_changed();
        return grpc::Status::OK;
    }
};
```

### Python Client Side

```python
class MachineIdentity:
    """Machine identity with mutable name property.

    The name property triggers a gRPC call when set.
    """

    def __init__(self, uuid: str, name: str, model_type: str,
                 model_name: str, system: "System"):
        self._uuid = uuid
        self._name = name
        self._model_type = model_type
        self._model_name = model_name
        self._system = system

    @property
    def uuid(self) -> str:
        return self._uuid

    @property
    def name(self) -> str:
        return self._name

    @name.setter
    def name(self, value: str) -> None:
        """Set machine name via gRPC."""
        request = SetMachineNameRequest(name=value)
        response = self._system._stub.SetMachineName(request)
        self._name = response.identity.name

    @property
    def model_type(self) -> str:
        return self._model_type

    @property
    def model_name(self) -> str:
        return self._model_name

class System:
    @property
    def identity(self) -> MachineIdentity:
        """Get machine identity (UUID and name)."""
        info = self._stub.GetSystemInfo(GetSystemInfoRequest())
        return MachineIdentity(
            uuid=info.identity.uuid,
            name=info.identity.name,
            model_type=info.identity.model_type,
            model_name=info.identity.model_name,
            system=self,
        )
```

## UI Integration

### Machines Menu (macOS)

```
Machines
────────────
Running Locally
  ▸ Teletext Server
  ▸ Level 3 Fileserver
────────────
Connected
  ▸ Print Server @ lab.local
```

Names, not model types. Technical details available on hover or in inspector.

### Quit Dialog

```
Running Locally
 ▸ Teletext Server        (Started by Beebium)
 ▸ Level 3 Fileserver    (Started from Terminal)
```

Name is the headline; provenance is supporting detail.

### Bonjour Discovery

Advertise:
- Service name: Machine name (e.g., "Teletext Server")
- Service type: `_beebium._tcp`
- TXT records:
  - `uuid=12345678-1234-1234-1234-123456789abc`
  - `model=ModelB`
  - `provenance=macos-gui`

### Rename UI

Good places for rename:
- Machine > Rename… menu item
- Inline rename in Machines panel (future)
- Machine Settings / Info sheet

Avoid:
- File menu (machines aren't files)
- App Settings (rename is per-machine, not global)

## Testing

```python
def test_machine_identity():
    with Beebium.launch(mos_filepath=MOS_PATH) as client:
        identity = client.system.identity

        # UUID is present and valid
        assert is_valid_uuid(identity.uuid)

        # Default name derived from model
        assert "Model B" in identity.name

        # Rename via property setter (triggers gRPC call)
        identity.name = "Test Server"
        assert identity.name == "Test Server"

        # UUID unchanged after rename
        assert identity.uuid == identity.uuid
```

### Verification Criteria

1. **UUID generated**: New machine has valid UUID
2. **UUID stable**: Same UUID across multiple `GetSystemInfo` calls
3. **Default name**: Reasonable default derived from model
4. **Rename works**: `SetMachineName` changes the name
5. **UUID immutable**: Rename doesn't change UUID
6. **Multi-client**: Two clients see same name after rename

## Future Considerations

- **Name persistence**: Save name with machine state for restore
- **Role/tag field**: Optional structured metadata (e.g., "fileserver", "printer")
- **Name templates**: Auto-generate names like "BBC Model B (2)" for duplicates
- **Name validation**: Character restrictions, length limits, profanity filter?
