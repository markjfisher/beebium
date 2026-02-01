# Preset System Design

## Overview

The Preset System provides a way to define and manage machine configurations. A preset is a named configuration that can be used to launch a new emulator core with specific settings (model type, ROM selection, disc images, peripherals, etc.).

Presets serve two audiences:

1. **Casual users**: Pick "BBC Model B" or "BBC Master 128" and go
2. **Power users**: Create custom presets with specific configurations (e.g., "Model B with Watford DFS", "Master 128 with MOS 3.50")

This phase builds on:
- Phase 7.5 (Settings Infrastructure) — provides the UI home for preset management

## Design Principles

1. **Discoverable defaults**: Default presets are auto-discovered from available core executables
2. **User presets are copies**: Users duplicate defaults and modify; defaults are immutable
3. **Configuration comes from cores**: Cores self-describe their configuration schema via gRPC
4. **Presets are portable**: Stored as JSON, can be shared between users
5. **Separation of concerns**: PresetManager is application-level; New Machine dialog just consumes it

## Preset Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│    Core Executable Discovery                                    │
│    ─────────────────────────                                    │
│    Find beebium-model-b, beebium-master-128, etc.               │
│                     │                                           │
│                     ▼                                           │
│    Query Preset Schema                                          │
│    ──────────────────                                           │
│    CLI describe-preset-schema → JSON schema with sections       │
│                     │                                           │
│                     ▼                                           │
│    Generate Default Presets                                     │
│    ────────────────────────                                     │
│    One preset per core executable with default values           │
│                     │                                           │
│                     ▼                                           │
│    Load User Presets                                            │
│    ─────────────────                                            │
│    From ~/Library/Application Support/Beebium/presets/          │
│                     │                                           │
│                     ▼                                           │
│    PresetManager Ready                                          │
│    ───────────────────                                          │
│    [Default Presets] + [User Presets] available                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Data Model

### MachinePreset

```swift
struct MachinePreset: Codable, Identifiable, Hashable {
    let id: UUID
    var name: String
    let coreExecutablePath: String  // Path to beebium-model-b, etc.
    var configuration: [String: ConfigValue]
    let isDefault: Bool  // True for auto-discovered presets
    var iconName: String?  // SF Symbol or custom icon

    // Derived from core's DescribeConfiguration
    var modelName: String  // "BBC Model B", "BBC Master 128"
    var modelDescription: String?  // Brief description for UI
}

enum ConfigValue: Codable, Hashable {
    case string(String)
    case int(Int)
    case bool(Bool)
    case path(String)  // File path, gets special UI treatment
    case choice(String, options: [String])  // Enum-like selection
}
```

### Configuration Schema

Cores describe their configurable options via the `describe-preset-schema` CLI subcommand. This enables the frontend to render appropriate UI without hardcoding knowledge of each model's options.

```bash
beebium-model-b describe-preset-schema
```

Output is JSON with a sectioned structure:

```json
{
  "schema_version": 1,
  "model": {
    "id": "model-b",
    "name": "BBC Model B",
    "description": "The original BBC Microcomputer with 32KB RAM"
  },
  "sections": [
    { "type": "storage", "builtin": {...}, "fdc_socket": {...}, "floppy_drives": [...] },
    { "type": "sideways_bank", ... },
    { "type": "coprocessor", ... }
  ]
}
```

See [preset-schema/overview.md](plans/preset-schema/overview.md) for the full schema specification.

### PresetManager

```swift
@MainActor
class PresetManager: ObservableObject {
    static let shared = PresetManager()

    @Published private(set) var defaultPresets: [MachinePreset] = []
    @Published private(set) var userPresets: [MachinePreset] = []

    var allPresets: [MachinePreset] {
        defaultPresets + userPresets
    }

    // Discovery
    func discoverCoreExecutables() async
    func refreshDefaultPresets() async

    // User preset management
    func duplicatePreset(_ preset: MachinePreset, newName: String) -> MachinePreset
    func updatePreset(_ preset: MachinePreset)
    func deletePreset(_ preset: MachinePreset)
    func resetToDefaults()  // Delete all user presets

    // Persistence
    private func loadUserPresets()
    private func saveUserPresets()

    // Core communication
    func fetchPresetSchema(for executablePath: String) async throws -> ConfigurationSchema
}
```

## Core Executable Discovery

### Search Locations

1. **App bundle**: `Beebium.app/Contents/MacOS/cores/`
2. **User support**: `~/Library/Application Support/Beebium/cores/`
3. **System-wide**: `/usr/local/bin/` (for development)
4. **Custom paths**: From Advanced settings (future)

### Discovery Process

```swift
func discoverCoreExecutables() async {
    var executables: [URL] = []

    // 1. App bundle cores
    if let bundleCores = Bundle.main.url(forResource: "cores",
                                          withExtension: nil) {
        executables += findExecutables(in: bundleCores)
    }

    // 2. User support directory
    let userCores = applicationSupportURL.appendingPathComponent("cores")
    executables += findExecutables(in: userCores)

    // 3. For each executable, query its configuration
    for executable in executables {
        if let schema = try? await fetchPresetSchema(for: executable.path) {
            let preset = MachinePreset(
                id: UUID(),
                name: schema.modelName,
                coreExecutablePath: executable.path,
                configuration: schema.defaults,
                isDefault: true,
                modelName: schema.modelName,
                modelDescription: schema.modelDescription
            )
            defaultPresets.append(preset)
        }
    }
}

private func findExecutables(in directory: URL) -> [URL] {
    // Find files matching pattern "beebium-*" that are executable
    let fm = FileManager.default
    guard let contents = try? fm.contentsOfDirectory(
        at: directory,
        includingPropertiesForKeys: [.isExecutableKey]
    ) else { return [] }

    return contents.filter { url in
        url.lastPathComponent.hasPrefix("beebium-") &&
        (try? url.resourceValues(forKeys: [.isExecutableKey]).isExecutable) == true
    }
}
```

### Naming Convention

Core executables follow the pattern `beebium-<model>`:
- `beebium-model-b`
- `beebium-model-b-plus`
- `beebium-master-128`
- `beebium-master-compact`

The human-readable model name comes from the core's `DescribeConfiguration` response, not the executable name.

## User Preset Storage

### File Location

```
~/Library/Application Support/Beebium/
└── presets/
    ├── My Custom Model B.json
    ├── Elite Setup.json
    └── Testing Config.json
```

### File Format

```json
{
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "name": "My Custom Model B",
    "coreExecutablePath": "/Applications/Beebium.app/Contents/MacOS/cores/beebium-model-b",
    "configuration": {
        "os_rom": "MOS120",
        "basic_rom": "BASIC2",
        "dfs_rom": "WatfordDFS",
        "disc0": "/Users/me/Discs/Elite.ssd"
    },
    "isDefault": false,
    "modelName": "BBC Model B",
    "modelDescription": "The original BBC Microcomputer with 32KB RAM"
}
```

### File Naming

Preset files are named after the preset name with `.json` extension. Invalid filename characters are replaced with underscores.

## Settings UI (Machines Pane)

### Layout

```
┌─────────────────────────────────────────────────────────────────┐
│  Machine Presets                                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Default Presets                                                │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ 🖥️  BBC Model B                                      [Dup] ││
│  │     The original BBC Microcomputer with 32KB RAM            ││
│  │                                                             ││
│  │ 🖥️  BBC Model B+                                     [Dup] ││
│  │     Enhanced Model B with 64KB RAM and built-in DFS         ││
│  │                                                             ││
│  │ 🖥️  BBC Master 128                                   [Dup] ││
│  │     The flagship BBC Micro with 128KB RAM                   ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                 │
│  Your Presets                                                   │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ 🖥️  Elite Setup                            [Edit] [Delete] ││
│  │     BBC Model B • Custom configuration                      ││
│  │                                                             ││
│  │ 🖥️  Testing Config                         [Edit] [Delete] ││
│  │     BBC Master 128 • Custom configuration                   ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                 │
│  No user presets yet. Duplicate a default preset to get started.│
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Interactions

| Action | Trigger | Result |
|--------|---------|--------|
| Duplicate | Click [Dup] button | Creates user preset with name "[Original] Copy", opens edit sheet |
| Edit | Click [Edit] button | Opens edit sheet for configuration |
| Delete | Click [Delete] button | Confirmation alert, then removes preset |
| Refresh | Automatic on pane open | Re-scans for core executables |

### Edit Sheet

```
┌─────────────────────────────────────────────────────────────────┐
│  Edit Preset                                                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Name: [Elite Setup                                          ]  │
│                                                                 │
│  Based on: BBC Model B                                          │
│                                                                 │
│  Configuration                                                  │
│  ─────────────────────────────────────────────────────────────  │
│                                                                 │
│  Operating System ROM     [▼ MOS 1.20                        ]  │
│                                                                 │
│  BASIC ROM                [▼ BASIC II                        ]  │
│                                                                 │
│  Disc Filing System       [▼ Acorn DFS 0.90                  ]  │
│                                                                 │
│  Drive 0 Disc Image       [None                         ] [📁] │
│                                                                 │
│  Drive 1 Disc Image       [None                         ] [📁] │
│                                                                 │
│  ☐ Enable Speech Chip                                           │
│                                                                 │
│                                    ┌────────┐ ┌──────────────┐  │
│                                    │ Cancel │ │     Save     │  │
│                                    └────────┘ └──────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

The configuration fields are generated dynamically from the core's `DescribeConfiguration` response:
- `CHOICE` → Dropdown picker
- `BOOLEAN` → Checkbox
- `FILE_PATH` → Text field + file picker button
- `STRING` → Text field
- `INTEGER` → Number field (with stepper if bounded)

## Integration with New Machine Dialog

The New Machine dialog (Phase 8) becomes a consumer of PresetManager:

```swift
struct NewMachineDialog: View {
    @EnvironmentObject var presetManager: PresetManager
    @State private var selectedPreset: MachinePreset?
    @State private var configuration: [String: ConfigValue] = [:]

    var body: some View {
        VStack {
            // Preset picker
            Picker("Preset", selection: $selectedPreset) {
                ForEach(presetManager.allPresets) { preset in
                    Text(preset.name).tag(Optional(preset))
                }
            }
            .onChange(of: selectedPreset) { preset in
                configuration = preset?.configuration ?? [:]
            }

            // Configuration editor (same as edit sheet)
            ConfigurationEditor(configuration: $configuration,
                               schema: selectedPreset?.schema)

            // Actions
            HStack {
                Button("Cancel") { dismiss() }
                Button("Create") { createMachine() }
                    .keyboardShortcut(.defaultAction)
            }
        }
    }
}
```

The key difference from Settings:
- Settings manages the *stored* presets
- New Machine dialog allows *temporary* modifications for a single launch

## Files to Create

| File | Purpose |
|------|---------|
| `clients/macos/Beebium/Beebium/PresetManager.swift` | Singleton managing preset discovery and storage |
| `clients/macos/Beebium/Beebium/MachinePreset.swift` | Preset data model |
| `clients/macos/Beebium/Beebium/Settings/MachinesSettingsPane.swift` | Full implementation (replaces placeholder) |
| `clients/macos/Beebium/Beebium/Settings/PresetEditSheet.swift` | Edit sheet for user presets |
| `clients/macos/Beebium/Beebium/ConfigurationEditor.swift` | Dynamic configuration form (shared with New Machine) |

## Files to Modify

| File | Changes |
|------|---------|
| `src/server/include/beebium/server/ServerMain.hpp` | Implement `describe-preset-schema` CLI subcommand |
| `clients/macos/Beebium/Beebium/Presets/ConfigurationSchema.swift` | Define schema types matching JSON output |
| `clients/macos/Beebium/Beebium/Presets/PresetManager.swift` | Invoke CLI and parse schema |
| `clients/macos/Beebium/Beebium/BeebiumApp.swift` | Add PresetManager to environment |

## CLI Interface

### describe-preset-schema Subcommand

Cores expose their configuration schema via CLI rather than gRPC. This allows schema introspection without starting a server.

```bash
beebium-model-b describe-preset-schema
```

Output is JSON (see [preset-schema/overview.md](plans/preset-schema/overview.md) for full specification):

```json
{
  "schema_version": 1,
  "model": {
    "id": "model-b",
    "name": "BBC Model B",
    "description": "The original BBC Microcomputer with 32KB RAM"
  },
  "sections": [
    {
      "type": "storage",
      "builtin": { "cassette": true, "fdc": null },
      "fdc_socket": { "options": [...] },
      "floppy_drives": [...]
    }
  ]
}
```

The macOS client invokes this subcommand for each discovered executable and parses the JSON to build its preset list.

## Testing

### Manual Verification

1. **Default preset discovery**
   - Launch app with core executables in bundle
   - Verify default presets appear in Settings > Presets
   - Verify preset names and descriptions match core responses

2. **User preset creation**
   - Duplicate a default preset
   - Verify copy appears in "Your Presets" section
   - Verify edit sheet opens with correct values

3. **User preset editing**
   - Modify configuration values
   - Save and re-open edit sheet
   - Verify changes persisted

4. **User preset deletion**
   - Delete a user preset
   - Verify it disappears from list
   - Verify file removed from disk

5. **Persistence across launches**
   - Create user presets
   - Quit and relaunch app
   - Verify user presets still present

6. **New Machine integration**
   - Open New Machine dialog
   - Verify all presets available in picker
   - Verify selecting preset populates configuration

### Unit Tests

```swift
final class PresetManagerTests: XCTestCase {

    func testDuplicatePreset() {
        let manager = PresetManager()
        let defaultPreset = MachinePreset(
            id: UUID(),
            name: "BBC Model B",
            coreExecutablePath: "/path/to/core",
            configuration: [:],
            isDefault: true,
            modelName: "BBC Model B",
            modelDescription: nil
        )
        manager.defaultPresets = [defaultPreset]

        let copy = manager.duplicatePreset(defaultPreset, newName: "My Copy")

        XCTAssertFalse(copy.isDefault)
        XCTAssertEqual(copy.name, "My Copy")
        XCTAssertEqual(copy.coreExecutablePath, defaultPreset.coreExecutablePath)
        XCTAssertNotEqual(copy.id, defaultPreset.id)
    }

    func testDeletePreset() {
        let manager = PresetManager()
        let userPreset = MachinePreset(
            id: UUID(),
            name: "Custom",
            coreExecutablePath: "/path/to/core",
            configuration: [:],
            isDefault: false,
            modelName: "BBC Model B",
            modelDescription: nil
        )
        manager.userPresets = [userPreset]

        manager.deletePreset(userPreset)

        XCTAssertTrue(manager.userPresets.isEmpty)
    }

    func testCannotDeleteDefaultPreset() {
        let manager = PresetManager()
        let defaultPreset = MachinePreset(
            id: UUID(),
            name: "BBC Model B",
            coreExecutablePath: "/path/to/core",
            configuration: [:],
            isDefault: true,
            modelName: "BBC Model B",
            modelDescription: nil
        )
        manager.defaultPresets = [defaultPreset]

        manager.deletePreset(defaultPreset)  // Should be no-op

        XCTAssertEqual(manager.defaultPresets.count, 1)
    }
}
```

## Edge Cases

### Missing Core Executables

If the app bundle doesn't contain any cores:
- Default presets list is empty
- Settings shows: "No machine cores found. Reinstall Beebium to restore default cores."
- New Machine dialog is disabled

### Core Version Mismatch

If a user preset references a configuration key that no longer exists in the core:
- Unknown keys are preserved but not shown in UI
- Warning shown: "Some configuration options are no longer available"
- Saving the preset removes the unknown keys

### Duplicate Preset Names

User preset names must be unique within user presets:
- If user tries to save with existing name, show error
- Suggest appending number: "My Preset" → "My Preset 2"

### Invalid File Paths in Presets

If a preset references a disc image that no longer exists:
- Configuration editor shows the path with warning icon
- Preset is still valid; the core will handle missing files

## Design Decisions

1. **Presets are JSON, not plist**: JSON is more portable and easier to share. Users can hand-edit if needed.

2. **Default presets are ephemeral**: They're regenerated from core discovery each launch. This ensures they always match the installed cores.

3. **User presets own their configuration**: Rather than storing only deltas from defaults, user presets store complete configuration. This makes them independent of default preset changes.

4. **Configuration schema comes from cores**: The frontend doesn't hardcode knowledge of what options each model supports. This allows new core versions to add options without frontend changes.

5. **File paths are stored absolute**: Relative paths would be ambiguous. We accept that presets with paths may not be portable between machines, but that's acceptable for disc images (users can browse to new location).

6. **PresetManager is a singleton**: There's one canonical set of presets for the app. Both Settings and New Machine dialog reference the same manager.

## Open Questions

1. **Preset icons**: Should presets have custom icons? Default presets could show the BBC Micro model; user presets could show a custom icon or the model icon with a badge.

2. **Preset export/import**: Should there be explicit Export/Import buttons, or is drag-and-drop of `.json` files sufficient?

3. **Preset validation**: How strict should validation be? Allow launching with missing ROM paths (core will error), or block in the UI?

4. **Recent configurations**: When user launches from New Machine with modifications (without saving as preset), should this be remembered as "last used configuration" per model?

5. **Core executable versioning**: If a core updates and its configuration schema changes, how do we migrate existing user presets? Probably best to leave unknown keys and let users update manually.

See the main [lifecycle-management.md](lifecycle-management.md) for the overall phase roadmap.
