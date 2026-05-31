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

import AppKit
import SwiftUI

/// Memory sidebar mode: live view of the running machine's memory state.
///
/// Currently a single "Sideways" section listing each socket priority 15 -> 0
/// with the ROM's title (extracted from the header), a ROM/RAM/- status, and
/// a read-only actions menu. Future sections (Shadow, ANDY, ...) live as
/// additional subviews beneath; cartridge slots will get their own when a
/// machine variant with cartridge slots lands.
///
/// Read-only by design: changing what's in a slot at runtime is intentionally
/// out of scope (see docs/plans/preset-schema/sideways-bank.md).
struct MemoryModeView: View {
    @ObservedObject var sidewaysClient: SidewaysClient

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                if !sidewaysClient.isLoaded {
                    loadingView
                } else if sidewaysClient.sockets.isEmpty {
                    noSidewaysView
                } else {
                    sidewaysSection
                }
                // Future sections (Shadow / ANDY / cartridges) sit here.
            }
            .padding(.vertical, 4)
        }
    }

    // MARK: - Sideways section

    @ViewBuilder
    private var sidewaysSection: some View {
        sectionHeader("Sideways")
        ForEach(sidewaysClient.sockets) { socket in
            row(socket: socket)
            if socket.id != sidewaysClient.sockets.last?.id {
                Divider().padding(.leading, 16)
            }
        }
    }

    private func sectionHeader(_ title: String) -> some View {
        Text(title)
            .font(.caption)
            .fontWeight(.semibold)
            .foregroundColor(.secondary)
            .textCase(.uppercase)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 16)
            .padding(.top, 8)
            .padding(.bottom, 4)
    }

    private func row(socket: SidewaysClient.Socket) -> some View {
        HStack(spacing: 8) {
            // Priority (highest slot the socket answers). Tight monospaced
            // column so it lines up across rows.
            Text("\(socket.priority)")
                .font(.caption.monospacedDigit())
                .foregroundColor(.secondary)
                .frame(width: 18, alignment: .trailing)
                .help("Priority \(socket.priority) (slot\(socket.slots.count > 1 ? "s" : "") " +
                      socket.slots.map(String.init).joined(separator: ", ") + ")")

            // ROM title (with version when known), else filename, else blank
            // for empty slots - the "-" status column already signals empty.
            Text(displayName(for: socket))
                .font(.subheadline)
                .lineLimit(1)
                .truncationMode(.middle)
                .foregroundColor(.primary)
                .frame(maxWidth: .infinity, alignment: .leading)

            // ROM/RAM/- status, mirroring the running device.
            Text(statusText(for: socket))
                .font(.caption.monospaced())
                .foregroundColor(.secondary)
                .frame(width: 32, alignment: .trailing)

            // Always render the menu so populated and empty rows have
            // the exact same trailing column width; hide and disable
            // it on rows with nothing to act on. Trying to substitute
            // a bare Image as a placeholder dances the status column
            // because the Menu's internal padding doesn't match a
            // plain Image.
            let actionable = !socket.imageName.isEmpty
            actionsMenu(for: socket)
                .opacity(actionable ? 1 : 0)
                .allowsHitTesting(actionable)
                .accessibilityHidden(!actionable)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
    }

    // MARK: - Display helpers

    private func displayName(for socket: SidewaysClient.Socket) -> String {
        if let header = socket.romHeader {
            if !header.version.isEmpty {
                return "\(header.title) \(header.version)"
            }
            return header.title
        }
        if socket.populated && !socket.imageName.isEmpty {
            return URL(fileURLWithPath: socket.imageName).lastPathComponent
        }
        return ""
    }

    private func statusText(for socket: SidewaysClient.Socket) -> String {
        switch socket.kind {
        case .rom:   return "ROM"
        case .ram:   return "RAM"
        case .empty: return "\u{2014}"  // em dash
        }
    }

    // MARK: - Actions menu (read-only: Copy Path / Reveal in Finder)

    private func actionsMenu(for socket: SidewaysClient.Socket) -> some View {
        Menu {
            Button("Copy Path") {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(socket.imageName, forType: .string)
            }
            .disabled(socket.imageName.isEmpty)

            if isExistingFile(socket.imageName) {
                Button("Reveal in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting(
                        [URL(fileURLWithPath: socket.imageName)])
                }
            }
        } label: {
            Image(systemName: "ellipsis.circle")
                .foregroundColor(.secondary)
        }
        .menuStyle(.borderlessButton)
        // The disclosure chevron Menu renders by default would offset the
        // trailing column on rows that have a menu versus those that
        // don't, dancing the ROM/RAM status text out of alignment. The
        // ellipsis already signals "menu available"; the chevron is
        // redundant.
        .menuIndicator(.hidden)
        .fixedSize()
        .help("Slot actions")
    }

    private func isExistingFile(_ path: String) -> Bool {
        FileManager.default.fileExists(atPath: path)
    }

    // MARK: - Loading / empty states

    private var loadingView: some View {
        HStack(spacing: 8) {
            ProgressView().scaleEffect(0.7)
            Text("Reading sideways state\u{2026}")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .padding(12)
    }

    private var noSidewaysView: some View {
        Text("This machine has no sideways memory.")
            .font(.caption)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(12)
    }
}
