# Storage Schema

## Domain Concept

Storage encompasses all mass storage systems available to the BBC Micro: cassette tape, floppy discs, hard discs, and potentially network-based storage. Each has different characteristics, but they share common abstractions.

## Storage Abstractions

### Interface-Based Model

Storage configuration has two aspects:

1. **Built-in hardware** — fixed, not user-configurable (cassette, built-in FDC)
2. **Open interfaces** — where devices can be plugged in (FDC socket, 1 MHz bus)

```
┌─────────────────────────────────────────────────────────────┐
│                    Built-in Hardware                         │
│         (varies by model, not configurable)                  │
├─────────────────────────────────────────────────────────────┤
│ Cassette interface │ Model B, B+, Master 128                 │
│ WD1770 FDC         │ Model B+, Master 128, Master Compact    │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    Open Interfaces                           │
│           (user can plug in compatible devices)              │
├─────────────────────────────────────────────────────────────┤
│ FDC Socket  │ Model B only                                   │
│ 1 MHz Bus   │ Model B, B+, Master 128 (daisy-chainable)      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                          Devices                             │
├─────────────────────────────────────────────────────────────┤
│ FDC boards, SCSI/IDE controllers, compound units             │
│ (e.g., Opus Challenger 3-in-1: FDC + floppy + RAM disc)      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                          Media                               │
├─────────────────────────────────────────────────────────────┤
│ Disc images, tape files, hard disc images                    │
└─────────────────────────────────────────────────────────────┘
```

### Built-in (Fixed) Hardware

What's hardwired into each machine — not user-configurable.

| Model | Cassette | FDC |
|-------|----------|-----|
| Model B | Yes | — |
| Model B+ | Yes | WD1770 |
| Master 128 | Yes | WD1770 |
| Master Compact | — | WD1770 (3.5") |

### Open Interfaces

Where you can plug things in.

| Model | FDC Socket | 1 MHz Bus |
|-------|------------|-----------|
| Model B | Yes | Yes |
| Model B+ | — | Yes |
| Master 128 | — | Yes |
| Master Compact | — | — |

### Interface Characteristics

| Interface | Multiple devices | Examples |
|-----------|------------------|----------|
| FDC Socket | No (one controller) | Acorn 1770, Opus, Watford, Solidisk |
| 1 MHz Bus | Yes (daisy-chain) | Opus Challenger, SCSI controllers, Winchester drives |

**1 MHz bus note:** This bus is multi-purpose — it hosts storage devices (SCSI, Opus Challenger), sound devices (Music 5000), and other peripherals. Devices are categorized by capability (storage, sound, etc.) rather than by connection type. Since devices can be daisy-chained, different device types coexist without conflict.

**CLI conventions encode socket vs built-in:**
- If `--fdc` option exists → machine has FDC socket, user chooses what to install
- If `--fdc` option absent → FDC is either built-in (B+, Master) or not available
- The `list-fdcs` subcommand lists available FDC options for socketed machines

This means presets for Model B need `fdc_socket: "acorn-1770"` (or similar), while presets for Model B+ don't — the FDC is just there.

Note: ROM sockets can also host modern storage devices (GoSDC, GoMMC) but this is covered in the sideways bank schema, not here.

### What This Means for Schema Design

1. **Model defines what's built-in and which interfaces are open** — the schema reports this, presets work within it
2. **Built-in hardware just works** — no configuration needed (though media can be loaded)
3. **Open interfaces accept compatible devices** — presets specify what's plugged in
4. **Some devices bypass expected interfaces** — e.g., Opus Challenger uses 1 MHz bus, doesn't need FDC socket
5. **Devices can be compound** — Opus Challenger 3-in-1 includes FDC, floppy drive, and RAM disc in one unit
6. **Media is independent** — format, source location (write protection derived from host file permissions)

---

## Hardware Reality

### Built-in Storage

Hardwired into the machine; not user-configurable.

**Cassette** (all models except Master Compact):
- One "deck" (notional — the real tape deck is external)
- Media: UEF files, CSW files, raw audio
- Filing system: CFS (Cassette Filing System), built into MOS

**Built-in FDC** (Model B+, Master 128, Master Compact):
- WD1770 controller soldered to motherboard
- 2 drives supported
- Master Compact uses 3.5" drives; others use 5.25"

### FDC Socket Devices

The BBC Model B has a motherboard socket for an optional floppy disc controller. Various manufacturers produced compatible boards.

| ID              | Name | Chip | Drives | Formats | Notes |
|-----------------|------|------|--------|---------|-------|
| `none`          | None | — | 0 | — | Socket empty |
| `acorn-8271`    | Acorn 8271 FDC | Intel 8271 | 2 | FM | Early, limited |
| `acorn-1770`    | Acorn 1770 FDC | WD1770 | 2 | FM/MFM | Most common upgrade |
| `opus-8272`     | Opus FDC (8272) | Intel 8272A | 2 | FM/MFM | DDOS 3.00, 3.05 |
| `opus-2791`     | Opus FDC (2791) | WD2791 | 2 | FM/MFM | DDOS 3.1x, EDOS 0.4 |
| `opus-2793`     | Opus FDC (2793) | WD2793 | 2 | FM/MFM | DDOS 3.35, 3.36 |
| `opus-1770`     | Opus FDC (1770) | WD1770 | 2 | FM/MFM | DDOS 3.45, 3.46 |
| `watford-1770`  | Watford FDC | WD1770 | 4 | FM/MFM | 4-drive support |
| `solidisk-1770` | Solidisk 1770 FDC | WD1770 | 2 | FM/MFM | |
| `solidisk-dfdc` | Solidisk DFDC | 8271+WD1770 | 2 | FM/MFM | Switchable dual controller |

**Opus FDC variants:** Opus produced four different boards, each requiring specific ROM versions. The WD2791/WD2793 are Western Digital FM/MFM controllers, predecessors to the WD1770.

**Solidisk DFDC:** Notable for including *both* an Intel 8271 and a WD1770 on a single board, with a physical switch to select between them. Modelled as a single controller with a "mode" property:
```json
{ "id": "solidisk-dfdc", "mode": "1770" }
```

### 1 MHz Bus Devices

The 1 MHz bus is an external expansion interface. Devices can be daisy-chained. Storage devices on this bus don't require (and don't use) the FDC socket.

| ID | Name | Type | Notes |
|----|------|------|-------|
| `opus-challenger` | Opus Challenger 3-in-1 | Compound | FDC + floppy drive + 256KB/512KB RAM disc |
| `acorn-scsi` | Acorn SCSI Host Adapter | SCSI controller | For Winchester hard drives |
| `acorn-ide` | Acorn IDE Interface | IDE controller | For IDE hard drives |

**Opus Challenger 3-in-1:** A self-contained storage peripheral including:
- WD1770 floppy disc controller
- Built-in 5.25" floppy drive
- 256KB or 512KB RAM disc (battery-backed optional)

Because it connects via 1 MHz bus, a Model B with an Opus Challenger doesn't need anything in the FDC socket.

**Hard disc interfaces:** Both Acorn SCSI and IDE controllers connect via the 1 MHz bus. They require ADFS or another suitable filing system ROM.

### ROM Socket Devices

Modern retro peripherals that plug into sideways ROM sockets, providing storage without using traditional interfaces.

| ID | Name | Type | Notes |
|----|------|------|-------|
| `gosdc` | GoSDC | SD card | Modern SD card interface |
| `gommc` | GoMMC | MMC card | Modern MMC interface |

These are primarily of interest for real hardware users; emulation may or may not support them.

---

## Drive Numbering

How physical drives map to logical drive numbers depends on the filing system:

| Filing System | Drive Numbering | Notes |
|---------------|-----------------|-------|
| Acorn DFS | Per-surface | Drive 0/2 = surfaces of physical drive 0; Drive 1/3 = surfaces of physical drive 1 |
| Watford DDFS | Per-surface | Same as Acorn DFS |
| Acorn ADFS | Per-drive | Drive 0 = physical drive 0 (both surfaces as one volume); Drive 1 = physical drive 1 |

**DFS drive numbering (per-surface):**

```
Physical Drive 0          Physical Drive 1
┌─────────────────┐       ┌─────────────────┐
│ ▲ Surface 0     │       │ ▲ Surface 0     │
│ │ = Drive 0     │       │ │ = Drive 1     │
│ ▼ Surface 1     │       │ ▼ Surface 1     │
│   = Drive 2     │       │   = Drive 3     │
└─────────────────┘       └─────────────────┘
```

So `*DRIVE 0` and `*DRIVE 2` access opposite sides of the same physical disc, while `*DRIVE 1` and `*DRIVE 3` access opposite sides of a different disc.

**ADFS drive numbering (per-drive):**

ADFS aggregates both surfaces into a single logical volume, so a double-sided 80-track disc appears as one 640KB drive rather than two 200KB drives.

**Schema implication:** Presets configure *physical* drives (0 and 1), not logical drives. When a .dsd (double-sided) image is inserted into physical drive 0, both logical drives 0 and 2 become accessible under DFS. The filing system handles the mapping.

```json
{
  "storage": {
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/game.dsd" },
      { "drive": 1, "image_uri": "file:///path/to/save.ssd" }
    ]
  }
}
```

Here `drive: 0` and `drive: 1` refer to physical drives, not DFS logical drive numbers.

---

## Physical Drive Types

- 40-track (100KB per side) or 80-track (200KB per side)
- Single-sided or double-sided heads
- 5.25" (most common) or 3.5" (Master Compact)

---

## Media Formats

| Extension | Description | Typical size |
|-----------|-------------|--------------|
| `.ssd` | Single-sided DFS image | 200KB |
| `.dsd` | Double-sided DFS image | 400KB |
| `.adf` | ADFS floppy image | 640KB |
| `.adl` | ADFS large floppy | 800KB |
| `.hdf` | Hard disc image | varies |
| `.img` | Raw sector image | varies |
| `.fdi` | Formatted Disk Image | varies |

---

## Schema: What the Machine Reports

Each machine reports its storage capabilities in the configuration schema.

### 1. Built-in (Fixed) Hardware

Informational — tells the user what's always present.

```json
{
  "type": "storage",
  "builtin": {
    "cassette": true,
    "fdc": null
  }
}
```

Or for Model B+ / Master:

```json
{
  "builtin": {
    "cassette": true,
    "fdc": {
      "device": "WD1770",
      "label": "Built-in WD1770"
    }
  }
}
```

Or for Master Compact:

```json
{
  "builtin": {
    "cassette": false,
    "fdc": {
      "device": "WD1770",
      "label": "Built-in WD1770 (3.5\")"
    }
  }
}
```

### 2. Open Interfaces

Lists available choices for each open interface.

**FDC Socket** (Model B only — current implementation):

```json
{
  "fdc_socket": {
    "options": [
      { "id": "none", "label": "Empty" },
      { "id": "acorn-1770", "label": "Acorn 1770 FDC", "device": "WD1770" }
    ]
  }
}
```

For machines without FDC socket (B+, Master), this section is absent.

**Future FDC options** might include additional parameters:

```json
{
  "id": "solidisk-dfdc",
  "label": "Solidisk DFDC",
  "device": "8271+WD1770",
  "parameters": [
    {
      "id": "mode",
      "label": "Controller Mode",
      "type": "enum",
      "options": [
        { "id": "1770", "label": "WD1770 (double density)" },
        { "id": "8271", "label": "Intel 8271 (single density)" }
      ],
      "default": "1770"
    }
  ]
}
```

The preset would then include: `"fdc_socket": { "id": "solidisk-dfdc", "mode": "8271" }`.

**Cassette** (machines with cassette interface):

```json
{
  "cassette": {
    "image_types": ["uef", "csw"],
    "image_uri": "file:///path/to/tape.uef"
  }
}
```

**1 MHz Bus**: Not implemented yet, but the schema structure allows for it.

### 3. Floppy Drives

Each drive declares its number, capabilities, and current media:

```json
{
  "floppy_drives": [
    {
      "drive": 0,
      "tracks": [40, 80],
      "sides": 2,
      "image_types": ["ssd", "dsd", "adf", "adl"],
      "image_uri": "file:///path/to/game.ssd"
    },
    {
      "drive": 1,
      "tracks": [40, 80],
      "sides": 2,
      "image_types": ["ssd", "dsd", "adf", "adl"],
      "image_uri": null
    }
  ]
}
```

**Drive properties:**
- `drive`: Physical drive number (0 or 1, typically)
- `tracks`: Array of supported track counts (40 and/or 80)
- `sides`: Number of heads (1 or 2)
- `image_types`: Compatible image formats
- `image_uri`: Current media (or null)

**Media URI schemes:**
- `file:///absolute/path/to/image.ssd` — absolute filesystem path
- `null` — no media inserted
- Future: `library://games/Elite.ssd` — reference to image library

Explicit drive numbers allow sparse configurations:

```json
{
  "floppy_drives": [
    {
      "drive": 1,
      "tracks": [40],
      "sides": 1,
      "image_types": ["ssd"],
      "image_uri": null
    }
  ]
}
```

This represents a system with only drive 1 (no drive 0) — unusual but valid.

---

## Preset Configurations

Presets use a `storage` namespace for all storage-related configuration. The preset structure mirrors the schema structure for consistency.

### Model B with Acorn 1770 FDC

```json
{
  "name": "My Game",
  "model": "model-b",
  "storage": {
    "fdc_socket": {
      "id": "acorn-1770"
    },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///Users/bob/Discs/game.ssd" },
      { "drive": 1, "image_uri": null }
    ]
  }
}
```

### Model B with no FDC (cassette only)

```json
{
  "name": "Tape Game",
  "model": "model-b",
  "storage": {
    "fdc_socket": {
      "id": "none"
    },
    "cassette": {
      "image_uri": "file:///Users/bob/Tapes/game.uef"
    }
  }
}
```

### Model B+ (built-in FDC)

```json
{
  "name": "B+ Game",
  "model": "model-b-plus",
  "storage": {
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///Users/bob/Discs/game.ssd" },
      { "drive": 1, "image_uri": null }
    ]
  }
}
```

No `fdc_socket` key — the FDC is built-in.

### Model B with Solidisk DFDC (future)

```json
{
  "name": "Dual Controller",
  "model": "model-b",
  "storage": {
    "fdc_socket": {
      "id": "solidisk-dfdc",
      "mode": "1770"
    },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///Users/bob/Discs/game.dsd" },
      { "drive": 1, "image_uri": null }
    ]
  }
}
```

Or in 8271 mode:

```json
{
  "storage": {
    "fdc_socket": {
      "id": "solidisk-dfdc",
      "mode": "8271"
    },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///Users/bob/Discs/old-game.ssd" },
      { "drive": 1, "image_uri": null }
    ]
  }
}
```

### Master Compact (built-in FDC, no cassette)

```json
{
  "name": "Compact Game",
  "model": "master-compact",
  "storage": {
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///Users/bob/Discs/game.adf" },
      { "drive": 1, "image_uri": null }
    ]
  }
}
```

### Using a library reference (future)

```json
{
  "storage": {
    "fdc_socket": {
      "id": "acorn-1770"
    },
    "floppy_drives": [
      { "drive": 0, "image_uri": "library://games/Elite.ssd" },
      { "drive": 1, "image_uri": null }
    ]
  }
}
```

The `library://` scheme would reference a managed image library, making presets portable.

---

## Design Decisions

1. **Drives declare capabilities**: Each drive specifies tracks, sides, and compatible image types. This allows heterogeneous configurations.

2. **URIs for media**: Using `file://` for absolute paths leaves room for `library://` or other schemes later.

3. **No drive hardware config in presets**: Users don't configure drive hardware — that's declared by the schema. Presets just specify what media to load.

4. **Blank/write-protected images**: User's responsibility to provide appropriate images.

5. **1 MHz bus / cassette storage**: Deferred — schema structure allows for them when needed.

6. **ROM socket storage** (GoSDC, GoMMC): Covered by sideways bank schema, not here.

---

## CLI Usage

### Primary: Load preset file

```bash
beebium-model-b start --preset=game.preset.beebium
```

The preset file contains the complete `storage` section (and other sections).

### Override preset values

CLI options override specific values from the preset:

```bash
# Load preset but use different disc
beebium-model-b start --preset=game.preset.beebium --floppy 0:file:///tmp/patched.ssd

# Load preset but change FDC
beebium-model-b start --preset=game.preset.beebium --fdc none
```

### CLI-only (no preset)

For quick testing without a preset file:

```bash
# Model B with Acorn 1770
beebium-model-b start --fdc acorn-1770 --floppy 0:file:///Users/bob/Discs/game.ssd

# Built-in FDC (B+, Master) - no --fdc needed
beebium-model-b-plus start --floppy 0:file:///Users/bob/Discs/game.ssd
```

### CLI to preset key mapping

| CLI argument | Preset key |
|--------------|------------|
| `--fdc <id>` | `storage.fdc_socket.id` |
| `--fdc-mode <mode>` | `storage.fdc_socket.mode` |
| `--floppy 0:<path-or-uri>` | `storage.floppy_drives[drive=0].image_uri` |
| `--floppy 1:<path-or-uri>` | `storage.floppy_drives[drive=1].image_uri` |
| `--cassette <path-or-uri>` | `storage.cassette.image_uri` |

### Path vs URI handling

The CLI accepts both filesystem paths and URIs:

```bash
# Filesystem paths (converted to file:// URIs internally)
--floppy 0:/Users/bob/Discs/game.ssd
--floppy 0:C:\Games\elite.ssd

# Explicit URIs
--floppy 0:file:///Users/bob/Discs/game.ssd
--floppy 0:library://games/Elite.ssd
```

The CLI checks if the value contains a URI scheme (e.g., `file://`, `library://`). If not, it parses it as a platform filesystem path and converts to a `file://` URI internally. This provides convenience for quick CLI use while supporting explicit URIs for portability.

### Override semantics

CLI options **merge** with preset values rather than replacing them entirely:

```bash
# Preset has floppy_drives with drives 0 and 1
# This overrides only drive 0's image_uri, leaving drive 1 unchanged
beebium-model-b start --preset=game.preset.beebium --floppy 0:file:///tmp/patched.ssd
```

This allows targeted overrides without re-specifying the entire configuration.

---

## Media Management Questions

### 1. Image Paths: Absolute vs Library

**Option A: Absolute paths only**
```json
{ "drive": 0, "image_uri": "file:///Users/bob/Discs/Elite.ssd" }
```
- Simple
- Not portable between machines
- Breaks if files move

**Option B: Library references**
```json
{ "drive": 0, "image_uri": "library://games/Elite.ssd" }
```
- Portable if library is synced
- Requires library management infrastructure
- More complex

**Option C: Both**
- `file://` scheme for absolute paths
- `library://` scheme for library references
- Flexible and explicit

### 2. Write Protection

Write protection is determined by host filesystem permissions. If the disc image file is read-only, the emulated disc appears write-protected. No separate flag needed in presets.

**Copy-on-write**: A potential future feature where changes go to a temporary file, leaving the original untouched. This would be a launch-time option, not stored in presets.

### 3. Missing Media at Launch

What happens if a preset references an image that doesn't exist?

**Options:**
1. Prevent launch, require user to fix
2. Warn and launch with empty drive
3. Prompt for replacement file
4. Silent fallback to empty drive

**Proposal**: Warn and offer choices: browse for replacement, continue without, or cancel.

### 4. Auto-Boot Interaction

If a floppy drive has an image loaded and startup options have `auto_boot: true`, the disc's `!BOOT` file will run. Should the preset system:

1. Just let it happen (simplest)
2. Validate that the disc has a `!BOOT` file (helpful)
3. Show boot file contents in UI (informative)

---

## UI Considerations

### Storage Panel Layout

```
Storage
├── Cassette
│   └── [tape.uef                      ] [Browse...] [Eject]
│
├── Floppy Discs
│   ├── Controller: [Acorn 1770 FDC ▾]
│   ├── Drive 0: [Elite.ssd            ] [Browse...] [Eject]
│   └── Drive 1: [Empty                ] [Browse...]
│
└── Hard Disc (if available)
    ├── Interface: [None ▾]
    └── (configure units when interface selected)
```

### Interactions

- **Drag-drop**: Drop image files onto drives
- **Recent images**: Quick-pick from recently used
- **Format detection**: Identify SSD vs DSD vs ADF automatically
- **Capacity display**: Show image size and format

### Conditional UI

- Hard disc section hidden if model doesn't support it
- Drive count adjusts based on controller selection

---

## Use Cases

### UC1: Cassette-only Model B

The simplest configuration — no disc hardware.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "none" },
    "cassette": { "image_uri": "file:///path/to/game.uef" }
  }
}
```

A user loading games from tape, as many did in the early days.

### UC2: Standard Gaming Setup

Model B with disc controller, a game disc ready to boot.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/Elite.ssd" }
    ]
  },
  "startup_options": {
    "auto_boot": true
  }
}
```

### UC3: Development Environment

Model B with disc controller, blank save disc in drive 1.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/DevTools.ssd" },
      { "drive": 1, "image_uri": "file:///path/to/Work.ssd" }
    ]
  }
}
```

### UC4: Hard Disc Workstation

Master 128 with hard disc for serious work.

```json
{
  "model": "master-128",
  "storage": {
    "floppy_drives": [
      { "drive": 0, "image_uri": null }
    ],
    "one_mhz_bus": {
      "devices": [
        {
          "id": "acorn-scsi",
          "hard_drives": [
            { "unit": 0, "scsi_id": 0, "image_uri": "file:///path/to/System.hdf" }
          ]
        }
      ]
    }
  }
}
```

### UC5: Econet Workstation (No Local Storage)

Machine that boots from network, no local discs needed.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "none" }
  },
  "networking": {
    "econet": {
      "enabled": true,
      "station": 42
    }
  }
}
```

### UC6: Dual-Disc Game with Save (4-drive controller)

Game that spans two discs with a separate save disc.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "watford-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/GameDisc1.ssd" },
      { "drive": 1, "image_uri": "file:///path/to/GameDisc2.ssd" },
      { "drive": 2, "image_uri": "file:///path/to/SaveGame.ssd" }
    ]
  }
}
```

### UC7: Clean Boot (Troubleshooting)

Minimal configuration for testing, no storage at all.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "none" }
  }
}
```

### UC8: Solidisk DFDC Dual Controller

User with Solidisk DFDC wants to switch between 8271 and 1770 modes.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "solidisk-dfdc", "mode": "1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/DoubleD.dsd" }
    ]
  }
}
```

Or for running old single-density software:

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "solidisk-dfdc", "mode": "8271" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/OldGame.ssd" }
    ]
  }
}
```

The DFDC had a physical switch; in emulation we model this as a mode property.

### UC9: Opus DDOS with Specific ROM

User has Opus hardware and needs matching ROM version.

```json
{
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "opus-2791" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/work.ssd" }
    ]
  },
  "sideways_bank": {
    "slots": [
      { "slot": 14, "type": "rom", "image_uri": "file:///path/to/opus-ddos_3_15.rom" }
    ]
  }
}
```

Different Opus boards required different DDOS versions. The UI might remind users about ROM compatibility, but won't enforce strict validation.

---

## Open Questions

1. **Controller and filing system independence**: An FDC without a filing system ROM is valid (if not useful), as is having a filing system ROM without an FDC. Multiple filing systems (e.g., DFS + ADFS) can coexist — this is standard on the Master 128. The UI might remind users that a suitable filing system ROM is needed to use the FDC, but shouldn't enforce strict validation. Users will need their own knowledge or research to match FDCs and ROMs correctly. In practice, curated preset libraries will solve this by providing known-working configurations.

2. **Image format auto-detection**: If user drops a `.ssd` file, we know it's a floppy image. But some files are ambiguous. How much validation?

3. **Creating new images**: Should presets support "create blank disc" as an option, or must images pre-exist?

4. **Image libraries**: Is a managed library of images worth the complexity? Or just filesystem paths?

5. **Network storage**: Econet file server access is another form of storage. Include here or separate?

6. **Swapping media at runtime**: Presets define initial state. How do we handle "insert disc 2 when prompted"?

7. **Hard disc partitioning**: Some hard disc images have multiple partitions. Expose this?

8. **Switchable controllers**: The Solidisk DFDC has two controllers on one board. How should presets represent the "mode" (8271 vs 1770)?

9. **Compound devices**: The Opus Challenger 3-in-1 bundles FDC + floppy drive + RAM disc. How should presets configure the individual components of a compound device?

10. **1 MHz bus device ordering**: If multiple devices are daisy-chained, does order matter? How to represent in presets?

---

## References

### Third-Party Peripherals

| Device | Source |
|--------|--------|
| Opus Challenger 3-in-1 | [Computing History](https://www.computinghistory.org.uk/det/34883/Opus%20Challenger%203-in-1/) |
| Opus DDOS/EDOS | [Stardot forum: Opus FDC variants](https://stardot.org.uk/forums/viewtopic.php?p=242402) |
| EDOS details | [regregex.bbcmicro.net: EDOS notes](http://regregex.bbcmicro.net/edos-notes.txt) |
| Solidisk DFDC | [Wouter's BBC pages: Solidisk DDFS](http://wouter.bbcmicro.net/bbc/hardware/solidisk/ddfs.html) |
| Solidisk DFDC | [Chris's Acorns: Solidisk DFDC](https://chrisacorns.computinghistory.org.uk/8bit_Upgrades/Solidisk_dfdc.html) |

### Controller Chip Specifications

| Chip | Manufacturer | Notes |
|------|--------------|-------|
| Intel 8271 | Intel | Original single-density FDC |
| Intel 8272A | Intel | Later variant of 8271/8272 family |
| WD1770 | Western Digital | Most common BBC FDC |
| WD1772 | Western Digital | Faster step rates than 1770 |
| WD2791/WD2793 | Western Digital | Predecessors to WD1770 |
