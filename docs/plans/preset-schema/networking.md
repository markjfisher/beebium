# Networking Schema

## Domain Concept

Econet was Acorn's local area network, allowing BBC Micros to share files, printers, and communicate. It predates Ethernet and uses a different protocol, but served similar purposes in schools and businesses.

AUN (Acorn Universal Networking) later bridged Econet to IP networks.

## Hardware Reality

### Econet
- Serial network using 68B54 ADLC chip
- Clock wire + data wires
- Station numbers 1-254 (0 reserved, 255 broadcast)
- Network numbers for internetworking
- Typical use: shared file server, printer server

### Availability

| Model | Econet |
|-------|--------|
| Model B | Optional (active terminator needed) |
| Model B+ | Optional |
| Master 128 | Built-in socket |
| Master Compact | None |

### Components
- **Station**: Individual BBC on the network
- **File Server**: Usually a Master or dedicated machine running NFS
- **Printer Server**: Shared printer access
- **Bridge**: Connects multiple Econet segments

## Schema Design

```json
{
  "type": "networking",
  "econet": {
    "available": true,
    "installation": "optional",
    "station_range": [1, 254],
    "default_enabled": false
  },
  "aun": {
    "available": true,
    "requires_econet": true,
    "description": "Bridge Econet to IP network"
  }
}
```

For models with built-in Econet:
```json
{
  "type": "networking",
  "econet": {
    "available": true,
    "installation": "builtin",
    "station_range": [1, 254],
    "default_enabled": false
  }
}
```

## Configuration Values

```json
{
  "econet_enabled": true,
  "econet_station": 42,
  "econet_network": 0,
  "aun_enabled": true,
  "aun_server": "fileserver.local"
}
```

Or simpler for basic use:
```json
{
  "econet_enabled": true,
  "econet_station": 42
}
```

## CLI Mapping

```
--econet-station 42
--econet-network 1
--aun-server fileserver.local
```

## UI Considerations

### Econet Panel
- Enable checkbox
- Station number field (1-254)
- Network number field (0-127, usually 0)
- Status indicator (connected/disconnected)

### AUN Panel (if available)
- Enable checkbox (only if Econet enabled)
- Server address field
- Connection status

### Validation
- Station number in valid range
- No duplicate stations on same network (if detectable)
- AUN requires Econet to be enabled

## Open Questions

1. **Multi-machine coordination**: If running multiple emulated BBCs, how to assign unique station numbers?

2. **Real Econet hardware**: Bridge to physical Econet via USB adapter?

3. **AUN server discovery**: mDNS/Bonjour for finding file servers?

4. **Network presets**: "Classroom setup" with predefined station numbers?

5. **File server configuration**: If running emulated file server, where does this go?

6. **Clock rate**: Econet clock speed affects timing. Configurable?

## Use Cases

### UC1: Standalone Machine (Default)

No networking — the most common configuration.

```json
{
  "econet_enabled": false
}
```

Econet hardware was expensive and mostly used in schools/businesses.

### UC2: Classroom Workstation

Student machine in a school network.

```json
{
  "econet_enabled": true,
  "econet_station": 42,
  "econet_network": 0
}
```

Station 42 on network 0. The file server (typically station 254) provides shared storage and login.

### UC3: Multiple Emulated Machines

Running several emulated BBCs that communicate.

```json
// Machine 1
{
  "econet_enabled": true,
  "econet_station": 1
}

// Machine 2
{
  "econet_enabled": true,
  "econet_station": 2
}

// Machine 3 (File Server)
{
  "econet_enabled": true,
  "econet_station": 254,
  "econet_role": "fileserver"
}
```

This simulates a classroom lab environment.

### UC4: AUN Bridge to Modern Network

Connecting emulated Econet to IP network for distributed emulation.

```json
{
  "econet_enabled": true,
  "econet_station": 10,
  "aun_enabled": true,
  "aun_server": "econet-bridge.local"
}
```

AUN (Acorn Universal Networking) bridges Econet to TCP/IP. This allows:
- Multiple users to share an emulated file server
- Connection to real Econet hardware via USB adapter
- Geographically distributed "classroom" simulations

### UC5: File Server Machine

Dedicated file server for other emulated stations.

```json
{
  "model": "master-128",
  "networking": {
    "econet": {
      "enabled": true,
      "station": 254
    }
  },
  "storage": {
    "one_mhz_bus": {
      "devices": [
        {
          "id": "acorn-scsi",
          "hard_drives": [
            { "unit": 0, "scsi_id": 0, "image_uri": "file:///path/to/NFS-Volume.hdf" }
          ]
        }
      ]
    }
  }
}
```

Station 254 is the conventional file server address. The hard disc image contains the network filing system volumes.

### UC6: Printer Server

Shared printer for the network.

```json
{
  "model": "model-b",
  "networking": {
    "econet": {
      "enabled": true,
      "station": 235
    }
  },
  "generic": {
    "printer_output": "file:///path/to/shared-printer.txt"
  }
}
```

Station 235 is the conventional printer server address.

### UC7: Network Boot (Diskless Workstation)

Machine that boots from network, no local storage.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "none" }
  },
  "networking": {
    "econet": {
      "enabled": true,
      "station": 50
    }
  },
  "startup_options": {
    "auto_boot": true
  }
}
```

With no local disc and auto-boot enabled, the machine will attempt network boot from the file server.

---

## Econet Station Number Conventions

| Range | Typical Use |
|-------|-------------|
| 1-127 | User workstations |
| 128-199 | Reserved |
| 200-234 | Bridge machines |
| 235 | Printer server |
| 236-253 | Reserved |
| 254 | File server |
| 255 | Broadcast (not assignable) |

---

## Network Number

Networks can be interconnected via bridges:

| Network | Description |
|---------|-------------|
| 0 | Local network (default) |
| 1-127 | Remote networks via bridge |

Within a single emulation session, network 0 is usually sufficient.

---

## Emulation Considerations

### Virtual vs Physical Econet

| Mode | Description | Use Case |
|------|-------------|----------|
| **Virtual** | Emulated wire between emulated machines | Testing, development |
| **AUN** | Bridge to IP network | Shared servers, remote users |
| **Physical** | USB adapter to real Econet | Connecting real hardware |

### Clock Source

Real Econet requires a clock signal. In emulation:
- Virtual Econet: Emulated clock, always synchronised
- AUN: No clock needed (TCP/IP handles timing)
- Physical: External clock from real network

**Open question**: Should clock rate be configurable for debugging?
