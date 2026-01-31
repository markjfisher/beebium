# OS ROM Schema

## Domain Concept

The Operating System ROM (MOS) is the firmware that boots the BBC Micro. Different versions exist with varying capabilities and bug fixes. Most users want the standard version for their model, but developers and testers may need specific versions.

## Hardware Reality

- Model B: MOS 0.10, 1.00, 1.20 (most common)
- Model B+: MOS 2.00
- Master 128: MOS 3.20, 3.50
- Master Compact: MOS 5.00

The ROM is 16KB, mapped at $C000-$FFFF.

## Schema Design

```json
{
  "type": "os_rom",
  "known_versions": [
    {
      "id": "mos120",
      "filename": "acorn-mos_1_20.rom",
      "label": "MOS 1.20",
      "description": "Standard Model B firmware",
      "default": true
    },
    {
      "id": "mos100",
      "filename": "acorn-mos_1_00.rom",
      "label": "MOS 1.00",
      "description": "Early Model B firmware"
    }
  ],
  "custom_allowed": true,
  "file_extensions": ["rom", "bin"]
}
```

## Configuration Values

**Standard version:**
```json
{ "mos": "mos120" }
```

**Custom ROM file:**
```json
{ "mos": "/path/to/patched-mos.rom" }
```

## CLI Mapping

```
--mos mos120
--mos /path/to/custom.rom
```

## UI Considerations

- Dropdown with known versions
- "Custom..." option opens file picker
- Show description for selected version
- Warn if custom ROM is missing

## Open Questions

1. Should custom ROMs be validated (16KB size, checksums)?
2. How to handle ROM files that could be MOS or sideways ROMs?
3. Should known versions be hardcoded or discoverable from a ROM library?

## Use Cases

### UC1: Standard Gaming

Most games work with MOS 1.20. User doesn't care about OS version.

```json
{
  "mos": "mos120"
}
```

Frontend shows "MOS 1.20 (Standard)" as default, no action needed.

### UC2: Compatibility Testing

Developer testing software with different MOS versions.

```json
{
  "mos": "mos100"
}
```

Some software has bugs with specific MOS versions, or uses undocumented features that changed. Testing with MOS 1.00 catches these issues.

### UC3: Custom Patched ROM

User has a patched MOS with custom features (e.g., faster tape loading, bug fixes).

```json
{
  "mos": "/Users/bob/ROMs/patched-mos-1.20.rom"
}
```

Frontend shows "Custom ROM" with filename, validates 16KB size.

### UC4: Recreating Specific Hardware

User wants to emulate their original BBC exactly, including the specific MOS chip it shipped with.

```json
{
  "mos": "mos010"
}
```

MOS 0.10 was rare (early issue 1/2 boards) but some users have nostalgic attachment.

### UC5: Model B+ Specific

Model B+ requires MOS 2.00 — this is not optional.

```json
{
  "model": "model-b-plus",
  "mos": "mos200"
}
```

Schema indicates this is the only valid choice for this model.

---

## Model-Specific Constraints

| Model | Valid MOS Versions | Default |
|-------|-------------------|---------|
| Model B | 0.10, 1.00, 1.20 | 1.20 |
| Model B+ | 2.00 | 2.00 |
| Master 128 | 3.20, 3.50 | 3.20 |
| Master Compact | 5.00 | 5.00 |

For models with only one valid MOS, the UI should show it as informational rather than a dropdown.
