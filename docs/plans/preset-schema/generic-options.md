# Generic Options Schema

## Purpose

Generic options provide a fallback for configuration that doesn't fit the domain-specific section types. They use primitive types and are rendered with standard controls.

Use generic options sparingly — prefer domain-specific sections where semantics are clear.

## Primitive Types

### `boolean`

```json
{
  "key": "turbo_mode",
  "type": "boolean",
  "label": "Turbo Mode",
  "description": "Run at maximum speed without pacing",
  "default": false
}
```

**UI**: Checkbox

### `integer`

```json
{
  "key": "audio_buffer_ms",
  "type": "integer",
  "label": "Audio Buffer Size",
  "description": "Latency vs stability tradeoff",
  "min": 10,
  "max": 500,
  "step": 10,
  "default": 50,
  "unit": "ms"
}
```

**UI**: Number field with stepper, or slider

### `string`

```json
{
  "key": "machine_name",
  "type": "string",
  "label": "Machine Name",
  "description": "Friendly name for this machine",
  "default": "",
  "max_length": 64
}
```

**UI**: Text field

### `enum`

```json
{
  "key": "video_output",
  "type": "enum",
  "label": "Video Output",
  "description": "Signal type simulation",
  "choices": [
    {"id": "rgb", "label": "RGB (sharp)"},
    {"id": "composite", "label": "Composite (authentic)"},
    {"id": "rf", "label": "RF (fuzzy)"}
  ],
  "default": "rgb"
}
```

**UI**: Dropdown or radio group

### `file_path`

```json
{
  "key": "printer_output",
  "type": "file_path",
  "label": "Printer Output File",
  "description": "File to capture printer output",
  "mode": "save",
  "extensions": ["txt", "prn"],
  "default": null
}
```

**UI**: Text field with file picker button

## Schema Structure

Generic options are grouped in a section:

```json
{
  "type": "generic",
  "label": "Advanced Options",
  "options": [
    { "key": "turbo_mode", "type": "boolean", ... },
    { "key": "video_output", "type": "enum", ... }
  ]
}
```

Multiple generic sections can exist with different labels:

```json
{
  "type": "generic",
  "label": "Video",
  "options": [...]
},
{
  "type": "generic",
  "label": "Audio",
  "options": [...]
}
```

## Configuration Values

Flat key-value pairs:
```json
{
  "turbo_mode": true,
  "video_output": "composite",
  "audio_buffer_ms": 100
}
```

## CLI Mapping

Generic options map to CLI flags with the key as the flag name:

```
--turbo-mode
--video-output composite
--audio-buffer-ms 100
```

Keys use underscores in JSON, hyphens on CLI.

## UI Considerations

### Grouping
- Group related options under labelled sections
- Collapse "Advanced" sections by default

### Ordering
- Schema order determines UI order
- Put common options first

### Validation
- Integer range checking
- Required field validation
- Enum value validation

## When to Use Generic vs Domain-Specific

**Use generic when:**
- Option is simple (single value, no dependencies)
- Option doesn't relate to BBC hardware
- Option is emulator-specific (turbo mode, debug settings)

**Use domain-specific when:**
- Option relates to BBC hardware configuration
- Option has dependencies on other options
- Option benefits from specialized UI (sideways bank editor)
- Option has well-known semantics (disc system, Econet)

## Potential Generic Options

### Emulation
- Turbo mode (boolean)
- Cycle-accurate mode (boolean)
- CPU type: NMOS vs CMOS (enum, for Model B)

### Video
- Video output type: RGB/composite/RF (enum)
- Scanline effect (boolean)
- Phosphor persistence (boolean)
- Display scaling mode (enum)

### Audio
- Audio enabled (boolean)
- Volume (integer 0-100)
- Audio buffer size (integer)
- Low-pass filter (boolean)

### Debug
- Break on BRK instruction (boolean)
- Trace execution (boolean)
- Memory watch enabled (boolean)

## Use Cases

### UC1: Fast-Forward Loading

Skip the long tape/disc loading sequences.

```json
{
  "model": "model-b",
  "generic": {
    "turbo_mode": true
  },
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///path/to/game.ssd" }
    ]
  },
  "startup_options": {
    "auto_boot": true
  }
}
```

Turbo mode runs at maximum speed. Useful for loading, then disable for gameplay.

### UC2: Authentic Video Experience

Simulate CRT display characteristics.

```json
{
  "video_output": "composite",
  "scanlines_enabled": true,
  "phosphor_persistence": true
}
```

Composite video adds colour fringing and softness that some users prefer for authenticity.

### UC3: Low-Latency Audio

Minimise audio delay for rhythm games or music software.

```json
{
  "audio_buffer_ms": 20
}
```

Smaller buffer = less latency, but more risk of crackling on slower machines.

### UC4: Development/Debugging

Enable debug features for software development.

```json
{
  "break_on_brk": true,
  "trace_execution": false
}
```

`break_on_brk` pauses emulation when BRK instruction executes — useful for debugging crashes.

### UC5: Cycle-Accurate Testing

Maximum accuracy for timing-sensitive software.

```json
{
  "cycle_accurate": true,
  "turbo_mode": false
}
```

Cycle-accurate mode ensures precise timing at the cost of performance.

### UC6: Presentation/Recording

Clean output for screenshots or video capture.

```json
{
  "video_output": "rgb",
  "scanlines_enabled": false,
  "display_scaling": "integer"
}
```

RGB gives sharp pixels; integer scaling avoids interpolation artifacts.

### UC7: Background Emulation

Running headless or minimised.

```json
{
  "audio_enabled": false,
  "turbo_mode": true
}
```

For automated testing or batch processing of disc images.

---

## Preset vs Preferences

Some generic options feel like they should be user preferences rather than preset values:

| Option | Preset? | Preference? | Notes |
|--------|---------|-------------|-------|
| turbo_mode | ✓ | | Often per-preset (loading vs gameplay) |
| video_output | | ✓ | User's monitor preference |
| scanlines | | ✓ | Aesthetic choice |
| audio_buffer | | ✓ | Depends on user's hardware |
| break_on_brk | ✓ | | Development presets need this |
| cycle_accurate | ✓ | | Some software requires it |

**Proposal**: Generic options can appear in both presets and preferences. Preset values override preferences. UI indicates when a preset overrides the user's preference.

---

## Open Questions

1. **Preset vs launch options**: Some generic options feel like they belong in preferences, not presets. Where's the line?

2. **Per-session vs persistent**: Turbo mode might be per-session, but video output might be persistent. How to indicate?

3. **Platform-specific**: Some options might only make sense on certain platforms (macOS vs Windows). How to handle?

4. **Deprecation**: How to handle options that are removed in future versions?

5. **Option discovery**: Should cores advertise which generic options they support, or assume all frontends know the standard set?
