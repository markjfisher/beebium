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

import Foundation

/// Represents a connection target (host and port)
struct ConnectionTarget: Codable, Hashable {
    let host: String
    let port: Int

    var address: String { "\(host):\(port)" }

    static let localhost = ConnectionTarget(host: "127.0.0.1", port: 48875)
}

/// A persisted recent connection entry
struct RecentConnection: Codable, Identifiable, Hashable {
    let id: UUID
    let host: String
    let port: Int
    let displayName: String?
    let lastUsed: Date
    var lastSuccessful: Bool

    var target: ConnectionTarget {
        ConnectionTarget(host: host, port: port)
    }

    /// Display label for UI
    var displayLabel: String {
        if let name = displayName {
            return "\(name) (\(host):\(port))"
        }
        return "\(host):\(port)"
    }

    /// Relative time string for display
    var relativeTimeString: String {
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .abbreviated
        return formatter.localizedString(for: lastUsed, relativeTo: Date())
    }
}

/// Manages recent connection history with UserDefaults persistence
@MainActor
class RecentConnectionsManager: ObservableObject {
    static let shared = RecentConnectionsManager()

    @Published private(set) var connections: [RecentConnection] = []

    private let key = "recentConnections"
    private let maxEntries = 10

    private init() {
        load()
    }

    /// Load connections from UserDefaults
    func load() {
        guard let data = UserDefaults.standard.data(forKey: key) else {
            connections = []
            return
        }

        do {
            connections = try JSONDecoder().decode([RecentConnection].self, from: data)
        } catch {
            NSLog("[RecentConnections] Failed to decode: \(error)")
            connections = []
        }
    }

    /// Add a successful connection (or update existing)
    func addSuccessful(host: String, port: Int, displayName: String?) {
        // Remove existing entry for same host:port
        connections.removeAll { $0.host == host && $0.port == port }

        // Insert at front
        let entry = RecentConnection(
            id: UUID(),
            host: host,
            port: port,
            displayName: displayName,
            lastUsed: Date(),
            lastSuccessful: true
        )
        connections.insert(entry, at: 0)

        // Enforce max entries
        if connections.count > maxEntries {
            connections = Array(connections.prefix(maxEntries))
        }

        save()
    }

    /// Mark a connection as failed (updates lastSuccessful flag)
    func markFailed(host: String, port: Int) {
        guard let index = connections.firstIndex(where: { $0.host == host && $0.port == port }) else {
            return
        }

        let old = connections[index]
        connections[index] = RecentConnection(
            id: old.id,
            host: old.host,
            port: old.port,
            displayName: old.displayName,
            lastUsed: old.lastUsed,
            lastSuccessful: false
        )

        save()
    }

    /// Clear all recent connections
    func clear() {
        connections = []
        UserDefaults.standard.removeObject(forKey: key)
    }

    /// Save connections to UserDefaults
    private func save() {
        do {
            let data = try JSONEncoder().encode(connections)
            UserDefaults.standard.set(data, forKey: key)
        } catch {
            NSLog("[RecentConnections] Failed to encode: \(error)")
        }
    }
}
