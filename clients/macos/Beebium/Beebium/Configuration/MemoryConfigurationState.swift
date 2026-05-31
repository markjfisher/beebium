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

    /// Whether a socket is empty, holds a ROM, or is sideways RAM. This is
    /// orthogonal to whatever image is loaded: a ROM is read-only, RAM is
    /// writable, but either can be populated from an image at startup.
    enum SocketKind: String, CaseIterable, Identifiable, Equatable {
        case empty
        case rom
        case ram

        var id: String { rawValue }

        var label: String {
            switch self {
            case .empty: return "Empty"
            case .rom:   return "ROM"
            case .ram:   return "RAM"
            }
        }
    }

    /// A socket's assignment: its kind plus an optional image. A ROM needs an
    /// image; sideways RAM may optionally be pre-loaded from one at startup
    /// (battery-backed RAM appears pre-loaded to the machine); empty has none.
    struct SocketContent: Equatable {
        var kind: SocketKind
        var image: String?  // ROM image, or RAM pre-load image; nil for empty/blank RAM

        static let empty = SocketContent(kind: .empty, image: nil)

        init(kind: SocketKind, image: String? = nil) {
            self.kind = kind
            self.image = image
        }

        /// Build from a preset slot's type/image strings.
        init(presetType: String, image: String?) {
            switch presetType {
            case "rom": self.init(kind: .rom, image: image)
            case "ram": self.init(kind: .ram, image: image)
            default:    self.init(kind: .empty, image: nil)
            }
        }

        /// Human-readable summary for the row.
        var displayLabel: String {
            switch kind {
            case .empty:
                return "Empty"
            case .rom:
                guard let image = image, !image.isEmpty else { return "ROM (no image)" }
                return URL(fileURLWithPath: image).lastPathComponent
            case .ram:
                guard let image = image, !image.isEmpty else { return "Sideways RAM" }
                return "Sideways RAM (preload: \(URL(fileURLWithPath: image).lastPathComponent))"
            }
        }

        /// The image path if it is an existing file on disk, for Reveal in
        /// Finder. Bare ROM-library names (from presets/defaults) return nil.
        var revealableFilepath: String? {
            guard let image = image, image.contains("/"),
                  FileManager.default.fileExists(atPath: image) else { return nil }
            return image
        }
    }

    /// One configurable socket row.
    struct SocketConfig: Identifiable {
        let id: String          // socket label, e.g. "IC52"
        let label: String
        let slots: [Int]        // logical slot numbers wired to this socket
        let priority: Int       // = max(slots); higher wins at boot
        let supportsRom: Bool
        let supportsRam: Bool
        let supportsEmpty: Bool
        /// Slot the initial content occupies (from preset or default), if any.
        let sourceSlot: Int?
        /// Content before any edit (for display and revert).
        let initialContent: SocketContent
        /// Current (possibly edited) content.
        var content: SocketContent
        /// Parsed ROM title/version for the loaded image, if it is a recognised
        /// sideways ROM (resolved asynchronously via describe-rom).
        var resolvedTitle: String? = nil
        var resolvedVersion: String? = nil
        /// What the ROM is - any combination of "language", "service", "romfs".
        /// Empty until resolved (or for an unrecognised image).
        var resolvedKinds: [String] = []

        var isChanged: Bool { content != initialContent }

        /// What to show for the contents. The kind (ROM/RAM/Empty) is shown by
        /// the adjacent control, so this describes the loaded image: a recognised
        /// ROM's title (and version) when known, otherwise the filename - the
        /// same for a ROM or a pre-loaded sideways-RAM image. An Empty socket
        /// always reads "Empty" regardless of any image retained in the model
        /// (so switching kind back to ROM/RAM restores the title cleanly).
        var displayText: String {
            guard content.kind != .empty,
                  let image = content.image, !image.isEmpty else {
                return content.displayLabel
            }
            if let titled = romTitleAndVersion { return titled }
            return URL(fileURLWithPath: image).lastPathComponent
        }

        private var romTitleAndVersion: String? {
            guard let title = resolvedTitle, !title.isEmpty else { return nil }
            if let version = resolvedVersion, !version.isEmpty { return "\(title) \(version)" }
            return title
        }

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
                content = SocketContent(kind: .rom, image: defaultRom.image)
                sourceSlot = defaultRom.slot
            } else if !socket.supportsEmpty && socket.supportsRam && !socket.supportsRom {
                // Fixed-RAM socket (e.g. B+ 128K's SRAM W/X/Y/Z): there is
                // no "empty" or "rom" state available; the socket is always
                // RAM. Reflect that in the initial content so the kind
                // picker has a valid selection to display.
                content = SocketContent(kind: .ram, image: nil)
                sourceSlot = nil
            } else {
                content = .empty
                sourceSlot = nil
            }

            rows.append(SocketConfig(
                id: socket.label,
                label: socket.label,
                slots: socket.slots,
                priority: socket.priority,
                supportsRom: socket.supportsRom,
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

    /// Reorder the *contents* (kind + image + resolved title) among the fixed
    /// sockets, leaving each socket's identity (label/slots/priority) in place.
    /// Drag-to-reorder uses this to rearrange the priority order of loaded ROMs:
    /// a socket that loses its content emits empty at launch and one that gains
    /// it emits the new content, each at its own fixed slot.
    func moveContents(fromOffsets source: IndexSet, toOffset destination: Int) {
        struct Payload {
            var content: SocketContent
            var resolvedTitle: String?
            var resolvedVersion: String?
            var resolvedKinds: [String]
        }
        var payloads = sockets.map {
            Payload(content: $0.content, resolvedTitle: $0.resolvedTitle,
                    resolvedVersion: $0.resolvedVersion, resolvedKinds: $0.resolvedKinds)
        }
        payloads.move(fromOffsets: source, toOffset: destination)
        for index in sockets.indices {
            sockets[index].content = payloads[index].content
            sockets[index].resolvedTitle = payloads[index].resolvedTitle
            sockets[index].resolvedVersion = payloads[index].resolvedVersion
            sockets[index].resolvedKinds = payloads[index].resolvedKinds
        }
    }

    /// `--sideways` arguments for the sockets the user changed. Unchanged
    /// sockets emit nothing, so the preset's own sideways_bank still applies;
    /// changed sockets override per slot via the server's merge.
    func sidewaysLaunchArguments() -> [String] {
        var arguments: [String] = []
        for socket in sockets where socket.isChanged {
            let slot = socket.emitSlot
            let image = socket.content.image
            switch socket.content.kind {
            case .empty:
                arguments.append(contentsOf: ["--sideways", "\(slot):empty"])
            case .rom:
                // A ROM needs an image; skip an incomplete selection so the
                // preset/default for the slot still applies.
                if let image = image, !image.isEmpty {
                    arguments.append(contentsOf: ["--sideways", "\(slot):rom:\(image)"])
                }
            case .ram:
                if let image = image, !image.isEmpty {
                    arguments.append(contentsOf: ["--sideways", "\(slot):ram:\(image)"])
                } else {
                    arguments.append(contentsOf: ["--sideways", "\(slot):ram"])
                }
            }
        }
        return arguments
    }

    // MARK: - ROM title resolution

    /// Resolves a ROM image reference to its parsed header (runs describe-rom),
    /// supplied by the host. Main-actor isolated so it can call PresetManager.
    var headerResolver: (@MainActor (String) async -> RomHeaderInfo?)?

    /// Resolve titles for every socket that holds an image.
    @MainActor
    func resolveTitles() async {
        for index in sockets.indices {
            await resolveTitle(at: index)
        }
    }

    /// Resolve (or clear) the title/version for one socket's loaded image.
    @MainActor
    func resolveTitle(at index: Int) async {
        guard sockets.indices.contains(index) else { return }
        guard let resolver = headerResolver,
              let image = sockets[index].content.image, !image.isEmpty else {
            sockets[index].resolvedTitle = nil
            sockets[index].resolvedVersion = nil
            sockets[index].resolvedKinds = []
            return
        }
        let info = await resolver(image)
        guard sockets.indices.contains(index) else { return }
        if let info = info, info.recognised, !info.title.isEmpty {
            sockets[index].resolvedTitle = info.title
            sockets[index].resolvedVersion = info.version
            sockets[index].resolvedKinds = info.kinds
        } else {
            sockets[index].resolvedTitle = nil
            sockets[index].resolvedVersion = nil
            sockets[index].resolvedKinds = []
        }
    }
}
