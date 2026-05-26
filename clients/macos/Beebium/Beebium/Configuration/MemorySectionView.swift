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
        VStack(alignment: .leading, spacing: 0) {
            header
                .padding(.horizontal, 12)
                .padding(.top, 12)

            if memoryConfig.sockets.isEmpty {
                Text("This machine has no configurable sideways sockets.")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .padding(12)
                Spacer()
            } else {
                // Rows are draggable to reorder: dragging moves a socket's
                // contents (image + kind), not its identity, so the sockets stay
                // in priority order while the ROMs move between them.
                List {
                    ForEach(memoryConfig.sockets) { socket in
                        if let index = memoryConfig.sockets.firstIndex(where: { $0.id == socket.id }) {
                            socketRow(index: index)
                        }
                    }
                    .onMove { source, destination in
                        memoryConfig.moveContents(fromOffsets: source, toOffset: destination)
                    }
                }
                .listStyle(.plain)
            }
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
        return VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 8) {
                // Socket label: a narrow fixed column so the contents line up.
                Text(socket.label)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .frame(width: 52, alignment: .leading)

                // Contents: ROM title/version when known, else filename.
                Text(socket.displayText)
                    .font(.subheadline)
                    .foregroundColor(socket.content.kind == .empty ? .secondary : .primary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .frame(maxWidth: .infinity, alignment: .leading)

                if socket.isChanged {
                    Image(systemName: "pencil.circle.fill")
                        .font(.caption)
                        .foregroundColor(.accentColor)
                        .help("Changed from the preset's configuration")
                }

                // Tri-state ROM / RAM / Empty control (socket writability),
                // independent of any image loaded into it.
                kindPicker(index: index, socket: socket)

                // Verbs menu: browse / clear / copy / reveal the image, revert.
                actionsMenu(index: index, socket: socket)
            }

            // Slot number(s) and priority (the highest slot the socket answers).
            Text("\(socket.slots.count > 1 ? "Slots" : "Slot") \(socket.slotsLabel) · priority \(socket.priority)")
                .font(.caption2)
                .foregroundColor(.secondary)
                .padding(.leading, 60)  // align under the contents, past the label column
        }
        .padding(.vertical, 6)
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
            Button("Clear") {
                memoryConfig.sockets[index].content.image = nil
                memoryConfig.sockets[index].resolvedTitle = nil
                memoryConfig.sockets[index].resolvedVersion = nil
            }
            .disabled(socket.content.image == nil)
            Button("Copy Path") {
                if let image = socket.content.image {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(image, forType: .string)
                }
            }
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
            // Browsing into an empty socket loads it as a ROM (one less step);
            // a RAM socket keeps its kind and treats the image as a preload.
            if memoryConfig.sockets[index].content.kind == .empty {
                memoryConfig.sockets[index].content.kind = .rom
            }
            memoryConfig.sockets[index].content.image = url.path
            memoryConfig.sockets[index].resolvedTitle = nil
            memoryConfig.sockets[index].resolvedVersion = nil
            Task { await memoryConfig.resolveTitle(at: index) }
        }
    }
}
