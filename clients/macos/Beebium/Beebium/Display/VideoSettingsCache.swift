// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Snapshot of the VideoSettings fields we cache per machine.
///
/// Only the per-window settings owned directly by VideoSettings are captured.
/// Per-style parameters (e.g. Standard's edgeMargin) live on the style
/// instances and are not persisted across windows; they remain at their
/// per-window defaults when a snapshot is restored.
///
/// All fields are stored as their primitive-string forms so the snapshot is
/// independent of the live style instances and trivially comparable.
struct VideoSettingsSnapshot: Equatable {
    let activeStyleID: String
    let pixelShapeRaw: String
    let windowBackgroundHex: String
}

/// In-memory cache mapping machine UUIDs to the user's most recent
/// `VideoSettings` choices on that machine.
///
/// Lookups are scoped by `MachineIdentity.uuid` (`SystemClient.machineUUID`),
/// which is stable for the lifetime of a running emulator server and is
/// available for any connection - including ad-hoc connections to servers
/// the client did not launch. This is the same identifier the user agreed
/// to use for the per-machine cache; persistence across server restarts and
/// client launches is a future refinement.
///
/// The cache is intentionally process-scoped: when the macOS client
/// terminates, the cached snapshots are gone and new windows will start from
/// the global defaults in `Settings > Video` again.
@MainActor
final class VideoSettingsCache {
    /// Singleton used by ContentView. Tests construct their own instance to
    /// keep test runs isolated from each other and from production state.
    static let shared = VideoSettingsCache()

    private var snapshots: [String: VideoSettingsSnapshot] = [:]

    /// Construct an empty cache. Public so tests can create an isolated cache.
    init() {}

    /// Fetch the snapshot last saved for this machine, or nil if none exists.
    func snapshot(forMachineUUID uuid: String) -> VideoSettingsSnapshot? {
        guard !uuid.isEmpty else { return nil }
        return snapshots[uuid]
    }

    /// Save (or replace) the snapshot for this machine. Empty UUIDs are
    /// silently ignored - the cache only stores meaningful identifiers.
    func save(_ snapshot: VideoSettingsSnapshot, forMachineUUID uuid: String) {
        guard !uuid.isEmpty else { return }
        snapshots[uuid] = snapshot
    }

    /// Remove all entries. Used by tests; not exposed to the UI.
    func clear() {
        snapshots.removeAll()
    }

    /// Number of snapshots currently cached.
    var count: Int { snapshots.count }
}
