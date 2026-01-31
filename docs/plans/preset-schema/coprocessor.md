# Coprocessor Schema

## Domain Concept

The BBC Micro's Tube interface allows a second processor to be connected, taking over program execution while the BBC handles I/O. This was Acorn's approach to expandability — the same BBC could run different processors.

## Hardware Reality

### Tube Interface
- High-speed parallel interface between host and parasite
- Host handles keyboard, screen, disc, sound
- Parasite runs user programs with its own RAM
- Some models have internal Tube (Master), others external

### Second Processors

| Processor | CPU | RAM | Use Case |
|-----------|-----|-----|----------|
| 6502 Second Processor | 65C02 | 64KB | Faster BASIC, larger programs |
| Z80 Second Processor | Z80A | 64KB | CP/M, business software |
| 32016 Second Processor | NS32016 | 1MB/4MB | Panos, scientific computing |
| ARM Evaluation System | ARM1/ARM2 | 4MB | Development, RISC OS precursor |
| 80186 | 80186 | 512KB | DOS compatibility |
| Master 512 | 80186 | 512KB | Internal, Master only |

### Availability by Model

| Model | Tube | Internal Options |
|-------|------|------------------|
| Model B | External only | — |
| Model B+ | External only | — |
| Master 128 | External + internal | Master 512 |
| Master Compact | None | — |

## Schema Design

```json
{
  "type": "coprocessor",
  "tube_interface": {
    "available": true,
    "internal_slot": false
  },
  "options": [
    {
      "id": "none",
      "label": "No second processor",
      "default": true
    },
    {
      "id": "6502",
      "label": "6502 Second Processor",
      "cpu": "65C02",
      "ram_kb": 64,
      "rom_required": "tube-6502.rom",
      "description": "Turbo mode for BASIC programs"
    },
    {
      "id": "z80",
      "label": "Z80 Second Processor",
      "cpu": "Z80A",
      "ram_kb": 64,
      "rom_required": "tube-z80.rom",
      "description": "Run CP/M software"
    },
    {
      "id": "32016",
      "label": "32016 Second Processor",
      "cpu": "NS32016",
      "ram_options": [1024, 4096],
      "default_ram_kb": 1024,
      "rom_required": "tube-32016.rom",
      "description": "Panos operating system"
    },
    {
      "id": "arm",
      "label": "ARM Evaluation System",
      "cpu": "ARM1",
      "ram_kb": 4096,
      "rom_required": "tube-arm.rom",
      "description": "ARM development system"
    }
  ]
}
```

For Master with internal option:

```json
{
  "type": "coprocessor",
  "tube_interface": {
    "available": true,
    "internal_slot": true
  },
  "options": [
    { "id": "none", ... },
    { "id": "master512", "label": "Master 512", "internal": true, ... },
    { "id": "6502", "internal": false, ... }
  ]
}
```

## Configuration Values

Simple case:
```json
{
  "coprocessor": "6502"
}
```

With options:
```json
{
  "coprocessor": "32016",
  "coprocessor_ram_kb": 4096
}
```

## CLI Mapping

```
--tube 6502
--tube 32016 --tube-ram 4096
--tube none
```

## UI Considerations

### Basic UI
- Dropdown/radio for processor selection
- "None" as default

### Expanded UI (when processor selected)
- RAM amount picker (for 32016)
- ROM status indicator
- Brief description of capabilities

### Dependencies
- Selecting a coprocessor may require specific host-side ROM
- Some software only works with specific processors

## Open Questions

1. **ROM management**: Tube client ROMs are needed. Part of sideways bank, or separate?

2. **Parasite disc images**: Z80 needs CP/M discs. Separate from BBC disc images?

3. **Memory configuration**: 32016 has RAM options. How detailed should this get?

4. **Emulation complexity**: Some processors are hard to emulate accurately. Indicate this?

5. **Multiple Tubes**: Some setups had multiple coprocessors (rare). Support?

## Use Cases

### UC1: No Second Processor (Default)

Standard BBC Micro without Tube upgrade.

```json
{
  "coprocessor": "none"
}
```

This is the default for all presets.

### UC2: 6502 Second Processor for Gaming

Turbo mode for BASIC games — faster execution, more memory.

```json
{
  "coprocessor": "6502"
}
```

The 6502 Second Processor gives 64KB of contiguous RAM and runs at 3MHz. Games like Elite have enhanced 6502 versions.

### UC3: Z80 for CP/M Business Software

Running CP/M applications (WordStar, dBASE, etc.).

```json
{
  "model": "model-b",
  "coprocessor": {
    "type": "z80"
  },
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/CPM-System.ssd" }
    ]
  }
}
```

The Z80 Second Processor runs CP/M 2.2, giving access to the extensive CP/M software library. Requires CP/M disc image to boot.

### UC4: 32016 for Scientific Computing

Panos environment for serious number-crunching.

```json
{
  "model": "master-128",
  "coprocessor": {
    "type": "32016",
    "ram_kb": 4096
  },
  "storage": {
    "one_mhz_bus": {
      "devices": [
        {
          "id": "acorn-scsi",
          "hard_drives": [
            { "unit": 0, "scsi_id": 0, "image_uri": "file:///path/to/Panos.hdf" }
          ]
        }
      ]
    }
  }
}
```

The 32016 was expensive (£499 in 1985) but offered 32-bit computing. Panos supported Fortran-77 and C compilers. 4MB RAM option was available.

### UC5: ARM Evaluation System

Early ARM development — before Archimedes.

```json
{
  "model": "master-128",
  "coprocessor": {
    "type": "arm",
    "ram_kb": 4096
  }
}
```

The ARM Evaluation System (1986) used ARM1/ARM2 as a second processor. This is where RISC OS was developed before the Archimedes launch.

### UC6: Master 512 (Internal)

Master 128 with built-in 80186 for DOS compatibility.

```json
{
  "model": "master-128",
  "coprocessor": {
    "type": "master512"
  }
}
```

The Master 512 was an internal upgrade for the Master 128, running GEM desktop and DOS applications. Required internal installation — different from external Tube devices.

### UC7: BASIC Program Needs More Memory

Large BASIC program that won't fit in 32KB.

```json
{
  "model": "model-b",
  "coprocessor": {
    "type": "6502"
  },
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/MyProgram.ssd" }
    ]
  },
  "startup_options": {
    "auto_boot": true
  }
}
```

With 64KB RAM (minus BASIC and filing system), much larger programs are possible.

---

## Software Compatibility

| Coprocessor | Software Considerations |
|-------------|------------------------|
| None | Maximum compatibility; all BBC software works |
| 6502 | Most software works; some timing-sensitive code fails |
| Z80 | CP/M only; no BBC software |
| 32016 | Panos only; no BBC software |
| ARM | ARM development tools; no BBC software |
| Master 512 | GEM/DOS; no BBC software |

**Note**: Second processors run their own software. The host BBC handles I/O but doesn't run the user's programs.

---

## Tube Host-Side Requirements

| Coprocessor | Host ROM Required |
|-------------|-------------------|
| 6502 | Tube client in sideways bank |
| Z80 | Z80 boot ROM + CP/M filing system |
| 32016 | Panos ROM |
| ARM | ARM ROM |
| Master 512 | Master 512 ROM (built into Master) |

The schema should cross-reference these requirements with sideways_bank configuration.
