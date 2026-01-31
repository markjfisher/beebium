# Startup Options Schema

## Domain Concept

The BBC Micro reads DIP switches and keyboard state at boot to determine startup behaviour. These "keyboard links" control screen mode, disc boot behaviour, and other options.

## Hardware Reality

### Keyboard Links (active low, active when closed/pressed)

| Link | Active | Effect |
|------|--------|--------|
| 0 | — | Reserved |
| 1 | — | Reserved |
| 2 | — | Reserved |
| 3 | MODE bit 0 | |
| 4 | MODE bit 1 | |
| 5 | MODE bit 2 | Screen mode (bits 3-5) |
| 6 | — | Boot behaviour |
| 7 | NO BOOT | Disable auto-boot |

### Screen Mode Selection

Bits 3-5 of the startup options byte select the mode:

| Bits | Mode | Resolution | Colours |
|------|------|------------|---------|
| 000 | 7 | 40x25 teletext | 8 |
| 001 | 6 | 40x25 graphics | 2 |
| 010 | 5 | 20x32 graphics | 4 |
| 011 | 4 | 40x32 graphics | 2 |
| 100 | 3 | 80x25 graphics | 2 |
| 101 | 2 | 20x32 graphics | 16 |
| 110 | 1 | 40x32 graphics | 4 |
| 111 | 0 | 80x32 graphics | 2 |

### Boot Behaviour

- **Normal**: SHIFT-BREAK loads and runs `!BOOT` from disc
- **Reversed** (link 6): BREAK loads `!BOOT`, SHIFT-BREAK doesn't

## Schema Design

```json
{
  "type": "startup_options",
  "screen_mode": {
    "range": [0, 7],
    "default": 7,
    "labels": {
      "0": "Mode 0 (80x32, 2 colours)",
      "1": "Mode 1 (40x32, 4 colours)",
      "2": "Mode 2 (20x32, 16 colours)",
      "3": "Mode 3 (80x25, 2 colours)",
      "4": "Mode 4 (40x32, 2 colours)",
      "5": "Mode 5 (20x32, 4 colours)",
      "6": "Mode 6 (40x25, 2 colours)",
      "7": "Mode 7 (40x25, teletext)"
    }
  },
  "auto_boot": {
    "default": false,
    "description": "Reverse SHIFT-BREAK behaviour (boot without SHIFT)"
  },
  "no_boot": {
    "default": false,
    "description": "Disable disc auto-boot entirely"
  },
  "raw_links": {
    "advanced": true,
    "range": [0, 255],
    "description": "Direct control of startup options byte"
  }
}
```

## Configuration Values

Semantic options:
```json
{
  "screen_mode": 7,
  "auto_boot": true
}
```

Raw byte (advanced):
```json
{
  "raw_links": 0x78
}
```

Note: `raw_links` and semantic options are mutually exclusive.

## CLI Mapping

```
--screen-mode 7
--auto-boot
--no-boot
--links 0x78
```

## UI Considerations

### Basic View
- Screen mode dropdown (0-7 with descriptions)
- "Auto-boot from disc" checkbox
- Maybe hide rarely-used options

### Advanced View
- Raw links byte input (hex)
- Explanation of what each bit does
- Warning when raw conflicts with semantic options

### Presets
- Most presets just use defaults (Mode 7, no auto-boot)
- Game presets might set auto-boot + appropriate mode
- Development presets might use Mode 0 or 3 for 80-column

## Open Questions

1. **Mode vs screen mode**: The startup mode isn't necessarily the final mode — programs change it. Is this confusing in UI?

2. **Auto-boot + disc image**: If preset has auto-boot and disc image, should we validate disc has `!BOOT`?

3. **Master differences**: Master has different link behaviour. Unified schema or model-specific?

4. **Raw links**: Expose to casual users? Or hide in "Advanced" section?

## Use Cases

### UC1: Default (Mode 7, No Auto-Boot)

Standard startup — teletext mode, manual commands.

```json
{
  "screen_mode": 7,
  "auto_boot": false
}
```

User sees `BBC Computer 32K` prompt and types commands manually.

### UC2: Game Auto-Boot

Game preset that boots automatically when started.

```json
{
  "model": "model-b",
  "startup_options": {
    "screen_mode": 7,
    "auto_boot": true
  },
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/Elite.ssd" }
    ]
  }
}
```

Equivalent to SHIFT-BREAK on real hardware. The disc's `!BOOT` file runs automatically.

### UC3: 80-Column Development

Development setup with high-resolution text mode.

```json
{
  "model": "model-b",
  "startup_options": {
    "screen_mode": 0,
    "auto_boot": false
  }
}
```

Mode 0 gives 80x32 characters — better for programming and text editing.

### UC4: Graphics Development

Start in a graphics-friendly mode.

```json
{
  "model": "model-b",
  "startup_options": {
    "screen_mode": 1,
    "auto_boot": false
  }
}
```

Mode 1 (40x32, 4 colours) is a good balance of resolution and colours for graphics work.

### UC5: Teletext Development

Working on Mode 7 / teletext graphics.

```json
{
  "model": "model-b",
  "startup_options": {
    "screen_mode": 7,
    "auto_boot": false
  }
}
```

Mode 7's character-mapped display uses less memory and has unique capabilities.

### UC6: High-Colour Graphics

Start in maximum colour mode.

```json
{
  "model": "model-b",
  "startup_options": {
    "screen_mode": 2,
    "auto_boot": false
  }
}
```

Mode 2 (20x32, 16 colours) — low resolution but maximum colour palette.

### UC7: No-Boot Troubleshooting

Prevent auto-boot for debugging disc issues.

```json
{
  "model": "model-b",
  "startup_options": {
    "screen_mode": 7,
    "no_boot": true
  },
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/problematic.ssd" }
    ]
  }
}
```

The `no_boot` flag prevents SHIFT-BREAK from running `!BOOT`, allowing manual investigation.

### UC8: Network Boot

Start in mode for network boot screen.

```json
{
  "model": "model-b",
  "startup_options": {
    "screen_mode": 7,
    "auto_boot": true
  },
  "networking": {
    "econet": {
      "enabled": true,
      "station": 42
    }
  },
  "storage": {
    "fdc_socket": { "id": "none" }
  }
}
```

Without local disc, auto-boot triggers network boot from file server.

---

## Mode Selection Quick Reference

| Mode | Resolution | Colours | Memory | Best For |
|------|------------|---------|--------|----------|
| 0 | 80×32 | 2 | 20KB | Text, programming |
| 1 | 40×32 | 4 | 20KB | Games, graphics |
| 2 | 20×32 | 16 | 20KB | Colourful graphics |
| 3 | 80×25 | 2 | 16KB | Text (less memory) |
| 4 | 40×32 | 2 | 10KB | Fast games |
| 5 | 20×32 | 4 | 10KB | Fast colourful |
| 6 | 40×25 | 2 | 8KB | Minimal memory |
| 7 | 40×25 | 8 (teletext) | 1KB | Memory efficient |

---

## Auto-Boot vs No-Boot Interaction

| auto_boot | no_boot | BREAK Behaviour | SHIFT-BREAK Behaviour |
|-----------|---------|-----------------|----------------------|
| false | false | Normal start | Load & run !BOOT |
| true | false | Load & run !BOOT | Normal start |
| false | true | Normal start | Normal start |
| true | true | Normal start | Normal start |

Note: `no_boot: true` overrides `auto_boot: true`.

---

## Master Series Differences

The Master has additional startup options:

| Link | Effect |
|------|--------|
| CMOS RAM | Persistent settings survive power-off |
| *CONFIGURE | Software-configurable boot options |

**Open question**: Should Master presets use CMOS settings rather than keyboard links?
