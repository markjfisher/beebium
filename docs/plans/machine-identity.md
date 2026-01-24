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

  // Machine type (e.g., "ModelB", "Master128")
  // Immutable, set at creation
  string model_type = 3;

  // Full display name for UI (e.g., "BBC Model B 32K")
  // Immutable, set at creation
  string display_name = 4;
}

// Extended SystemInfo response
message SystemInfo {
  string machine_type = 1;        // Existing
  string machine_display_name = 2; // Existing
  LaunchProvenance provenance = 3; // Phase 1
  MachineIdentity identity = 4;    // NEW
}

// Rename RPC
message SetMachineNameRequest {
  string name = 1;
}

message SetMachineNameResponse {
  bool success = 1;
  string error = 2;  // e.g., "name too long", "invalid characters"
}

rpc SetMachineName(SetMachineNameRequest) returns (SetMachineNameResponse);
```

### Name Change Notification (Optional)

For multi-client scenarios, clients may want to be notified when the name changes:

```protobuf
message MachineIdentityEvent {
  MachineIdentity identity = 1;
}

// Could be part of WatchServerStatus or a dedicated stream
rpc WatchMachineIdentity(Empty) returns (stream MachineIdentityEvent);
```

This is optional for initial implementation—clients can poll `GetSystemInfo` if needed.

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

### Files to Modify

- `src/service/proto/system.proto` - Add `MachineIdentity` message, `SetMachineName` RPC
- `src/service/include/beebium/service/SystemService.hpp` - Store and report identity
- `src/server/include/beebium/server/ServerMain.hpp` - Add CLI flag parsing, UUID generation
- `clients/python/src/beebium/system.py` - Expose identity in Python API

### C++ Server Side

```cpp
struct MachineIdentity {
    std::string uuid;
    std::string name;
    std::string model_type;
    std::string display_name;
};

// In SystemService:
class SystemServiceImpl {
    MachineIdentity identity_;
    std::mutex identity_mutex_;  // For thread-safe renaming

public:
    grpc::Status SetMachineName(grpc::ServerContext* context,
                                const SetMachineNameRequest* request,
                                SetMachineNameResponse* response) {
        std::lock_guard<std::mutex> lock(identity_mutex_);
        identity_.name = request->name();
        response->set_success(true);
        return grpc::Status::OK;
    }
};
```

### Python Client Side

```python
@dataclass(frozen=True)
class MachineIdentity:
    uuid: str
    name: str
    model_type: str
    display_name: str

class System:
    @property
    def identity(self) -> MachineIdentity:
        """Get machine identity (UUID and name)."""
        info = self._stub.GetSystemInfo(GetSystemInfoRequest())
        return MachineIdentity(
            uuid=info.identity.uuid,
            name=info.identity.name,
            model_type=info.identity.model_type,
            display_name=info.identity.display_name,
        )

    def set_name(self, name: str) -> None:
        """Rename the machine."""
        response = self._stub.SetMachineName(SetMachineNameRequest(name=name))
        if not response.success:
            raise BeebiumError(response.error)
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

        # Rename works
        client.system.set_name("Test Server")
        assert client.system.identity.name == "Test Server"

        # UUID unchanged after rename
        assert client.system.identity.uuid == identity.uuid
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
