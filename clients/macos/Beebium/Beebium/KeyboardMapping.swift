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

/// A reference to a BBC key with optional modifier overrides.
struct BBCKeyRef {
    /// The BBC key name (e.g., "Up", "f0", "a", "Return")
    let bbcKeyName: String

    /// Override BBC Shift state (nil = use default from cache)
    let bbcWithShift: Bool?

    /// Override BBC Ctrl state (nil = use default, typically false)
    let bbcWithCtrl: Bool?
}

/// A key mapping entry from JSON that preserves unknown platform keys for round-tripping.
///
/// The macOS client only interprets `macOSKeyCode` and `bbcKeyName`; other platform
/// keys (windowsVKCode, linuxKeyCode, etc.) are preserved but not understood.
struct KeyMappingEntry {
    /// All key/value pairs from JSON (preserves unknown platform keys)
    var rawJSON: [String: Any]

    /// Extracted for macOS use (nil if this entry has no macOSKeyCode)
    var macOSKeyCode: UInt16? {
        if let intValue = rawJSON["macOSKeyCode"] as? Int {
            return UInt16(intValue)
        }
        return rawJSON["macOSKeyCode"] as? UInt16
    }

    /// The BBC key name this maps to
    var bbcKeyName: String? {
        rawJSON["bbcKeyName"] as? String
    }

    /// Override for BBC Shift key state
    var bbcWithShift: Bool? {
        rawJSON["bbcWithShift"] as? Bool
    }

    /// Override for BBC Ctrl key state
    var bbcWithCtrl: Bool? {
        rawJSON["bbcWithCtrl"] as? Bool
    }

    /// Create from JSON dictionary (preserves all keys)
    init(json: [String: Any]) {
        self.rawJSON = json
    }

    /// Create programmatically for macOS
    init(macOSKeyCode: UInt16, bbcKeyName: String, bbcWithShift: Bool? = nil, bbcWithCtrl: Bool? = nil) {
        var json: [String: Any] = [
            "macOSKeyCode": Int(macOSKeyCode),
            "bbcKeyName": bbcKeyName
        ]
        if let shift = bbcWithShift {
            json["bbcWithShift"] = shift
        }
        if let ctrl = bbcWithCtrl {
            json["bbcWithCtrl"] = ctrl
        }
        self.rawJSON = json
    }
}

/// A keyboard mapping that translates host key input to BBC Micro keys.
///
/// Each mapping has:
/// - `characterMapping`: Whether to use character→BBC key resolution
/// - `keyMappings`: Direct keyCode→BBC key mappings (checked first)
///
/// Resolution order:
/// 1. Check `keyMappings` for the keyCode
/// 2. If not found and `characterMapping` is true, look up the character in the cache
final class KeyboardMapping: Identifiable {

    /// Unique identifier for this mapping
    let id: UUID

    /// Human-readable name (e.g., "Logical (Default)", "Thrust")
    var name: String

    /// Whether this mapping is built-in (read-only)
    let isBuiltIn: Bool

    /// Enable character→BBC key resolution
    var characterMapping: Bool

    /// Whether to synchronize macOS Caps Lock state with BBC Caps Lock
    var synchronizeCapsLock: Bool

    /// Direct keyCode→BBC key mappings (checked first)
    /// Extracted from allEntries for macOS use
    var keyMappings: [UInt16: BBCKeyRef]

    /// All entries from JSON (for round-trip serialization)
    /// Includes entries for other platforms that we don't understand
    private var allEntries: [KeyMappingEntry]

    /// The result of resolving an input to a BBC key
    struct ResolvedKey {
        /// BBC key name (e.g., "A", "Return", "Break")
        let bbcKeyName: String

        /// Internal key number: (row << 4) | column
        /// Note: For Break key, this is 0 (not a matrix key)
        let ikNumber: UInt8

        /// Whether BBC Shift should be pressed
        let bbcShift: Bool

        /// Whether BBC Ctrl should be pressed
        let bbcCtrl: Bool

        /// Whether this is the Break key (requires special handling)
        var isBreak: Bool { bbcKeyName == "Break" }
    }

    /// Resolve a key input to a BBC key using this mapping
    /// - Parameters:
    ///   - input: The key input event
    ///   - cache: The BBC key cache for character/name lookups
    /// - Returns: The resolved BBC key, or nil if not mapped
    func resolve(_ input: KeyInput, cache: BBCKeyCache) -> ResolvedKey? {
        // 1. Check keyMappings first (physical mapping)
        if let ref = keyMappings[input.keyCode] {
            // Break key is not in the matrix - handle specially
            if ref.bbcKeyName == "Break" {
                return ResolvedKey(
                    bbcKeyName: "Break",
                    ikNumber: 0,  // Not a matrix key
                    bbcShift: ref.bbcWithShift ?? false,
                    bbcCtrl: ref.bbcWithCtrl ?? false
                )
            }

            // Normal matrix key - look up in cache
            if let entry = cache.lookup(name: ref.bbcKeyName) {
                return ResolvedKey(
                    bbcKeyName: entry.name,
                    ikNumber: entry.ikNumber,
                    bbcShift: ref.bbcWithShift ?? entry.needsShift,
                    bbcCtrl: ref.bbcWithCtrl ?? false
                )
            }
        }

        // 2. If characterMapping enabled, try character lookup
        if characterMapping,
           let char = input.primaryCharacter,
           let entry = cache.lookup(character: char) {
            return ResolvedKey(
                bbcKeyName: entry.name,
                ikNumber: entry.ikNumber,
                bbcShift: entry.needsShift,
                bbcCtrl: false
            )
        }

        return nil
    }

    /// Create a built-in mapping programmatically
    init(
        id: UUID = UUID(),
        name: String,
        isBuiltIn: Bool = false,
        characterMapping: Bool = true,
        synchronizeCapsLock: Bool = true,
        keyMappings: [UInt16: BBCKeyRef] = [:]
    ) {
        self.id = id
        self.name = name
        self.isBuiltIn = isBuiltIn
        self.characterMapping = characterMapping
        self.synchronizeCapsLock = synchronizeCapsLock
        self.keyMappings = keyMappings

        // Build allEntries from keyMappings
        self.allEntries = keyMappings.map { keyCode, ref in
            KeyMappingEntry(
                macOSKeyCode: keyCode,
                bbcKeyName: ref.bbcKeyName,
                bbcWithShift: ref.bbcWithShift,
                bbcWithCtrl: ref.bbcWithCtrl
            )
        }
    }

    /// Load from JSON dictionary, extracting macOS entries while preserving all for round-trip
    /// - Parameters:
    ///   - json: The JSON dictionary
    ///   - isBuiltIn: Whether this is a built-in mapping (default: false for user mappings)
    init(json: [String: Any], isBuiltIn: Bool = false) {
        // Extract known fields
        if let idString = json["id"] as? String,
           let uuid = UUID(uuidString: idString) {
            self.id = uuid
        } else {
            self.id = UUID()
        }

        self.name = json["name"] as? String ?? "Unnamed"
        self.isBuiltIn = isBuiltIn
        self.characterMapping = json["characterMapping"] as? Bool ?? true
        self.synchronizeCapsLock = json["synchronizeCapsLock"] as? Bool ?? true

        // Parse all entries, preserving raw JSON
        let entriesJSON = json["keyMappings"] as? [[String: Any]] ?? []
        self.allEntries = entriesJSON.map { KeyMappingEntry(json: $0) }

        // Build keyMappings from entries that have macOSKeyCode
        self.keyMappings = [:]
        for entry in allEntries {
            if let keyCode = entry.macOSKeyCode,
               let bbcName = entry.bbcKeyName {
                keyMappings[keyCode] = BBCKeyRef(
                    bbcKeyName: bbcName,
                    bbcWithShift: entry.bbcWithShift,
                    bbcWithCtrl: entry.bbcWithCtrl
                )
            }
        }
    }

    /// Serialize to JSON dictionary, preserving all platform entries
    func toJSON() -> [String: Any] {
        return [
            "version": 1,
            "id": id.uuidString,
            "name": name,
            "characterMapping": characterMapping,
            "synchronizeCapsLock": synchronizeCapsLock,
            "keyMappings": allEntries.map { $0.rawJSON }
        ]
    }

    /// Clone this mapping with a new name
    func clone(newName: String) -> KeyboardMapping {
        let cloned = KeyboardMapping(json: toJSON())
        // Generate new ID for the clone
        return KeyboardMapping(
            id: UUID(),
            name: newName,
            isBuiltIn: false,
            characterMapping: cloned.characterMapping,
            synchronizeCapsLock: cloned.synchronizeCapsLock,
            keyMappings: cloned.keyMappings
        )
    }

    /// Add or update a key mapping
    func setKeyMapping(keyCode: UInt16, bbcKeyName: String, bbcWithShift: Bool? = nil, bbcWithCtrl: Bool? = nil) {
        guard !isBuiltIn else { return }

        let ref = BBCKeyRef(bbcKeyName: bbcKeyName, bbcWithShift: bbcWithShift, bbcWithCtrl: bbcWithCtrl)
        keyMappings[keyCode] = ref

        // Update allEntries
        let entry = KeyMappingEntry(macOSKeyCode: keyCode, bbcKeyName: bbcKeyName, bbcWithShift: bbcWithShift, bbcWithCtrl: bbcWithCtrl)

        // Find and update existing entry, or append
        if let index = allEntries.firstIndex(where: { $0.macOSKeyCode == keyCode }) {
            allEntries[index] = entry
        } else {
            allEntries.append(entry)
        }
    }

    /// Remove a key mapping
    func removeKeyMapping(keyCode: UInt16) {
        guard !isBuiltIn else { return }

        keyMappings.removeValue(forKey: keyCode)
        allEntries.removeAll { $0.macOSKeyCode == keyCode }
    }
}

// MARK: - Built-in Mapping Loading

extension KeyboardMapping {

    /// Load the default logical mapping from bundle resources
    static func createDefaultLogical() -> KeyboardMapping {
        guard let url = Bundle.main.url(forResource: "DefaultLogical", withExtension: "json") else {
            fatalError("DefaultLogical.json not found in bundle")
        }

        do {
            let data = try Data(contentsOf: url)
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                fatalError("DefaultLogical.json is not valid JSON")
            }
            return KeyboardMapping(json: json, isBuiltIn: true)
        } catch {
            fatalError("Failed to load DefaultLogical.json: \(error)")
        }
    }
}
