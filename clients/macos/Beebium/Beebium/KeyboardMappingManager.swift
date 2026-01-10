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

/// Manages keyboard mappings: built-in and user-defined.
///
/// Responsible for:
/// - Creating built-in mappings
/// - Loading/saving user mappings from disk
/// - Tracking the active mapping
/// - Import/export of mapping files
@MainActor
final class KeyboardMappingManager: ObservableObject {

    /// All available mappings (built-in + user-defined)
    @Published private(set) var mappings: [KeyboardMapping] = []

    /// The currently active mapping
    @Published var activeMapping: KeyboardMapping?

    /// The BBC key cache (populated from core)
    let bbcKeyCache = BBCKeyCache()

    /// Directory for user mapping files
    private let userMappingsDirectory: URL

    init() {
        // Set up user mappings directory
        let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        userMappingsDirectory = appSupport.appendingPathComponent("Beebium/KeyboardMappings", isDirectory: true)

        // Create built-in mappings
        let builtIns = Self.createBuiltInMappings()
        mappings = builtIns

        // Set default active mapping
        activeMapping = builtIns.first
    }

    // MARK: - Cache Loading

    /// Load the BBC key cache from a gRPC response
    func loadCache(from response: Beebium_AllKeyMappingsResponse) {
        bbcKeyCache.load(from: response)
    }

    /// Whether the cache is ready for use
    var isCacheLoaded: Bool {
        bbcKeyCache.isLoaded
    }

    // MARK: - User Mappings

    /// Load user-defined mappings from disk
    func loadUserMappings() throws {
        // Ensure directory exists
        try FileManager.default.createDirectory(at: userMappingsDirectory, withIntermediateDirectories: true)

        // Find all JSON files
        let fileURLs = try FileManager.default.contentsOfDirectory(
            at: userMappingsDirectory,
            includingPropertiesForKeys: nil,
            options: .skipsHiddenFiles
        ).filter { $0.pathExtension == "json" }

        // Load each file
        for fileURL in fileURLs {
            do {
                let data = try Data(contentsOf: fileURL)
                if let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] {
                    let mapping = KeyboardMapping(json: json)
                    mappings.append(mapping)
                    print("[KeyboardMappingManager] Loaded mapping: \(mapping.name)")
                }
            } catch {
                print("[KeyboardMappingManager] Failed to load \(fileURL.lastPathComponent): \(error)")
            }
        }
    }

    /// Save all user-defined mappings to disk
    func saveUserMappings() throws {
        // Ensure directory exists
        try FileManager.default.createDirectory(at: userMappingsDirectory, withIntermediateDirectories: true)

        for mapping in mappings where !mapping.isBuiltIn {
            let json = mapping.toJSON()
            let data = try JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys])
            let fileURL = userMappingsDirectory.appendingPathComponent("\(mapping.id.uuidString).json")
            try data.write(to: fileURL)
            print("[KeyboardMappingManager] Saved mapping: \(mapping.name)")
        }
    }

    /// Save a specific mapping to disk
    func saveMapping(_ mapping: KeyboardMapping) throws {
        guard !mapping.isBuiltIn else { return }

        try FileManager.default.createDirectory(at: userMappingsDirectory, withIntermediateDirectories: true)

        let json = mapping.toJSON()
        let data = try JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys])
        let fileURL = userMappingsDirectory.appendingPathComponent("\(mapping.id.uuidString).json")
        try data.write(to: fileURL)
    }

    // MARK: - Mapping Management

    /// Clone an existing mapping with a new name
    func cloneMapping(_ mapping: KeyboardMapping, newName: String) -> KeyboardMapping {
        let uniqueName = makeUniqueName(newName)
        let clone = mapping.clone(newName: uniqueName)
        mappings.append(clone)
        return clone
    }

    /// Delete a user-defined mapping
    func deleteMapping(_ mapping: KeyboardMapping) {
        guard !mapping.isBuiltIn else { return }

        mappings.removeAll { $0.id == mapping.id }

        // Delete file if it exists
        let fileURL = userMappingsDirectory.appendingPathComponent("\(mapping.id.uuidString).json")
        try? FileManager.default.removeItem(at: fileURL)

        // If we deleted the active mapping, switch to first available
        if activeMapping?.id == mapping.id {
            activeMapping = mappings.first
        }
    }

    /// Import a mapping from JSON data
    func importMapping(from data: Data) throws -> KeyboardMapping {
        guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw MappingError.invalidJSON
        }

        let mapping = KeyboardMapping(json: json)

        // Ensure unique name
        mapping.name = makeUniqueName(mapping.name)

        mappings.append(mapping)

        // Save to disk
        try saveMapping(mapping)

        return mapping
    }

    /// Export a mapping to JSON data
    func exportMapping(_ mapping: KeyboardMapping) throws -> Data {
        let json = mapping.toJSON()
        return try JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys])
    }

    /// Import a mapping from a file URL
    func importMapping(from url: URL) throws -> KeyboardMapping {
        let data = try Data(contentsOf: url)
        return try importMapping(from: data)
    }

    /// Export a mapping to a file URL
    func exportMapping(_ mapping: KeyboardMapping, to url: URL) throws {
        let data = try exportMapping(mapping)
        try data.write(to: url)
    }

    // MARK: - Private

    private func makeUniqueName(_ base: String) -> String {
        var name = base
        var counter = 2
        while mappings.contains(where: { $0.name == name }) {
            name = "\(base) \(counter)"
            counter += 1
        }
        return name
    }

    private static func createBuiltInMappings() -> [KeyboardMapping] {
        return [
            KeyboardMapping.createDefaultLogical()
        ]
    }

    // MARK: - Errors

    enum MappingError: Error {
        case invalidJSON
        case fileNotFound
    }
}
