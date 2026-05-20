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

import Foundation

/// One configured peripheral extension and its descendants.
///
/// `primaryAttachment` names the extension point we mounted this node
/// under (the first entry in its `attaches_to` list). `secondaryAttachments`
/// holds any further extension points the device also occupies -- used
/// for multi-port devices like the Voltmace Delta 14B/1, which plugs
/// into the user port and the analogue port simultaneously. The node
/// is rendered once (under the primary) with the secondaries shown as
/// badges, so the conceptual DAG collapses cleanly to a tree.
struct PeripheralNode: Identifiable, Hashable {
    let id: String
    let name: String
    let label: String
    let description: String
    let config: [String: String]
    let primaryAttachment: String
    let secondaryAttachments: [String]
    let provides: [String]
    var children: [PeripheralNode]

    /// Best human-readable name for the default (no-ExtensionUi) row.
    ///
    /// Falls through in order:
    /// 1. `label` if it was explicitly set (i.e. distinct from `id`,
    ///    which is the server's default fallback when no label was
    ///    given by the user).
    /// 2. Built-in display name for the manifest `name` slug, if
    ///    known to the client.
    /// 3. A humanised form of the slug.
    ///
    /// The manifest `description` is deliberately NOT in this chain --
    /// descriptions are catalogue prose (often with parenthetical
    /// detail), too long for a sidebar row. If out-of-tree plugins
    /// ever appear and want to ship their own display name, we can
    /// add a `display_name` field to the manifest then.
    var displayName: String {
        if !label.isEmpty && label != id {
            return label
        }
        if let known = PeripheralLabels.extensionType(name) {
            return known
        }
        return PeripheralNameFormatter.humanise(name)
    }
}

/// Display-name lookup tables for the Peripherals sidebar.
///
/// Two distinct mappings, both keyed by the canonical slug:
/// - `extensionPoint(_:)` for the section headers and multi-attach
///   badges ("1mhz-bus" -> "1 MHz Bus").
/// - `extensionType(_:)` for the default row, when no per-instance
///   label has been set ("acorn-scsi" -> "SCSI Adapter").
///
/// Returning the slug unchanged (for `extensionPoint`) or nil (for
/// `extensionType`) for unknown keys keeps the view honest: nothing
/// silently disappears, and the displayName fallback chain in
/// `PeripheralNode.displayName` falls through to a humanised slug if
/// the type is unknown.
enum PeripheralLabels {
    private static let extensionPointNames: [String: String] = [
        "1mhz-bus": "1 MHz Bus",
        "user-port": "User Port",
        "tube": "Tube",
        "scsi": "SCSI Bus",
        "analogue-port": "Analogue Port",
        "printer-port": "Printer Port",
        "serial-port": "Serial Port",
    ]

    private static let extensionTypeNames: [String: String] = [
        "acorn-scsi": "SCSI Adapter",
        "scsi-hard-disc": "SCSI HDD",
        "acorn-rtc": "Real-Time Clock",
        "piconet": "Piconet",
        "acorn-65c02-coprocessor": "65C02 Co-processor",
        "test-scratch-ram": "Test Scratch RAM",
    ]

    static func extensionPoint(_ name: String) -> String {
        extensionPointNames[name] ?? name
    }

    static func extensionType(_ name: String) -> String? {
        extensionTypeNames[name]
    }
}

/// Title-cases a manifest name slug (kebab-case) into a human-friendly
/// form, upper-casing well-known acronyms. Used as the last-resort
/// fallback when an extension has no explicit label and isn't in the
/// client's known-type table.
enum PeripheralNameFormatter {
    private static let acronyms: Set<String> = [
        "scsi", "rtc", "usb", "aun", "adfs", "dfs", "nfs",
        "rom", "ram", "led", "vdu", "fdc", "ide", "spi",
        "i2c", "uart", "io", "rs423", "bbc",
    ]

    static func humanise(_ slug: String) -> String {
        slug.split(separator: "-").map { token -> String in
            let lower = token.lowercased()
            if acronyms.contains(lower) {
                return lower.uppercased()
            }
            return lower.prefix(1).uppercased() + lower.dropFirst()
        }.joined(separator: " ")
    }
}

/// Hierarchical view of the configured peripherals, rooted at the built-in
/// extension points (1mhz-bus, user-port, tube, ...).
///
/// `groups` is ordered alphabetically by extension point name for stable
/// display; nodes within a group preserve the order returned by the server
/// (which corresponds to command-line / preset configuration order).
///
/// `orphans` is a defensive bucket for extensions with an empty
/// `attaches_to`; the server's PeripheralExtension contract requires at
/// least one attachment so in practice it stays empty.
struct PeripheralTree: Equatable {

    struct ExtensionPointGroup: Equatable, Identifiable {
        let extensionPoint: String
        let nodes: [PeripheralNode]
        var id: String { extensionPoint }
    }

    let groups: [ExtensionPointGroup]
    let orphans: [PeripheralNode]

    static func build(from extensions: [Beebium_ExtensionInfo]) -> PeripheralTree {
        if extensions.isEmpty {
            return PeripheralTree(groups: [], orphans: [])
        }

        // Map each extension point provided by some loaded extension to
        // the id of that provider. Used to decide whether a child's
        // primary attachment resolves to another extension (parent-child
        // nesting) or to a built-in extension point (root group).
        var providerIdByPoint: [String: String] = [:]
        for ext in extensions {
            for point in ext.provides {
                providerIdByPoint[point] = ext.id
            }
        }

        // Build the leaf-less nodes and index by id so the second pass
        // can hang children off them in any input order.
        var nodesById: [String: PeripheralNode] = [:]
        for ext in extensions {
            let primary = ext.attachesTo.first ?? ""
            let secondary = ext.attachesTo.count > 1
                ? Array(ext.attachesTo.dropFirst())
                : []
            nodesById[ext.id] = PeripheralNode(
                id: ext.id,
                name: ext.name,
                label: ext.label.isEmpty ? ext.id : ext.label,
                description: ext.description_p,
                config: ext.config,
                primaryAttachment: primary,
                secondaryAttachments: secondary,
                provides: ext.provides,
                children: []
            )
        }

        // Classify each extension as either a child of another loaded
        // extension or a root under some extension point name. Empty
        // `attaches_to` is treated as orphan rather than indexed under
        // an empty group name.
        var childIdsByParentId: [String: [String]] = [:]
        var rootIdsByPoint: [String: [String]] = [:]
        var orphanIds: [String] = []

        for ext in extensions {
            guard let primary = ext.attachesTo.first, !primary.isEmpty else {
                orphanIds.append(ext.id)
                continue
            }
            if let parentId = providerIdByPoint[primary] {
                childIdsByParentId[parentId, default: []].append(ext.id)
            } else {
                rootIdsByPoint[primary, default: []].append(ext.id)
            }
        }

        // Recursively walk from each root, attaching children. The
        // server's topological init order guarantees no cycles, but the
        // recursion would terminate naturally even without that
        // guarantee because a node's children are looked up by its id,
        // which never recurses back to itself.
        func attachChildren(to nodeId: String) -> PeripheralNode {
            var node = nodesById[nodeId]!
            let childIds = childIdsByParentId[nodeId, default: []]
            node.children = childIds.map { attachChildren(to: $0) }
            return node
        }

        let sortedPoints = rootIdsByPoint.keys.sorted()
        let groups = sortedPoints.map { point -> ExtensionPointGroup in
            let ids = rootIdsByPoint[point] ?? []
            return ExtensionPointGroup(
                extensionPoint: point,
                nodes: ids.map { attachChildren(to: $0) }
            )
        }

        let orphans = orphanIds.compactMap { nodesById[$0] }

        return PeripheralTree(groups: groups, orphans: orphans)
    }
}
