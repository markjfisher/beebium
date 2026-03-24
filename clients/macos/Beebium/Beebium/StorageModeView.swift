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

import SwiftUI
import UniformTypeIdentifiers

/// Supported disc image file types
/// TODO: Query server for supported extensions via gRPC API (Phase 9a)
private let discImageTypes: [UTType] = [
    UTType(filenameExtension: "ssd") ?? .data,  // DFS single-sided disc
    UTType(filenameExtension: "dsd") ?? .data,  // DFS double-sided disc
    UTType(filenameExtension: "adf") ?? .data,  // ADFS disc (auto-detect geometry)
    UTType(filenameExtension: "adl") ?? .data,  // ADFS large
    UTType(filenameExtension: "adm") ?? .data,  // ADFS medium
    UTType(filenameExtension: "ads") ?? .data,  // ADFS small
    UTType(filenameExtension: "hfe") ?? .data,  // HFE flux-level disc image
    UTType(filenameExtension: "img") ?? .data,  // Raw disc image
]

/// Storage mode view showing floppy drives with insert/eject functionality
struct StorageModeView: View {
    @ObservedObject var discClient: DiscClient

    var body: some View {
        if !discClient.isLoaded {
            loadingView
        } else if !discClient.hasDiscController {
            noControllerView
        } else {
            driveListView
        }
    }

    private var loadingView: some View {
        VStack(spacing: 12) {
            if let error = discClient.errorMessage {
                Image(systemName: "exclamationmark.triangle")
                    .font(.system(size: 24))
                    .foregroundColor(.yellow)
                Text("Connection Error")
                    .font(.headline)
                    .foregroundColor(.secondary)
                Text(error)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
            } else {
                ProgressView()
                Text("Loading...")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var noControllerView: some View {
        VStack(spacing: 12) {
            Image(systemName: "internaldrive")
                .font(.system(size: 32))
                .foregroundColor(.secondary)
            Text("No Disc Controller")
                .font(.headline)
                .foregroundColor(.secondary)
            Text("This machine does not have a disc interface")
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var driveListView: some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(Array(discClient.drives.enumerated()), id: \.offset) { index, drive in
                    DriveRowView(
                        drive: drive,
                        discClient: discClient
                    )
                    if index < discClient.drives.count - 1 {
                        Divider()
                            .padding(.horizontal, 12)
                    }
                }
            }
            .padding(.vertical, 8)
        }
    }
}

/// Row view for a single floppy drive
private struct DriveRowView: View {
    let drive: Beebium_DriveStatus
    @ObservedObject var discClient: DiscClient
    @State private var isDropTargeted = false
    @State private var isProcessing = false

    private var isEmpty: Bool {
        drive.state == .empty
    }

    private var isEjecting: Bool {
        drive.state == .ejecting
    }

    private var isLoaded: Bool {
        drive.state == .loaded
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Drive header
            HStack {
                Text("Floppy \(drive.drive)")
                    .font(.headline)
                Spacer()
                if drive.motorOn {
                    Image(systemName: "circle.fill")
                        .font(.system(size: 6))
                        .foregroundColor(.green)
                        .help("Motor running")
                }
            }

            // Content based on state
            if isEjecting {
                ejectingContent
            } else if isLoaded {
                loadedContent
            } else {
                emptyContent
            }

            // Actions row
            HStack(spacing: 8) {
                browseButton
                Spacer()
                ejectButton
            }
        }
        .padding(12)
        .background(isDropTargeted ? Color.accentColor.opacity(0.1) : Color.clear)
        .contentShape(Rectangle())
        .onDrop(of: [.fileURL], isTargeted: $isDropTargeted) { providers in
            handleDrop(providers)
        }
    }

    // MARK: - Content Views

    private var emptyContent: some View {
        Text("Empty - Drop disc image here")
            .font(.caption)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var loadedContent: some View {
        VStack(alignment: .leading, spacing: 4) {
            // Disc name with path tooltip and context menu
            if !drive.discName.isEmpty {
                Text(drive.discName)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .lineLimit(1)
                    .help(fullPath)
                    .contextMenu {
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(fullPath, forType: .string)
                        } label: {
                            Label("Copy Path", systemImage: "doc.on.doc")
                        }

                        if let url = fileURL {
                            Button {
                                NSWorkspace.shared.activateFileViewerSelecting([url])
                            } label: {
                                Label("Reveal in Finder", systemImage: "folder")
                            }
                        }
                    }
            }

            // Write protection status
            if drive.writeProtected {
                HStack(spacing: 4) {
                    Image(systemName: "lock.fill")
                        .font(.caption2)
                    Text("Read-only")
                        .font(.caption)
                }
                .foregroundColor(.secondary)
                .help("Disc is write-protected")
            }
        }
    }

    private var fullPath: String {
        if let url = URL(string: drive.discURL) {
            return url.path
        }
        return drive.discURL
    }

    private var fileURL: URL? {
        URL(string: drive.discURL)
    }

    private var ejectingContent: some View {
        HStack(spacing: 8) {
            ProgressView()
                .scaleEffect(0.7)
            Text("Ejecting...")
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    // MARK: - Buttons

    private var browseButton: some View {
        let isEnabled = isEmpty && !isProcessing
        return Button {
            browseForDisc()
        } label: {
            Image(systemName: "folder")
                .font(.system(size: 16))
                .foregroundColor(isEnabled ? .primary.opacity(0.6) : .secondary.opacity(0.3))
        }
        .buttonStyle(.borderless)
        .disabled(!isEnabled)
        .help(isEmpty ? "Browse for disc image" : "Eject disc first")
    }

    private var ejectButton: some View {
        Button {
            ejectDisc()
        } label: {
            Image(systemName: "eject.fill")
                .font(.system(size: 14))
        }
        .buttonStyle(.bordered)
        .controlSize(.regular)
        .disabled(isEmpty || isEjecting || isProcessing)
        .help("Eject disc")
    }

    // MARK: - Actions

    private func browseForDisc() {
        let panel = NSOpenPanel()
        panel.title = "Select Disc Image"
        panel.allowedContentTypes = discImageTypes
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false

        if panel.runModal() == .OK, let url = panel.url {
            insertDisc(url: url)
        }
    }

    private func insertDisc(url: URL) {
        guard isEmpty else { return }

        isProcessing = true
        Task {
            let result = await discClient.insertDisc(drive: Int(drive.drive), url: url)
            await MainActor.run {
                isProcessing = false
                if case .failure(let error) = result {
                    NSLog("[StorageModeView] Insert failed: \(error.localizedDescription)")
                }
            }
        }
    }

    private func ejectDisc() {
        guard isLoaded else { return }

        isProcessing = true
        Task {
            let result = await discClient.ejectDisc(drive: Int(drive.drive), immediate: false)
            await MainActor.run {
                isProcessing = false
                if case .failure(let error) = result {
                    NSLog("[StorageModeView] Eject failed: \(error.localizedDescription)")
                }
            }
        }
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard isEmpty else { return false }

        for provider in providers {
            if provider.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) {
                provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, error in
                    if let data = item as? Data,
                       let url = URL(dataRepresentation: data, relativeTo: nil) {
                        // Check if file extension is supported
                        let ext = url.pathExtension.lowercased()
                        if ["ssd", "dsd", "adf", "adl", "adm", "ads", "hfe", "img"].contains(ext) {
                            Task { @MainActor in
                                self.insertDisc(url: url)
                            }
                        }
                    }
                }
                return true
            }
        }
        return false
    }
}

#if DEBUG
struct StorageModeView_Previews: PreviewProvider {
    static var previews: some View {
        StorageModeView(discClient: DiscClient())
            .frame(width: 220, height: 300)
            .background(Color(nsColor: .windowBackgroundColor))
    }
}
#endif
