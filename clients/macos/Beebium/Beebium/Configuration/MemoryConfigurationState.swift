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

import Foundation

/// Observable state for the Memory (sideways ROM/RAM) configuration tab.
///
/// Built from the machine's sideways_bank schema plus the selected preset, it
/// presents one row per physical socket, ordered by boot priority (highest
/// slot first, mirroring the MOS scan). Each socket's initial content is the
/// preset's assignment if any, otherwise the machine's default ROM, otherwise
/// empty. The user can override a socket; only changed sockets emit
/// `--sideways` at launch, layering over the preset.
class MemoryConfigurationState: ObservableObject {

    /// What a socket holds.
    enum SocketContent: Equatable {
        case empty
        case ram(imageFilepath: String?)  // optional pre-load image
        case rom(image: String)           // ROM library name or file path

        /// Build from a preset slot's type/image strings.
        init(presetType: String, image: String?) {
            switch presetType {
            case "rom": self = .rom(image: image ?? "")
            case "ram": self = .ram(imageFilepath: image)
            default:    self = .empty
            }
        }

        /// Human-readable summary for the row.
        var displayLabel: String {
            switch self {
            case .empty:
                return "Empty"
            case .ram(let path):
                if let path = path {
                    return "Sideways RAM (preload: \(URL(fileURLWithPath: path).lastPathComponent))"
                }
                return "Sideways RAM"
            case .rom(let image):
                if image.isEmpty { return "ROM" }
                // Show just the filename whether it's a bare name or a path.
                return URL(fileURLWithPath: image).lastPathComponent
            }
        }
    }

    /// One configurable socket row.
    struct SocketConfig: Identifiable {
        let id: String          // socket label, e.g. "IC52"
        let label: String
        let slots: [Int]        // logical slot numbers wired to this socket
        let priority: Int       // = max(slots); higher wins at boot
        let supportsRam: Bool
        let supportsEmpty: Bool
        /// Slot the initial content occupies (from preset or default), if any.
        let sourceSlot: Int?
        /// Content before any edit (for display and revert).
        let initialContent: SocketContent
        /// Current (possibly edited) content.
        var content: SocketContent

        var isChanged: Bool { content != initialContent }

        /// Slot used when emitting `--sideways` for this socket. Using the
        /// slot the initial content occupied keeps the server's per-slot
        /// override aligned with the preset and avoids aliased-socket conflicts;
        /// for a previously empty socket the highest (priority) slot is used.
        var emitSlot: Int { sourceSlot ?? priority }

        /// Comma-separated slot numbers for display, e.g. "0, 4, 8, 12".
        var slotsLabel: String { slots.map(String.init).joined(separator: ", ") }
    }

    @Published var sockets: [SocketConfig] = []

    /// Rebuild rows from the machine schema and the preset's sideways assignments.
    func configure(schema: SidewaysSchemaSection, presetSlots: [PresetSidewaysSlot]) {
        var rows: [SocketConfig] = []
        for socket in schema.sockets {
            let presetSlot = presetSlots.first { socket.slots.contains($0.slot) }
            let defaultRom = schema.defaultRoms.first { socket.slots.contains($0.slot) }

            let content: SocketContent
            let sourceSlot: Int?
            if let presetSlot = presetSlot {
                content = SocketContent(presetType: presetSlot.type, image: presetSlot.imageUri)
                sourceSlot = presetSlot.slot
            } else if let defaultRom = defaultRom {
                content = .rom(image: defaultRom.image)
                sourceSlot = defaultRom.slot
            } else {
                content = .empty
                sourceSlot = nil
            }

            rows.append(SocketConfig(
                id: socket.label,
                label: socket.label,
                slots: socket.slots,
                priority: socket.priority,
                supportsRam: socket.supportsRam,
                supportsEmpty: socket.supportsEmpty,
                sourceSlot: sourceSlot,
                initialContent: content,
                content: content))
        }
        // Highest priority (highest slot) first, mirroring the boot scan order.
        sockets = rows.sorted { $0.priority > $1.priority }
    }

    /// True if any socket has been changed from its preset/default content.
    var hasChanges: Bool { sockets.contains { $0.isChanged } }

    /// Reset every socket to its initial (preset/default) content.
    func revertAll() {
        for index in sockets.indices {
            sockets[index].content = sockets[index].initialContent
        }
    }

    /// `--sideways` arguments for the sockets the user changed. Unchanged
    /// sockets emit nothing, so the preset's own sideways_bank still applies;
    /// changed sockets override per slot via the server's merge.
    func sidewaysLaunchArguments() -> [String] {
        var arguments: [String] = []
        for socket in sockets where socket.isChanged {
            let slot = socket.emitSlot
            switch socket.content {
            case .empty:
                arguments.append(contentsOf: ["--sideways", "\(slot):empty"])
            case .ram(let image):
                if let image = image, !image.isEmpty {
                    arguments.append(contentsOf: ["--sideways", "\(slot):ram:\(image)"])
                } else {
                    arguments.append(contentsOf: ["--sideways", "\(slot):ram"])
                }
            case .rom(let image):
                arguments.append(contentsOf: ["--sideways", "\(slot):rom:\(image)"])
            }
        }
        return arguments
    }
}
