// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

import SwiftUI
import UniformTypeIdentifiers

/// Memory (sideways ROM/RAM) configuration section.
///
/// Lists the machine's physical sideways sockets, highest boot-priority first,
/// showing the slot number(s) each answers and what it holds. Each socket can
/// be overridden (empty / sideways RAM / a ROM file); changes apply at launch
/// via --sideways, layering over the preset.
struct MemorySectionView: View {
    @ObservedObject var memoryConfig: MemoryConfigurationState
    let schema: SidewaysSchemaSection?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                header

                if memoryConfig.sockets.isEmpty {
                    Text("This machine has no configurable sideways sockets.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.vertical, 8)
                } else {
                    ForEach(memoryConfig.sockets.indices, id: \.self) { index in
                        socketRow(index: index)
                        if index < memoryConfig.sockets.count - 1 {
                            Divider().padding(.horizontal, 4)
                        }
                    }
                }
            }
            .padding(12)
        }
    }

    // MARK: - Header

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Sideways ROM / RAM")
                .font(.headline)
            Text("Listed highest-priority first. At reset the machine enters the language ROM in the highest-numbered slot.")
                .font(.caption)
                .foregroundColor(.secondary)
            if schema?.hasAliasing == true {
                Text("Each socket on this machine answers several aliased slot numbers.")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.bottom, 8)
    }

    // MARK: - Socket Row

    private func socketRow(index: Int) -> some View {
        let socket = memoryConfig.sockets[index]
        return HStack(alignment: .top, spacing: 12) {
            // Identity: socket label, slot numbers, priority.
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 5) {
                    Text(socket.label)
                        .font(.subheadline)
                        .fontWeight(.medium)
                    if socket.isChanged {
                        Image(systemName: "pencil.circle.fill")
                            .font(.caption2)
                            .foregroundColor(.accentColor)
                            .help("Changed from the preset's configuration")
                    }
                }
                Text(socket.slots.count > 1 ? "Slots \(socket.slotsLabel)" : "Slot \(socket.slotsLabel)")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Text("Priority \(socket.priority)")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
            .frame(width: 130, alignment: .leading)

            // Current contents.
            Text(socket.content.displayLabel)
                .font(.subheadline)
                .foregroundColor(socket.content.kind == .empty ? .secondary : .primary)
                .lineLimit(1)
                .truncationMode(.middle)
                .frame(maxWidth: .infinity, alignment: .leading)

            // Tri-state ROM / RAM / Empty control (writability of the socket),
            // independent of any image loaded into it.
            kindPicker(index: index, socket: socket)

            // Verbs menu: browse / clear the image, reveal it, revert.
            actionsMenu(index: index, socket: socket)
        }
        .padding(.vertical, 8)
    }

    private func kindPicker(index: Int, socket: MemoryConfigurationState.SocketConfig) -> some View {
        Picker("", selection: Binding(
            get: { memoryConfig.sockets[index].content.kind },
            set: { memoryConfig.sockets[index].content.kind = $0 }
        )) {
            // Only offer kinds the physical socket can actually be.
            if socket.supportsRom { Text("ROM").tag(MemoryConfigurationState.SocketKind.rom) }
            if socket.supportsRam { Text("RAM").tag(MemoryConfigurationState.SocketKind.ram) }
            if socket.supportsEmpty { Text("Empty").tag(MemoryConfigurationState.SocketKind.empty) }
        }
        .pickerStyle(.menu)
        .labelsHidden()
        .frame(width: 88)
        .help("ROM (read-only), RAM (writable), or empty")
    }

    private func actionsMenu(index: Int, socket: MemoryConfigurationState.SocketConfig) -> some View {
        Menu {
            Button("Browse for ROM image…") { chooseImage(index: index) }
                .disabled(socket.content.kind == .empty)
            Button("Clear") { memoryConfig.sockets[index].content.image = nil }
                .disabled(socket.content.image == nil)
            if let filepath = socket.content.revealableFilepath {
                Button("Reveal in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: filepath)])
                }
            }
            if socket.isChanged {
                Divider()
                Button("Revert to Default") {
                    memoryConfig.sockets[index].content = socket.initialContent
                }
            }
        } label: {
            Image(systemName: "ellipsis.circle")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .help("Image actions for this socket")
    }

    // MARK: - Actions

    /// Browse for a ROM image to load into the socket. For a ROM socket this is
    /// the ROM; for a RAM socket it is an optional startup pre-load image.
    private func chooseImage(index: Int) {
        let panel = NSOpenPanel()
        panel.title = "Select ROM Image"
        panel.allowedContentTypes = [UTType(filenameExtension: "rom") ?? .data]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false

        if panel.runModal() == .OK, let url = panel.url {
            memoryConfig.sockets[index].content.image = url.path
        }
    }
}
