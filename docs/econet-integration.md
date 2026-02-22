# Econet/AUN Stack Integration

This document captures the work programme for integrating Econet/AUN features across the Beebium stack: presets, gRPC, service discovery, Python client, and macOS client.

The Econet/AUN networking core (MC68B54 ADLC emulation, EconetSocket, AunBackend, FourWayHandshake) is working and has been tested against a real Acorn Level 3 Fileserver running in BeebEm via AUN. This document plans the remaining work to expose these capabilities through the standard Beebium interfaces.

## Phases

| Phase | Summary | Depends on |
|-------|---------|------------|
| 0 | Update `docs/networking.md` to match implementation | — |
| 1 | Preset integration (JSON format, schema, apply) | — |
| 2 | gRPC proto + C++ service | — |
| 3 | Service discovery metadata | Phase 2 |
| 4 | Python client | Phase 2 |
| 5 | macOS client | Phase 2 |

Phases 1 and 2 are independent and can be done in either order. Phases 3-5 all depend on the proto definition from Phase 2. Phase 0 is a documentation prerequisite that should be done first.

## Design Decisions

These decisions were made during planning and inform all phases:

- **Full read/write gRPC API**: The EconetService supports both status queries and runtime configuration (enable/disable Econet, add/remove peers), following the DiscService pattern which supports `InstallDiscController`, `InsertDisc`, etc.

- **Frame-level event streaming**: The gRPC service includes a `SubscribeEconetEvents` streaming RPC for frame send/receive events, handshake stage changes, and connection state changes. Follows the `SubscribeDiscEvents` pattern.

- **DiscService as the primary pattern**: All aspects of the Econet integration (proto design, C++ service template, Python wrapper, Swift client, sidebar UI) follow the corresponding Disc subsystem implementation.

- **IP addresses as strings in proto**: Peer addresses use dotted-quad strings (`"192.168.1.100"`) rather than packed uint32, for client ergonomics.

- **Observable backend decorator for frame events**: Frame observation uses a decorator in the backend chain (`FourWayHandshake -> ObservableBackend -> AunBackend`) rather than modifying the `NetworkBackend` interface. This follows the existing decorator pattern.

---

## Phase 0: Documentation Update

Review and update `docs/networking.md` to match the current implementation. The document was written during research and initial development; some sections are now out of date.

---

## Phase 1: Preset Integration

Currently Econet is configured exclusively via CLI arguments (`--station`, `--aun-port`, `--aun-map`). This phase adds Econet to the preset file format.

### Preset JSON format

```json
{
  "econet": {
    "station": 5,
    "aun": {
      "port": 32768,
      "peers": [
        { "net": 0, "stn": 254, "ip": "127.0.0.1", "port": 32768 }
      ]
    }
  }
}
```

### Key files

- `src/server/include/beebium/server/PresetLoader.hpp` — add `PresetEconetConfig` struct, `parse_econet_section()`
- `src/server/include/beebium/server/ServerMain.hpp` — extend `apply_preset()`, extend `describe-preset-schema`
- `tests/test_preset_loader.cpp` — Econet preset parsing tests

### Merge semantics

CLI arguments override preset values, matching the existing storage preset pattern. Peer lists from preset and CLI are merged (not replaced).

---

## Phase 2: gRPC Service

### Core library prerequisites

Before the gRPC service can be implemented, these accessors are needed:

- `EconetSocket::backend()` — returns `NetworkBackend*` for peer management
- `EconetSocket::aun_mode()` — query whether AUN mode is active
- `AunBackend::list_peers()` — enumerate the peer table (returns `vector<PeerInfo>`)

### Proto outline

```
service EconetService {
    GetEconetStatus     — hardware state, ADLC registers, handshake stage, connection
    EnableEconet        — fit Econet hardware (station ID, AUN port, AUN mode)
    DisableEconet       — remove Econet hardware
    AddPeer             — add Econet address <-> UDP endpoint mapping
    RemovePeer          — remove peer by Econet address
    ListPeers           — enumerate configured peers
    SubscribeEconetEvents — stream frame, handshake, and connection events
}
```

Key messages:
- `GetEconetStatusResponse` includes nested `AdlcStatus` (CR1-4, SR1-2, FIFO state) and `HandshakeStatus` (stage, flag fill)
- `EconetEvent` carries event type, timestamp, and optional frame info / peer info / handshake stage
- `EconetFrameInfo` carries frame type, addresses, port, control byte, data length, and (possibly truncated) payload

### Key files

- `src/service/proto/econet.proto` — new proto definition
- `src/service/include/beebium/service/EconetService.hpp` — template service implementation
- `src/service/include/beebium/service/Server.hpp` — registration
- `src/core/include/beebium/econet/EconetSocket.hpp` — add accessors
- `src/core/include/beebium/econet/AunBackend.hpp` — add `list_peers()`
- `tests/test_grpc_econet.cpp` — service tests

---

## Phase 3: Service Discovery Metadata

Add Econet TXT records to the mDNS advertisement so discovery clients can see which machines have Econet fitted and their station numbers.

### TXT records

When Econet is enabled: `econet_station=N`

### Key files

- `src/service/include/beebium/service/SystemService.hpp` — add TXT record in `SetAdvertisement`
- `clients/python/src/beebium/discovery.py` — parse `econet_station` from TXT records

---

## Phase 4: Python Client

### Wrapper class outline

```python
class Econet:
    status -> EconetStatus
    is_enabled -> bool
    station_id -> int
    peers -> list[PeerInfo]

    enable(station_id, aun_port, aun_mode)
    disable()
    add_peer(net, station, ip_address, port)
    remove_peer(net, station)

    events() -> Iterator[EconetEvent]
    start_background_events(callback) -> EventStreamHandle
```

### Key files

- `clients/python/src/beebium/econet.py` — new wrapper class
- `clients/python/src/beebium/connection.py` — add `econet_stub`
- `clients/python/src/beebium/client.py` — add `econet` property
- `clients/python/src/beebium/__init__.py` — export new classes
- `clients/python/tests/test_econet.py` — mock stub tests

---

## Phase 5: macOS Client

### UI integration

The macOS sidebar already has a `.network` mode (case 8 in `SidebarMode.swift`) with the "network" SF Symbol icon. This is the natural home for Econet controls.

### Key files

- `clients/macos/Beebium/Beebium/EconetClient.swift` — gRPC client wrapper (`@MainActor`, `ObservableObject`, `Disconnectable`)
- `clients/macos/Beebium/Beebium/NetworkModeView.swift` — sidebar content for `.network` mode
- `clients/macos/Beebium/Beebium/ContentView.swift` — create and register `EconetClient`
- `clients/macos/Beebium/Beebium/SidebarModeContent.swift` — route `.network` to `NetworkModeView`
- `clients/macos/Beebium/Beebium/Configuration/` — Econet section in configuration editor (preset UI)
- Generated Swift proto stubs from `econet.proto`
