# Econet/AUN Stack Integration

This document captures the work programme for integrating Econet/AUN features across the Beebium stack: presets, gRPC, service discovery, Python client, and macOS client.

The Econet/AUN networking core (MC68B54 ADLC emulation, EconetSocket, FourWayHandshake) supports three transport backends: `AunBackend` (UDP/IP), `PiconetBackend` (USB-attached Piconet device on a real Econet wire), and `TestBackend` (in-process test double). It has been validated end-to-end against a real BBC Microcomputer talking via real Econet to a Beebium-emulated Level 3 File Server, and against a real Acorn Level 3 Fileserver running in BeebEm via AUN. This document plans the remaining work to expose these capabilities through the standard Beebium interfaces.

## Phases

| Phase | Summary | Status | Depends on |
|-------|---------|--------|------------|
| 0 | Update `docs/networking.md` to match implementation | **Done** | — |
| 1 | Preset integration (JSON format, schema, apply) | **Done** (AUN + Piconet) | — |
| 2 | gRPC proto + C++ service | Partial — status surfaces both AUN and Piconet; full read/write API still pending | — |
| 3 | Service discovery metadata | Pending | Phase 2 |
| 4 | Python client | Pending | Phase 2 |
| 5 | macOS client | Pending | Phase 2 |

Phases 1 and 2 are independent and can be done in either order. Phases 3-5 all depend on the proto definition from Phase 2. Phase 0 is a documentation prerequisite that should be done first.

## Design Decisions

These decisions were made during planning and inform all phases:

- **Full read/write gRPC API**: The EconetService supports both status queries and runtime configuration (enable/disable Econet, add/remove peers), following the DiscService pattern which supports `InstallDiscController`, `InsertDisc`, etc.

- **Frame-level event streaming**: The gRPC service includes a `SubscribeEconetEvents` streaming RPC for frame send/receive events, handshake stage changes, and connection state changes. Follows the `SubscribeDiscEvents` pattern.

- **DiscService as the primary pattern**: All aspects of the Econet integration (proto design, C++ service template, Python wrapper, Swift client, sidebar UI) follow the corresponding Disc subsystem implementation.

- **IP addresses as strings in proto**: Peer addresses use dotted-quad strings (`"192.168.1.100"`) rather than packed uint32, for client ergonomics.

- **Observable backend decorator for frame events**: Frame observation uses a decorator in the backend chain (`FourWayHandshake -> ObservableBackend -> AunBackend`) rather than modifying the `NetworkBackend` interface. This follows the existing decorator pattern.

- **Peer resolution must be an abstraction, not just static config**: The current `--aun-map` mechanism requires users to know IP addresses and ports up front, which is at odds with modern networking (DHCP, dynamic IPs, mDNS). The `AunBackend` already has the right runtime mutation API (`add_peer`/`remove_peer`), but the architecture must ensure this is accessible to multiple peer sources — not just CLI args and gRPC calls. Considerations:

  - **`AunBackend` peer table as the single source of truth**: All peer sources (CLI, presets, gRPC, future discovery) converge on `add_peer`/`remove_peer`. This is already the case and should remain so.

  - **`EconetSocket` must expose the backend chain**: The gRPC service, discovery mechanisms, and any future peer source need a path to reach `AunBackend::add_peer()`. Phase 2 adds `EconetSocket::backend()` for this. Any discovery mechanism running in-process can use the same accessor.

  - **Peer provenance**: Currently all peers are equivalent. A future discovery mechanism (mDNS, AUN broadcast) may need to distinguish static peers (from config/presets — never expire) from discovered peers (expire after a timeout, refreshed by re-announcement). This could be modelled as a provenance tag on peer entries, or as a separate overlay that manages discovered peers and calls `add_peer`/`remove_peer` as they appear and disappear. The simpler overlay approach avoids complicating the core peer table.

  - **Candidate discovery mechanisms** (future work, not part of the current programme):
    - **Beebium mDNS**: Beebium already advertises itself via mDNS for gRPC service discovery. Econet station metadata (Phase 3) could be used by other Beebium instances to auto-discover peers. This is the most natural fit for Beebium-to-Beebium networking.
    - **AUN broadcast discovery**: The original AUN protocol included broadcast announcements. This would enable interop with other AUN implementations (BeebEm, RISC OS).
    - **User-specified discovery server**: A central rendezvous point for peers that aren't on the same subnet.
    - **DSCP (Dynamic Station Configuration Protocol)**: A DHCP analogue for Econet — a DSCP server assigns station numbers from a pool, so instances can launch with `--station auto` rather than manually coordinating IDs. The client would query the server early in the `enable()` path and proceed with the assigned station ID. Just an idea for now, but potentially worth exploring as the number of Beebium instances on a network grows.

  - **No changes needed now**: The current `add_peer`/`remove_peer` API on `AunBackend` is the right seam. Phase 2's `EconetSocket::backend()` accessor completes the access path. Future discovery work is additive — it calls into the existing API without requiring changes to `AunBackend`, `FourWayHandshake`, or `Mc6854`.

---

## Phase 0: Documentation Update — Done

Networking documentation in `docs/networking.md` was reviewed and updated as part of the piconet branch. Three transport backends (`AunBackend`, `PiconetBackend`, `TestBackend`) are now documented with selection guidance, mutual-exclusion rules, and pointers to design and limitation docs.

---

## Phase 1: Preset Integration — Done

The preset file format supports Econet as of the piconet branch. Two transports are supported, mutually exclusive within a single `econet` section.

### Preset JSON format — AUN

```json
{
  "econet": {
    "station": 5,
    "aun_port": 32768
  }
}
```

### Preset JSON format — Piconet

```json
{
  "econet": {
    "station": 32,
    "piconet": { "device_path": "/dev/tty.usbmodem101" }
  }
}
```

`aun_port` and `piconet` are mutually exclusive — `PresetLoader::parse_econet_section` rejects both being present in the same `econet` block. CLI arguments override preset values; combining a preset's `aun_port` with a CLI `--piconet` is a validation error at startup.

### Key files

- `src/server/include/beebium/server/PresetLoader.hpp` — `PresetEconetConfig`, `PresetPiconetConfig`, `parse_econet_section()`
- `src/server/include/beebium/server/ServerMain.hpp` — `apply_preset()` propagates both backends; `validate_config()` enforces mutual exclusion
- `tests/test_preset_loader.cpp` — Econet preset parsing tests including the AUN/Piconet co-presence error case
- `tests/test_cli.cpp` — CLI validation tests for `--piconet` / `--aun-port` mutual exclusion

---

## Phase 2: gRPC Service

**Status:** `GetEconetStatus` is implemented, including both AUN and Piconet backend-specific fields (the response carries an `aun_port` / `peer_count` block when the backend is `AunBackend`, and a `PiconetStatus` sub-message with `device_path` and `serial_open` when the backend is `PiconetBackend`). The full read/write surface (`EnableEconet`, `DisableEconet`, `AddPeer`, etc.) and `SubscribeEconetEvents` are still pending.

### Core library prerequisites

Before the gRPC service can be implemented, these accessors are needed:

- `EconetSocket::backend()` — returns `NetworkBackend*` for peer management
- `EconetSocket::aun_mode()` — query whether AUN mode is active
- `AunBackend::list_peers()` — enumerate the peer table (returns `vector<PeerInfo>`)
- `PiconetBackend::config()` — exposes the device path for status reporting (already in place)

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
