// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

import AppKit
import SwiftUI

/// Header showing mode icon and title at top of sidebar content
private struct SidebarHeader: View {
    let mode: SidebarMode

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: mode.icon)
                .font(.system(size: 18))
                .foregroundColor(.secondary)
            Text(mode.label)
                .font(.headline)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }
}

/// Container view that displays content for the selected sidebar mode
struct SidebarModeContent: View {
    let mode: SidebarMode
    @ObservedObject var discClient: DiscClient
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager

    var body: some View {
        VStack(spacing: 0) {
            SidebarHeader(mode: mode)

            switch mode {
            case .storage:
                StorageModeView(discClient: discClient)
            case .memory:
                MemoryModeView()
            case .peripherals:
                PeripheralsModeView()
            case .video:
                VideoModeView()
            case .sound:
                SoundModeView()
            case .keyboard:
                KeyboardModeView(mappingManager: keyboardMappingManager)
            case .coprocessor:
                CoprocessorModeView()
            case .network:
                NetworkModeView()
            }
        }
    }
}

// MARK: - Placeholder Mode Views

/// Placeholder view for Memory mode (sideways ROM slots, memory config)
struct MemoryModeView: View {
    var body: some View {
        ModePlaceholder(mode: .memory)
    }
}

/// Placeholder view for Peripherals mode (1MHz bus, user port, printer, serial, analogue)
struct PeripheralsModeView: View {
    var body: some View {
        ModePlaceholder(mode: .peripherals)
    }
}

/// Placeholder view for Video mode
struct VideoModeView: View {
    var body: some View {
        ModePlaceholder(mode: .video)
    }
}

/// Placeholder view for Sound mode
struct SoundModeView: View {
    var body: some View {
        ModePlaceholder(mode: .sound)
    }
}

/// Keyboard mode view showing mapping selection
struct KeyboardModeView: View {
    @ObservedObject var mappingManager: KeyboardMappingManager

    private var builtInMappings: [KeyboardMapping] {
        mappingManager.mappings.filter { $0.isBuiltIn }
    }

    private var userMappings: [KeyboardMapping] {
        mappingManager.mappings.filter { !$0.isBuiltIn }
    }

    /// Binding for Caps Lock sync toggle that sets the session override
    private var capsLockSyncBinding: Binding<Bool> {
        Binding(
            get: { mappingManager.isCapsLockSyncEnabled },
            set: { mappingManager.capsLockSyncOverride = $0 }
        )
    }

    /// Binding for a specific key's disabled state
    private func disabledKeyBinding(for keyName: String) -> Binding<Bool> {
        Binding(
            get: {
                mappingManager.isKeyDisabled(keyName)
            },
            set: { newValue in
                // Initialize override dictionary if needed
                if mappingManager.disabledKeysOverride == nil {
                    mappingManager.disabledKeysOverride = [:]
                }
                mappingManager.disabledKeysOverride?[keyName] = newValue
            }
        )
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            List {
                Section {
                    ForEach(builtInMappings) { mapping in
                        MappingRowView(
                            mapping: mapping,
                            isActive: mapping.id == mappingManager.activeMapping?.id,
                            onSelect: { selectMapping(mapping) }
                        )
                    }
                } header: {
                    Text("Beebium")
                }

                if !userMappings.isEmpty {
                    Section {
                        ForEach(userMappings) { mapping in
                            MappingRowView(
                                mapping: mapping,
                                isActive: mapping.id == mappingManager.activeMapping?.id,
                                onSelect: { selectMapping(mapping) }
                            )
                            .contextMenu {
                                if let url = mappingManager.fileURL(for: mapping) {
                                    Button {
                                        NSWorkspace.shared.activateFileViewerSelecting([url])
                                    } label: {
                                        Label("Reveal in Finder", systemImage: "folder")
                                    }
                                }
                            }
                        }
                    } header: {
                        Text("Custom")
                    }
                }
            }
            .listStyle(.sidebar)

            Divider()

            // Disableable keys section
            if !mappingManager.disableableKeyNames.isEmpty {
                ForEach(mappingManager.disableableKeyNames, id: \.self) { keyName in
                    Toggle("Disable \(keyName)", isOn: disabledKeyBinding(for: keyName))
                        .toggleStyle(.checkbox)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                }

                Divider()
            }

            Toggle("Synchronize Caps Lock", isOn: capsLockSyncBinding)
                .toggleStyle(.checkbox)
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
        }
    }

    private func selectMapping(_ mapping: KeyboardMapping) {
        mappingManager.activeMapping = mapping
    }
}

/// Row view for a single keyboard mapping
private struct MappingRowView: View {
    let mapping: KeyboardMapping
    let isActive: Bool
    let onSelect: () -> Void

    var body: some View {
        Button(action: onSelect) {
            HStack {
                Text(mapping.name)
                    .lineLimit(1)
                Spacer()
                if isActive {
                    Image(systemName: "checkmark")
                        .foregroundColor(.accentColor)
                        .font(.system(size: 12, weight: .semibold))
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// Placeholder view for Coprocessor mode (Tube coprocessors)
struct CoprocessorModeView: View {
    var body: some View {
        ModePlaceholder(mode: .coprocessor)
    }
}

/// Placeholder view for Network mode
struct NetworkModeView: View {
    var body: some View {
        ModePlaceholder(mode: .network)
    }
}

// MARK: - Common Placeholder

/// Generic placeholder view for unimplemented modes
private struct ModePlaceholder: View {
    let mode: SidebarMode

    var body: some View {
        Text("Coming soon")
            .font(.subheadline)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

#if DEBUG
struct SidebarModeContent_Previews: PreviewProvider {
    static var previews: some View {
        SidebarModeContent(
            mode: .keyboard,
            discClient: DiscClient(),
            keyboardMappingManager: KeyboardMappingManager()
        )
        .frame(width: 220, height: 300)
        .background(Color(nsColor: .windowBackgroundColor))
    }
}
#endif
