// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Sidebar mode showing the configured peripheral extensions in a
/// hierarchical tree rooted at the built-in extension points (1 MHz
/// bus, user port, ...). Multi-port devices appear once under their
/// primary attachment with the other extension points shown as
/// secondary badges.
///
/// This first cut renders peripheral identity and configuration only.
/// Future iterations will fold in per-peripheral ExtensionUi panels
/// (Indicator, Button, etc.) using the same declarative framework the
/// Network sidebar uses for AUN and Piconet.
struct PeripheralsModeView: View {
    @ObservedObject var client: PeripheralsClient

    var body: some View {
        Group {
            if !client.isLoaded {
                loadingView
            } else if let error = client.errorMessage,
                      client.tree.groups.isEmpty,
                      client.tree.orphans.isEmpty {
                errorView(error)
            } else if client.tree.groups.isEmpty && client.tree.orphans.isEmpty {
                emptyView
            } else {
                treeContent
            }
        }
    }

    // MARK: - States

    private var loadingView: some View {
        VStack(spacing: 8) {
            ProgressView()
            Text("Loading...")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func errorView(_ message: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 24))
                .foregroundColor(.yellow)
            Text("Couldn't load peripherals")
                .font(.headline)
                .foregroundColor(.secondary)
            Text(message)
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var emptyView: some View {
        VStack(spacing: 12) {
            Image(systemName: "puzzlepiece.extension")
                .font(.system(size: 32))
                .foregroundColor(.secondary)
            Text("No Peripherals")
                .font(.headline)
                .foregroundColor(.secondary)
            Text("This machine is configured without any external peripherals.")
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Tree

    private var treeContent: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                ForEach(Array(client.tree.groups.enumerated()),
                        id: \.element.id) { index, group in
                    if index > 0 {
                        Divider().padding(.horizontal, 12)
                    }
                    ExtensionPointSection(group: group)
                }
                if !client.tree.orphans.isEmpty {
                    if !client.tree.groups.isEmpty {
                        Divider().padding(.horizontal, 12)
                    }
                    OrphanSection(orphans: client.tree.orphans)
                }
            }
            .padding(.vertical, 8)
        }
    }
}

// MARK: - Sections

private struct ExtensionPointSection: View {
    let group: PeripheralTree.ExtensionPointGroup

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(PeripheralLabels.extensionPoint(group.extensionPoint))
                .font(.subheadline.weight(.semibold))
                .foregroundColor(.secondary)
                .padding(.horizontal, 16)
                .padding(.top, 8)
                .padding(.bottom, 2)

            ForEach(group.nodes) { node in
                PeripheralNodeRow(node: node, depth: 0)
            }
        }
    }
}

private struct OrphanSection: View {
    let orphans: [PeripheralNode]

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Unattached")
                .font(.subheadline.weight(.semibold))
                .foregroundColor(.secondary)
                .padding(.horizontal, 16)
                .padding(.top, 8)
                .padding(.bottom, 2)

            ForEach(orphans) { node in
                PeripheralNodeRow(node: node, depth: 0)
            }
        }
    }
}

// MARK: - Row

private struct PeripheralNodeRow: View {
    let node: PeripheralNode
    let depth: Int

    private var leadingPadding: CGFloat {
        // 16pt base indent for the section + 16pt per nesting level.
        16 + CGFloat(depth) * 16
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text(node.displayName)
                    .font(.body)
                Spacer()
                ForEach(node.secondaryAttachments, id: \.self) { point in
                    Text("also: \(PeripheralLabels.extensionPoint(point))")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 1)
                        .background(
                            Capsule().stroke(Color.secondary.opacity(0.4),
                                             lineWidth: 1)
                        )
                }
            }
            .padding(.leading, leadingPadding)
            .padding(.trailing, 16)
            .padding(.vertical, 4)

            ForEach(node.children) { child in
                PeripheralNodeRow(node: child, depth: depth + 1)
            }
        }
    }
}

#if DEBUG
struct PeripheralsModeView_Previews: PreviewProvider {
    static var previews: some View {
        PeripheralsModeView(client: PeripheralsClient())
            .frame(width: 280, height: 400)
    }
}
#endif
