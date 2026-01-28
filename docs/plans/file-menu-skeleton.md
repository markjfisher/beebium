# File Menu Skeleton Design

## Overview

The File Menu Skeleton establishes the complete File menu structure for the macOS frontend before any of the dialogs or features are implemented. Having stub menu items in place early provides several benefits:

- **Architecture visualisation**: The menu structure makes the planned features tangible
- **Keyboard shortcut reservation**: Shortcuts are claimed early, avoiding conflicts
- **Framework for incremental development**: Each subsequent phase fills in a stub
- **User expectations**: Even disabled items signal what the app will eventually do

Without the skeleton, features would be added piecemeal, potentially leading to inconsistent menu organisation or shortcut conflicts.

## Design Principles

1. **macOS-canonical structure**: Follow Apple's Human Interface Guidelines for File menu organisation
2. **Complete from day one**: All planned items present, even if disabled
3. **Machine state as document**: The "document" in Beebium is the emulated machine's state
4. **Progressive enablement**: Items become functional as phases are completed
5. **Clear disabled state**: Users can see what's coming without confusion

## Menu Structure

```
File
 ├─ New…                       ⌘N
 │    (Create a new machine; opens configuration dialog)
 ├─ New Window                 ⌘⇧N
 │    (Open a new window for the currently selected machine)
 ├─ Open…                      ⌘O
 │    (Open saved machine state)
 ├─ Open Recent               ▶
 │    (List of recently opened machine state files)
 ├─ Connect…
 │    (Connect to an already-running machine; local or remote)
 ├─ ─────────────
 ├─ Save                       ⌘S
 │    (Save current machine state)
 ├─ Save As…                   ⇧⌘S
 │    (Save current machine state under a new name/path)
 ├─ Revert To                 ▶
 │    (Revert to last saved state)
 ├─ ─────────────
 └─ Close                      ⌘W
      (Close the current window; does not necessarily stop the machine)
```

## Design Rationale

| Item | Purpose | macOS Convention |
|------|---------|------------------|
| New… | Create a new machine via configuration dialog | Standard ⌘N for "new document" |
| New Window | Open additional window for current machine | Standard ⌘⇧N for multi-window apps |
| Open… | Load saved machine state from file | Standard ⌘O |
| Open Recent | Quick access to recent state files | Standard submenu |
| Connect… | Attach to already-running core (local or remote) | No shortcut (infrequent operation) |
| Save / Save As… | Persist machine state | Standard ⌘S / ⇧⌘S |
| Revert To | Restore previous saved state | Standard submenu (matches Pages, TextEdit) |
| Close | Close window only; machine may keep running | Standard ⌘W |

### Why No Shortcut for Connect?

While Finder uses ⌘K for "Connect to Server…", Beebium already uses ⌘K for rapid keyboard mapping switching (K for Keyboard). Connect… is also an infrequent operation — users typically create new machines rather than connecting to existing ones — so a shortcut isn't essential.

### Machine State as Document

The File menu treats **machine state** as the "document". This aligns with user mental models:

- "Saving" means preserving the current state of the emulated machine
- "Opening" means restoring a previously saved state
- "New" means creating a fresh machine instance

This is analogous to how virtual machine software (VMware, Parallels, VirtualBox) treats VM snapshots.

## Implementation

### FileCommands Structure

```swift
// clients/macos/Beebium/Beebium/FileCommands.swift

import SwiftUI

struct FileCommands: Commands {
    @ObservedObject var machineManager: MachineManager
    @Binding var showConnectDialog: Bool
    @Binding var showNewMachineDialog: Bool

    var body: some Commands {
        // Replace the standard "New" command group
        CommandGroup(replacing: .newItem) {
            newMenuSection
            Divider()
            openMenuSection
            Divider()
            connectMenuSection
        }

        // Replace the standard "Save" command group
        CommandGroup(replacing: .saveItem) {
            saveMenuSection
        }
    }

    // MARK: - Menu Sections

    @CommandsBuilder
    private var newMenuSection: some Commands {
        Button("New…") {
            showNewMachineDialog = true
        }
        .keyboardShortcut("n", modifiers: .command)
        .disabled(!isNewMachineEnabled)

        Button("New Window") {
            createNewWindowForCurrentMachine()
        }
        .keyboardShortcut("n", modifiers: [.command, .shift])
        .disabled(!isNewWindowEnabled)
    }

    @CommandsBuilder
    private var openMenuSection: some Commands {
        Button("Open…") {
            openMachineState()
        }
        .keyboardShortcut("o", modifiers: .command)
        .disabled(!isOpenEnabled)

        Menu("Open Recent") {
            if recentFiles.isEmpty {
                Text("No Recent Items")
                    .disabled(true)
            } else {
                ForEach(recentFiles, id: \.self) { file in
                    Button(file.lastPathComponent) {
                        openRecentFile(file)
                    }
                }
                Divider()
                Button("Clear Menu") {
                    clearRecentFiles()
                }
            }
        }
        .disabled(!isOpenRecentEnabled)
    }

    @CommandsBuilder
    private var connectMenuSection: some Commands {
        Button("Connect…") {
            showConnectDialog = true
        }
        .disabled(!isConnectEnabled)
    }

    @CommandsBuilder
    private var saveMenuSection: some Commands {
        Button("Save") {
            saveCurrentMachineState()
        }
        .keyboardShortcut("s", modifiers: .command)
        .disabled(!isSaveEnabled)

        Button("Save As…") {
            saveCurrentMachineStateAs()
        }
        .keyboardShortcut("s", modifiers: [.command, .shift])
        .disabled(!isSaveAsEnabled)

        Menu("Revert To") {
            if savedVersions.isEmpty {
                Text("No Saved Versions")
                    .disabled(true)
            } else {
                ForEach(savedVersions, id: \.self) { version in
                    Button(version.displayName) {
                        revertTo(version)
                    }
                }
            }
        }
        .disabled(!isRevertEnabled)
    }

    // MARK: - Enablement State

    // Phase 6: Only Close is functional
    // These will be updated as phases are implemented

    private var isNewMachineEnabled: Bool {
        false  // Enabled in Phase 8
    }

    private var isNewWindowEnabled: Bool {
        false  // Enabled when multi-window support added
    }

    private var isOpenEnabled: Bool {
        false  // Enabled when state persistence added
    }

    private var isOpenRecentEnabled: Bool {
        false  // Enabled when state persistence added
    }

    private var isConnectEnabled: Bool {
        false  // Enabled in Phase 7
    }

    private var isSaveEnabled: Bool {
        false  // Enabled when state persistence added
    }

    private var isSaveAsEnabled: Bool {
        false  // Enabled when state persistence added
    }

    private var isRevertEnabled: Bool {
        false  // Enabled when state persistence added
    }

    // MARK: - Stub Actions

    private var recentFiles: [URL] { [] }
    private var savedVersions: [SavedVersion] { [] }

    private func createNewWindowForCurrentMachine() {
        // TODO: Multi-window support
    }

    private func openMachineState() {
        // TODO: State persistence - Phase future
    }

    private func openRecentFile(_ url: URL) {
        // TODO: State persistence - Phase future
    }

    private func clearRecentFiles() {
        // TODO: State persistence - Phase future
    }

    private func saveCurrentMachineState() {
        // TODO: State persistence - Phase future
    }

    private func saveCurrentMachineStateAs() {
        // TODO: State persistence - Phase future
    }

    private func revertTo(_ version: SavedVersion) {
        // TODO: State persistence - Phase future
    }
}

// Placeholder for future state persistence
struct SavedVersion: Hashable {
    let displayName: String
    let date: Date
}
```

### App Integration

```swift
// clients/macos/Beebium/Beebium/BeebiumApp.swift

import SwiftUI

@main
struct BeebiumApp: App {
    @StateObject private var machineManager = MachineManager()
    @State private var showConnectDialog = false
    @State private var showNewMachineDialog = false

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(machineManager)
                .sheet(isPresented: $showConnectDialog) {
                    ConnectDialog(machineManager: machineManager)
                }
                .sheet(isPresented: $showNewMachineDialog) {
                    NewMachineDialog(machineManager: machineManager)
                }
        }
        .commands {
            FileCommands(
                machineManager: machineManager,
                showConnectDialog: $showConnectDialog,
                showNewMachineDialog: $showNewMachineDialog
            )
        }
    }
}
```

### Placeholder Dialogs

For the skeleton phase, dialogs can be simple placeholders:

```swift
// Placeholder until Phase 7
struct ConnectDialog: View {
    @ObservedObject var machineManager: MachineManager
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 20) {
            Text("Connect to Machine")
                .font(.headline)
            Text("This feature is not yet implemented.")
                .foregroundColor(.secondary)
            Button("OK") {
                dismiss()
            }
            .keyboardShortcut(.defaultAction)
        }
        .padding(40)
    }
}

// Placeholder until Phase 8
struct NewMachineDialog: View {
    @ObservedObject var machineManager: MachineManager
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 20) {
            Text("New Machine")
                .font(.headline)
            Text("This feature is not yet implemented.")
                .foregroundColor(.secondary)
            Button("OK") {
                dismiss()
            }
            .keyboardShortcut(.defaultAction)
        }
        .padding(40)
    }
}
```

## Enablement Roadmap

| Item | Initial State | Enabled In | Notes |
|------|---------------|------------|-------|
| New… | Shows placeholder | Phase 8 (New Machine Dialog) | Opens configuration dialog |
| New Window | Disabled | Future | Multi-window support |
| Open… | Disabled | Future | State persistence |
| Open Recent | Disabled (empty) | Future | State persistence |
| Connect… | Shows placeholder | Phase 7 (Connect Dialog) | Opens discovery/manual entry dialog |
| Save | Disabled | Future | State persistence |
| Save As… | Disabled | Future | State persistence |
| Revert To | Disabled (empty) | Future | State persistence |
| Close | Functional | Phase 6 | Standard window close |

## Files to Create

| File | Purpose |
|------|---------|
| `clients/macos/Beebium/Beebium/FileCommands.swift` | File menu implementation |
| `clients/macos/Beebium/Beebium/ConnectDialog.swift` | Placeholder (becomes real in Phase 7) |
| `clients/macos/Beebium/Beebium/NewMachineDialog.swift` | Placeholder (becomes real in Phase 8) |

## Files to Modify

| File | Changes |
|------|---------|
| `clients/macos/Beebium/Beebium/BeebiumApp.swift` | Integrate FileCommands, add dialog state |

## Testing

### Manual Verification

1. **Menu structure**
   - All items appear in correct order
   - Separators in correct positions
   - Submenus (Open Recent, Revert To) expand correctly

2. **Keyboard shortcuts**
   - ⌘N triggers New… (shows placeholder)
   - ⌘⇧N does nothing (New Window disabled)
   - ⌘O does nothing (Open… disabled)
   - ⌘S does nothing (Save disabled)
   - ⇧⌘S does nothing (Save As… disabled)
   - ⌘W closes current window
   - Connect… has no shortcut (accessible via menu only)

3. **Visual state**
   - Disabled items are greyed out
   - Enabled items with placeholders show dialog
   - Submenus show "No Recent Items" / "No Saved Versions"

4. **Close behaviour**
   - ⌘W closes the frontmost window
   - If last window closes, app behaviour follows macOS convention (stays running or quits based on LSUIElement)

### Automated Tests

```swift
// FileCommandsTests.swift

import XCTest
@testable import Beebium

final class FileCommandsTests: XCTestCase {

    func testInitialEnablementState() {
        let machineManager = MachineManager()
        let commands = FileCommands(
            machineManager: machineManager,
            showConnectDialog: .constant(false),
            showNewMachineDialog: .constant(false)
        )

        // In Phase 6, most items should be disabled
        XCTAssertFalse(commands.isNewWindowEnabled)
        XCTAssertFalse(commands.isOpenEnabled)
        XCTAssertFalse(commands.isSaveEnabled)
    }
}
```

## Edge Cases

### No Windows Open

When no windows are open:
- File menu still accessible from menu bar
- New… and Connect… should still work (they create windows)
- Save/Close items can be disabled (nothing to save/close)

### Multiple Windows

When multiple windows are open:
- Close (⌘W) closes the frontmost window only
- Save applies to the machine in the frontmost window
- This becomes more relevant when multi-window support is enabled

### Menu Bar App Mode

If Beebium runs as a menu bar app (LSUIElement):
- File menu may be in a status item menu instead
- Same structure applies, but rendering differs

## Design Decisions

1. **Placeholder dialogs vs alerts**: Dialogs are preferred over simple alerts because they establish the UI pattern early. When the real implementation comes, the transition is smoother.

2. **Disabled vs hidden**: Disabled items are shown (greyed out) rather than hidden. This signals planned functionality and maintains consistent menu structure.

3. **Connect… placement**: Connect… is placed after Open Recent but before the separator, grouping all "get a machine" actions together (New, New Window, Open, Open Recent, Connect).

4. **Revert To submenu**: Follows Apple's pattern (Pages, TextEdit) rather than a simple "Revert" item. This supports multiple saved versions in the future.

## Open Questions

1. **Open Recent scope**: Should Open Recent show recently opened state files, recently connected machines, or both? Leaning toward state files only, with recent connections handled in the Connect dialog.

2. **Save without state file**: If a machine has never been saved, should Save behave like Save As…? Standard macOS behaviour is yes.

3. **Unsaved changes indicator**: Should the window title show a dot (standard macOS unsaved indicator) when machine state has changed since last save? This requires tracking "dirty" state.

See the main [lifecycle-management.md](lifecycle-management.md) for the overall phase roadmap.
