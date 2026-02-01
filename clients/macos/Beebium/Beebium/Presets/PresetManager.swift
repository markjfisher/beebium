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

/// Manages machine presets by discovering preset files and querying their configurations.
///
/// Discovers presets via:
/// 1. `BEEBIUM_SERVERS_DIRPATH` environment variable (if set)
/// 2. Fallback to `~/Code/beebium/build/src/server/` for development
///
/// Preset files are located in the `presets/` subdirectory with `.preset.beebium` extension.
/// Each preset declares a `model` field that maps to an executable (beebium-{model}).
@MainActor
class PresetManager: ObservableObject {
    static let shared = PresetManager()

    @Published private(set) var systemPresets: [MachinePreset] = []
    @Published private(set) var isDiscovering = false
    @Published private(set) var discoveryError: String?

    private init() {}

    /// Discover preset files and build system presets.
    func discoverPresets() async {
        isDiscovering = true
        discoveryError = nil
        systemPresets = []

        let dirpath = serversDirpath()
        let presetsDirpath = "\(dirpath)/presets"
        var discovered: [MachinePreset] = []
        var configFailures: [String] = []

        NSLog("[PresetManager] Searching for presets in: \(presetsDirpath)")

        let (presetFilepaths, directoryError) = findPresetFiles(in: presetsDirpath)
        NSLog("[PresetManager] Found \(presetFilepaths.count) preset file(s)")

        for presetFilepath in presetFilepaths {
            let filename = URL(fileURLWithPath: presetFilepath).lastPathComponent
            NSLog("[PresetManager] Processing preset: \(filename)")

            guard let presetData = parsePresetFile(at: presetFilepath) else {
                configFailures.append(filename)
                NSLog("[PresetManager] Failed to parse preset file: \(filename)")
                continue
            }

            let executablePath = "\(dirpath)/beebium-\(presetData.model)"
            guard FileManager.default.isExecutableFile(atPath: executablePath) else {
                NSLog("[PresetManager] Executable not found for model '\(presetData.model)': \(executablePath)")
                continue
            }

            let (schema, error) = await fetchPresetSchema(from: executablePath)
            guard let schema = schema else {
                configFailures.append(filename)
                NSLog("[PresetManager] Failed to get schema from \(presetData.model): \(error ?? "unknown error")")
                continue
            }

            let preset = MachinePreset(
                id: UUID(),
                name: presetData.name ?? schema.model.name,
                coreExecutablePath: executablePath,
                source: .systemPreset,
                modelName: schema.model.name,
                modelDescription: presetData.description ?? schema.model.description,
                releaseDate: presetData.releaseDate,
                configuration: [:]
            )
            discovered.append(preset)
            NSLog("[PresetManager] Discovered: \(preset.name)")
        }

        systemPresets = sortPresets(discovered)
        isDiscovering = false

        if let directoryError = directoryError {
            discoveryError = directoryError
        } else if presetFilepaths.isEmpty {
            discoveryError = "No preset files found in \(presetsDirpath)"
        } else if discovered.isEmpty {
            let failedList = configFailures.joined(separator: ", ")
            discoveryError = "Found \(presetFilepaths.count) preset(s) but failed to load: \(failedList)"
        }
    }

    /// Get the directory path where server executables are located.
    private func serversDirpath() -> String {
        if let envPath = ProcessInfo.processInfo.environment["BEEBIUM_SERVERS_DIRPATH"] {
            return envPath
        }

        let homeDir = FileManager.default.homeDirectoryForCurrentUser
        return homeDir.appendingPathComponent("Code/beebium/build/src/server").path
    }

    /// Find preset files in a directory (files with `.preset.beebium` extension).
    /// Returns (filepaths, errorMessage) - errorMessage is nil on success.
    private func findPresetFiles(in dirpath: String) -> ([String], String?) {
        let fm = FileManager.default

        var isDirectory: ObjCBool = false
        guard fm.fileExists(atPath: dirpath, isDirectory: &isDirectory), isDirectory.boolValue else {
            NSLog("[PresetManager] Presets directory does not exist: \(dirpath)")
            return ([], "Presets directory does not exist: \(dirpath)")
        }

        guard let contents = try? fm.contentsOfDirectory(atPath: dirpath) else {
            NSLog("[PresetManager] Cannot read presets directory: \(dirpath)")
            return ([], "Cannot read presets directory: \(dirpath)")
        }

        let presetFilepaths = contents
            .filter { $0.hasSuffix(".preset.beebium") }
            .map { "\(dirpath)/\($0)" }
            .sorted()

        return (presetFilepaths, nil)
    }

    /// Parse a preset file and extract its data.
    private func parsePresetFile(at filepath: String) -> PresetFileData? {
        guard let data = FileManager.default.contents(atPath: filepath) else {
            return nil
        }

        do {
            return try JSONDecoder().decode(PresetFileData.self, from: data)
        } catch {
            NSLog("[PresetManager] JSON decode error for \(filepath): \(error)")
            return nil
        }
    }

    /// Invoke `describe-preset-schema` on a server executable and parse the JSON response.
    /// Returns (schema, errorMessage) - errorMessage is nil on success.
    private func fetchPresetSchema(from executablePath: String) async -> (PresetSchema?, String?) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = ["describe-preset-schema"]

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
            process.waitUntilExit()

            guard process.terminationStatus == 0 else {
                let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
                let stderrText = String(data: stderrData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                let errorMsg = stderrText.isEmpty
                    ? "exit status \(process.terminationStatus)"
                    : stderrText
                NSLog("[PresetManager] Process exited with status \(process.terminationStatus): \(errorMsg)")
                return (nil, errorMsg)
            }

            let data = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
            let schema = try JSONDecoder().decode(PresetSchema.self, from: data)
            return (schema, nil)
        } catch {
            NSLog("[PresetManager] Failed to query \(executablePath): \(error)")
            return (nil, error.localizedDescription)
        }
    }

    /// Sort presets by release date (chronological), then by name (natural alphanumeric).
    private func sortPresets(_ presets: [MachinePreset]) -> [MachinePreset] {
        presets.sorted { lhs, rhs in
            let lhsDate = normalizedReleaseDate(lhs.releaseDate)
            let rhsDate = normalizedReleaseDate(rhs.releaseDate)

            if lhsDate != rhsDate {
                return lhsDate < rhsDate
            }

            return lhs.name.localizedStandardCompare(rhs.name) == .orderedAscending
        }
    }

    /// Normalize a release date string for sorting.
    /// Converts YYYY, YYYY-MM, YYYY-MM-DD to YYYY-MM-DD with missing parts as 00.
    private func normalizedReleaseDate(_ date: String?) -> String {
        guard let date = date else { return "9999-99-99" }

        let parts = date.split(separator: "-")
        let year = parts.count > 0 ? String(parts[0]) : "9999"
        let month = parts.count > 1 ? String(parts[1]) : "00"
        let day = parts.count > 2 ? String(parts[2]) : "00"

        return "\(year)-\(month)-\(day)"
    }
}
