# Provenance Reporting Design

## Overview

Provenance reporting allows emulator cores to know and report how they were launched. This information helps frontends make appropriate UI and lifecycle decisions (e.g., whether to offer "Power Off" for a machine, how to label machines in a list).

## Design Principles

1. **Extensibility**: New client types can be added without changing server code
2. **Uniformity**: Use standard types (UUID, strings) to avoid collision concerns
3. **Minimalism**: Only essential fields, no complex metadata structures

## Protocol Design

### Message Definition (`system.proto`)

```protobuf
message LaunchProvenance {
  // Freeform type - convention-based, not enumerated
  // Examples: "macos-gui", "python-client", "terminal", "vscode-extension", "ci-runner"
  string type = 1;

  // Per-instance UUID generated at launch time (RFC 4122)
  // Provides uniform typing and guarantees no collisions
  string instance_uuid = 2;

  // Optional version string, format defined by the launching client
  string version = 3;

  // Unix timestamp when the core was launched (seconds since epoch)
  int64 timestamp = 4;
}

message GetSystemInfoResponse {
  string machine_type = 1;
  string display_name = 2;
  LaunchProvenance provenance = 3;
}
```

### Why Freeform Strings (Not Enums)

An enumeration would require the server to know about all possible client types:

```protobuf
// BAD: Requires server changes for each new client
enum LauncherKind {
  LAUNCHER_UNKNOWN = 0;
  LAUNCHER_MACOS_GUI = 1;
  LAUNCHER_PYTHON_CLIENT = 2;
  // What about VS Code extension? CI runner? Third-party tools?
}
```

With freeform strings, any client can identify itself without server changes. The server stores and reports whatever it's given.

### Why Per-Instance UUID

The `instance_uuid` is a UUID generated fresh each time a client launches a core:

- **Uniform type**: All clients use the same format (UUID string)
- **No collisions**: UUIDs are globally unique by design
- **Session tracking**: Can correlate logs/events to a specific launch session
- **No registry needed**: Unlike sequential IDs, no central authority required

## CLI Interface

```
--provenance-type <type>   Freeform provenance type string
--provenance-uuid <uuid>   Per-instance UUID (RFC 4122 format)
--provenance-version <ver> Optional version string
```

Example:
```bash
beebium-model-b start \
  --provenance-type python-client \
  --provenance-uuid 550e8400-e29b-41d4-a716-446655440000 \
  --provenance-version 0.4.0
```

## Validation

The server MUST validate `instance_uuid` as a well-formed UUID (RFC 4122):

- Accept: `"550e8400-e29b-41d4-a716-446655440000"` (lowercase)
- Accept: `"550E8400-E29B-41D4-A716-446655440000"` (uppercase)
- Reject: malformed strings, empty strings, non-UUID formats
- On rejection: fail startup with clear error message

This ensures consistency and prevents accidental misuse of the field.

## Fallback Behavior (Protocol Specification)

When provenance flags are not provided, the server MUST apply these defaults. This is part of the protocol specification, not an implementation detail—clients can rely on consistent behavior.

| Field | Default Behavior |
|-------|-----------------|
| `type` | `"terminal"` if stdin is a terminal, otherwise `"unknown"` |
| `instance_uuid` | Server generates a new UUID v4 |
| `version` | Empty string `""` |
| `timestamp` | Unix timestamp at server startup (seconds since epoch) |

## Conventions (Not Enforced)

Recommended `type` values:

| Value | Description |
|-------|-------------|
| `"macos-gui"` | Beebium macOS application |
| `"python-client"` | Python beebium library |
| `"terminal"` | Launched from terminal (auto-detected) |
| `"unknown"` | No provenance provided (auto-detected) |

Third parties are free to use any string value.

## Implementation

### Files to Modify

- `src/service/proto/system.proto` - Add `LaunchProvenance` message
- `src/server/ServerMain.hpp` - Add CLI flag parsing
- `src/service/SystemService.hpp/.cpp` - Store and report provenance
- `clients/python/src/beebium/server.py` - Pass provenance flags when launching
- `clients/python/src/beebium/system.py` - Expose provenance in Python API

### C++ Server Side

```cpp
struct Provenance {
    std::string type;
    std::string instance_uuid;
    std::string version;
    std::chrono::system_clock::time_point timestamp;
};

// In ServerMain, parse and store:
Provenance provenance{
    args.get("--provenance-type", detect_default_type()),
    args.get("--provenance-uuid", generate_uuid()),
    args.get("--provenance-version", ""),
    std::chrono::system_clock::now()
};
```

### Python Client Side

```python
# In ServerProcess.__init__ or start():
import uuid

self._instance_uuid = str(uuid.uuid4())

# When launching:
args = [
    server_path, "start",
    "--provenance-type", "python-client",
    "--provenance-uuid", self._instance_uuid,
    "--provenance-version", beebium.__version__,
    # ... other args
]
```

### Querying Provenance

```python
client = Beebium.connect("localhost:48875")
prov = client.system.provenance

print(f"Launched by: {prov.type}")
print(f"Instance: {prov.instance_uuid}")
print(f"Version: {prov.version}")
print(f"At: {datetime.fromtimestamp(prov.timestamp)}")
```

## Testing

Testing uses the Python client's `Beebium.launch()` context manager, which manages the emulator process lifecycle (starts on entry, sends SIGTERM on exit).

```python
def test_provenance_reporting():
    with Beebium.launch(
        mos_filepath=MOS_PATH,
        basic_filepath=BASIC_PATH,
    ) as client:
        prov = client.system.provenance

        assert prov.type == "python-client"
        assert is_valid_uuid(prov.instance_uuid)
        assert prov.version == beebium.__version__
        assert prov.timestamp > 0
```

### Verification Criteria

1. **Python launch**: `client.system.provenance.type == "python-client"`
2. **UUID present**: `instance_uuid` is a valid UUID string
3. **Terminal fallback**: Launch from terminal with no flags → `type == "terminal"`
4. **Unknown fallback**: Launch non-interactively with no flags → `type == "unknown"`
5. **Invalid UUID rejected**: `--provenance-uuid "not-a-uuid"` fails startup

## Future Considerations

- **Provenance in Bonjour advertisements**: Include `type` in mDNS TXT records
- **Shutdown policy**: Use provenance to decide if a client can request shutdown
- **UI labeling**: Display "Started by Beebium" vs "Started externally" based on provenance
