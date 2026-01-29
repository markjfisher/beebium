# Settings Infrastructure Design

## Overview

The Settings Infrastructure phase establishes the Preferences window skeleton for the macOS frontend. This provides the foundational UI for application-level configuration before specific preference panes are implemented.

macOS applications conventionally use ⌘, to open Preferences (or Settings in recent macOS versions). Having this infrastructure in place early:

- Provides a home for application-level settings (distinct from per-machine settings)
- Establishes the UI pattern for preference panes before any are fully implemented
- Creates the architecture for `PresetManager` and other settings objects to live in
- Signals planned functionality to users even before panes are complete

This phase builds on:
- Phase 7 (Connect Dialog) — establishes dialog/window patterns in the app

## Design Principles

1. **macOS-canonical**: Follow Apple's Human Interface Guidelines for Settings windows
2. **Toolbar-based navigation**: Use toolbar icons to switch between panes (like Safari, Xcode)
3. **Skeleton first**: Establish the structure; panes are stubs until their phases complete
4. **Application-level only**: Machine-specific settings belong in per-machine inspectors, not here
5. **Panes as launching points**: Complex tools (e.g., keyboard layout editor) open in separate windows, not crammed into panes

## Window Layout

```
┌─────────────────────────────────────────────────────────────────┐
│  ◀ ▶                        Settings                            │
├────────┬────────┬────────┬──────────────────────────────────────┤
│ General│Machines│Keyboard│                                      │
│  ⚙️    │  🖥️   │  ⌨️    │                                      │
├────────┴────────┴────────┴──────────────────────────────────────┤
│                                                                 │
│                                                                 │
│                     [Selected Pane Content]                     │
│                                                                 │
│                                                                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Preference Panes

### Phase 7.5 (This Phase) — Skeleton Only

| Pane | Purpose | Status |
|------|---------|--------|
| General | App-wide behaviour (quit, launch, updates) | Placeholder |
| Machines | Machine preset management | Placeholder (Phase 7.6) |
| Keyboard | Keyboard mapping management | Placeholder |

### Planned Pane Contents

**General** (future):
- Quit behaviour: Ask / Always power off / Always keep running
- Show Welcome window on launch
- Check for updates (if applicable)

**Machines** (Phase 7.6):
- List of default and user presets
- Duplicate / Edit / Delete user presets
- Reset to defaults

**Keyboard** (future):
- Keyboard mapping list
- Import / Export mappings
- Set default mapping
- "Open Layout Editor..." button → launches separate Keyboard Layout Editor window

## Data Model

### Settings Window State

```swift
enum SettingsPane: String, CaseIterable, Identifiable {
    case general
    case machines
    case keyboard

    var id: String { rawValue }

    var label: String {
        switch self {
        case .general: return "General"
        case .machines: return "Machines"
        case .keyboard: return "Keyboard"
        }
    }

    var systemImage: String {
        switch self {
        case .general: return "gearshape"
        case .machines: return "desktopcomputer"
        case .keyboard: return "keyboard"
        }
    }
}
```

### App-Level Settings Storage

```swift
class AppSettings: ObservableObject {
    static let shared = AppSettings()

    // General
    @AppStorage("quitBehavior") var quitBehavior: QuitBehavior = .ask
    @AppStorage("showWelcomeOnLaunch") var showWelcomeOnLaunch: Bool = true

    enum QuitBehavior: String, CaseIterable {
        case ask = "ask"
        case alwaysPowerOff = "alwaysPowerOff"
        case alwaysKeepRunning = "alwaysKeepRunning"

        var label: String {
            switch self {
            case .ask: return "Ask what to do"
            case .alwaysPowerOff: return "Always power off machines"
            case .alwaysKeepRunning: return "Always keep machines running"
            }
        }
    }
}
```

## Implementation

### Settings Window

```swift
// clients/macos/Beebium/Beebium/SettingsView.swift

import SwiftUI

struct SettingsView: View {
    @State private var selectedPane: SettingsPane = .general

    var body: some View {
        TabView(selection: $selectedPane) {
            GeneralSettingsPane()
                .tabItem {
                    Label(SettingsPane.general.label,
                          systemImage: SettingsPane.general.systemImage)
                }
                .tag(SettingsPane.general)

            MachinesSettingsPane()
                .tabItem {
                    Label(SettingsPane.machines.label,
                          systemImage: SettingsPane.machines.systemImage)
                }
                .tag(SettingsPane.machines)

            KeyboardSettingsPane()
                .tabItem {
                    Label(SettingsPane.keyboard.label,
                          systemImage: SettingsPane.keyboard.systemImage)
                }
                .tag(SettingsPane.keyboard)
        }
        .frame(width: 500, height: 300)
    }
}
```

### Placeholder Panes

```swift
// clients/macos/Beebium/Beebium/Settings/GeneralSettingsPane.swift

struct GeneralSettingsPane: View {
    var body: some View {
        VStack {
            Image(systemName: "gearshape")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("General Settings")
                .font(.headline)
            Text("Coming soon.")
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// Similar placeholders for other panes...

struct MachinesSettingsPane: View {
    var body: some View {
        VStack {
            Image(systemName: "shippingbox")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("Machine Presets")
                .font(.headline)
            Text("Manage your machine presets here.")
                .foregroundColor(.secondary)
            Text("Coming in Phase 7.6.")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

struct KeyboardSettingsPane: View {
    var body: some View {
        VStack {
            Image(systemName: "keyboard")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("Keyboard Mappings")
                .font(.headline)
            Text("Coming soon.")
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
```

### App Integration

```swift
// In BeebiumApp.swift

@main
struct BeebiumApp: App {
    // ... existing state ...

    var body: some Scene {
        // ... existing WindowGroup ...

        // Settings window
        Settings {
            SettingsView()
        }
    }
}
```

SwiftUI's `Settings` scene automatically:
- Responds to ⌘, keyboard shortcut
- Adds "Settings..." to the application menu
- Creates a singleton window (opening again brings it to front)

## Launching Separate Windows from Settings

Some features are too complex to fit within a settings pane. For these, the pane provides a button that opens a separate window. This is a standard macOS pattern.

### When to Use Each Approach

| Approach | Use For | Examples |
|----------|---------|----------|
| Inline in pane | Simple forms, toggles, pickers | Quit behaviour, default mapping |
| Sheet (modal) | Focused configuration that doesn't need external context | Edit preset details, import dialog |
| Separate window | Tools that benefit from staying open alongside other work | Keyboard layout editor, ROM manager |

### Implementation Pattern

```swift
struct KeyboardSettingsPane: View {
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Form {
            // Mapping list, toggles, etc.

            Section("Tools") {
                Button("Open Layout Editor...") {
                    openWindow(id: "keyboard-layout-editor")
                }
            }
        }
    }
}

// In BeebiumApp.swift, declare the auxiliary window:
@main
struct BeebiumApp: App {
    var body: some Scene {
        // ... existing scenes ...

        Window("Keyboard Layout Editor", id: "keyboard-layout-editor") {
            KeyboardLayoutEditorView()
        }
        .defaultSize(width: 800, height: 600)
    }
}
```

### Button Text Conventions

- **"Edit..." or "Configure..."** → Opens a sheet (stays in settings context)
- **"Open [Tool Name]..."** → Opens a separate window (independent tool)
- **"Choose..." or "Browse..."** → Opens a system file picker

### Rationale

Separate windows are appropriate when:
1. The user might want the tool open while working in the main emulator window
2. The tool is a creative/design environment rather than just configuration
3. The tool has its own toolbar, complex layout, or needs to be resizable
4. Multiple instances might be useful (e.g., comparing two layouts)

This keeps settings panes focused on configuration while providing launching points for deeper tools.

## Files to Create

| File | Purpose |
|------|---------|
| `clients/macos/Beebium/Beebium/SettingsView.swift` | Main settings window with tab navigation |
| `clients/macos/Beebium/Beebium/Settings/GeneralSettingsPane.swift` | General pane (placeholder) |
| `clients/macos/Beebium/Beebium/Settings/MachinesSettingsPane.swift` | Machines pane (placeholder for Phase 7.6) |
| `clients/macos/Beebium/Beebium/Settings/KeyboardSettingsPane.swift` | Keyboard pane (placeholder) |
| `clients/macos/Beebium/Beebium/AppSettings.swift` | Singleton for app-level UserDefaults access |

## Files to Modify

| File | Changes |
|------|---------|
| `clients/macos/Beebium/Beebium/BeebiumApp.swift` | Add `Settings` scene |

## Testing

### Manual Verification

1. **Menu access**
   - Beebium menu shows "Settings..." item
   - ⌘, opens the Settings window

2. **Window behaviour**
   - Settings window is a singleton (⌘, when open brings to front)
   - Window has fixed size appropriate to content
   - Window title is "Settings" (or "Beebium Settings" depending on macOS version)

3. **Tab navigation**
   - All three tabs appear in toolbar
   - Clicking tab switches pane content
   - Selected tab is visually indicated

4. **Placeholder content**
   - Each pane shows placeholder text
   - Icons render correctly

## Design Decisions

1. **TabView over NavigationSplitView**: Toolbar tabs (like Safari, Xcode) rather than sidebar navigation (like System Settings). The sidebar style works better for apps with many panes; toolbar tabs are cleaner for a smaller number. Safari manages 13+ tabs in its toolbar, giving ample room for growth.

2. **Settings scene over custom window**: SwiftUI's `Settings` scene handles ⌘, binding, singleton behaviour, and menu item automatically. No need to reinvent this.

3. **Pane organization**: Grouped by concern:
   - General: App lifecycle and behaviour
   - Machines: Machine configurations (the main Phase 7.6 content)
   - Keyboard: Input mappings
   - Additional panes can be added as needed (e.g., Audio, Developer, Paths)

4. **No per-machine settings here**: Machine-specific settings (e.g., disc images, ROM selection) belong in per-machine inspectors or the sidebar, not in application Settings.

5. **AppSettings singleton**: Centralizes UserDefaults access and makes settings observable. Could use SwiftUI's `@AppStorage` directly in views, but a singleton provides a single source of truth and makes testing easier.

6. **Separate windows for complex tools**: Rather than cramming complex editors (keyboard layout designer, ROM manager, etc.) into settings panes, panes provide buttons to launch dedicated editor windows. This follows Apple's pattern (e.g., Accessibility Keyboard editor, Font Book) and keeps panes focused on configuration.

## Open Questions

1. **Pane sizing**: Should all panes share the same size, or should the window resize per pane? System Settings resizes; simpler apps often don't. Leaning toward fixed size for simplicity.

2. **Search**: Should Settings have a search field (like System Settings)? Probably overkill for 3 panes; defer until we have more content.

3. **Future panes**: As the app grows, additional panes may be needed (e.g., Audio, Display, Network). The toolbar approach scales to ~12 icons comfortably before considering alternatives.

See the main [lifecycle-management.md](lifecycle-management.md) for the overall phase roadmap.
