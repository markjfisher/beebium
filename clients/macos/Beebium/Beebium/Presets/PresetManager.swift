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
import AppKit

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
    @Published private(set) var userPresets: [MachinePreset] = []
    @Published private(set) var isDiscovering = false
    @Published private(set) var discoveryError: String?

    /// Cached user presets directory path (retrieved from CLI)
    private var cachedUserPresetsDirpath: String?

    private init() {}

    /// Discover preset files and build system and user presets.
    func discoverPresets() async {
        isDiscovering = true
        discoveryError = nil
        systemPresets = []
        userPresets = []

        let dirpath = serversDirpath()
        let systemPresetsDirpath = "\(dirpath)/presets"
        var discoveredSystem: [MachinePreset] = []
        var discoveredUser: [MachinePreset] = []
        var configFailures: [String] = []

        NSLog("[PresetManager] Searching for system presets in: \(systemPresetsDirpath)")

        // Discover system presets
        let (systemFilepaths, directoryError) = findPresetFiles(in: systemPresetsDirpath)
        NSLog("[PresetManager] Found \(systemFilepaths.count) system preset file(s)")

        for presetFilepath in systemFilepaths {
            if let preset = await loadPreset(from: presetFilepath, source: .systemPreset, serversDirpath: dirpath, configFailures: &configFailures) {
                discoveredSystem.append(preset)
            }
        }

        // Discover user presets (need an executable to query the user presets directory)
        if let firstSystemPreset = discoveredSystem.first,
           let userDirpath = await fetchUserPresetsDirpath(using: firstSystemPreset.coreExecutablePath) {
            NSLog("[PresetManager] Searching for user presets in: \(userDirpath)")
            let (userFilepaths, _) = findPresetFiles(in: userDirpath)
            NSLog("[PresetManager] Found \(userFilepaths.count) user preset file(s)")

            for presetFilepath in userFilepaths {
                if let preset = await loadPreset(from: presetFilepath, source: .userPreset, serversDirpath: dirpath, configFailures: &configFailures) {
                    discoveredUser.append(preset)
                }
            }
        }

        systemPresets = sortPresets(discoveredSystem)
        userPresets = sortPresets(discoveredUser)
        isDiscovering = false

        if let directoryError = directoryError {
            discoveryError = directoryError
        } else if systemFilepaths.isEmpty {
            discoveryError = "No preset files found in \(systemPresetsDirpath)"
        } else if discoveredSystem.isEmpty {
            let failedList = configFailures.joined(separator: ", ")
            discoveryError = "Found \(systemFilepaths.count) preset(s) but failed to load: \(failedList)"
        }
    }

    /// Load a single preset from a file path.
    private func loadPreset(from presetFilepath: String, source: MachinePreset.Source, serversDirpath: String, configFailures: inout [String]) async -> MachinePreset? {
        let filename = URL(fileURLWithPath: presetFilepath).lastPathComponent
        let presetId = presetIdFromFilename(filename)
        NSLog("[PresetManager] Processing preset: \(filename) (id: \(presetId))")

        guard let presetData = parsePresetFile(at: presetFilepath) else {
            configFailures.append(filename)
            NSLog("[PresetManager] Failed to parse preset file: \(filename)")
            return nil
        }

        let executablePath = "\(serversDirpath)/beebium-\(presetData.model)"
        guard FileManager.default.isExecutableFile(atPath: executablePath) else {
            NSLog("[PresetManager] Executable not found for model '\(presetData.model)': \(executablePath)")
            return nil
        }

        let (schema, error) = await fetchPresetSchema(from: executablePath)
        guard let schema = schema else {
            configFailures.append(filename)
            NSLog("[PresetManager] Failed to get schema from \(presetData.model): \(error ?? "unknown error")")
            return nil
        }

        let preset = MachinePreset(
            id: UUID(),
            presetId: presetId,
            name: presetData.name ?? schema.model.name,
            coreExecutablePath: executablePath,
            presetFilepath: presetFilepath,
            source: source,
            modelName: schema.model.name,
            modelDescription: presetData.description ?? schema.model.description,
            releaseDate: presetData.releaseDate,
            configuration: [:]
        )
        NSLog("[PresetManager] Discovered: \(preset.name)")
        return preset
    }

    /// Extract preset ID from filename (e.g., "bbc-model-b.preset.beebium" -> "bbc-model-b")
    private func presetIdFromFilename(_ filename: String) -> String {
        let suffix = ".preset.beebium"
        if filename.hasSuffix(suffix) {
            return String(filename.dropLast(suffix.count))
        }
        return filename
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

    // MARK: - User Presets Directory

    /// Get the user presets directory path by querying the CLI.
    func userPresetsDirpath() async -> String? {
        if let cached = cachedUserPresetsDirpath {
            return cached
        }

        // Need at least one system preset to determine executable
        guard let firstPreset = systemPresets.first else {
            NSLog("[PresetManager] No system presets available to query user presets directory")
            return nil
        }

        return await fetchUserPresetsDirpath(using: firstPreset.coreExecutablePath)
    }

    /// Fetch the user presets directory path using a specific executable.
    private func fetchUserPresetsDirpath(using executablePath: String) async -> String? {
        if let cached = cachedUserPresetsDirpath {
            return cached
        }

        let (output, error) = await runCli(executable: executablePath, arguments: ["report-presets-dirpath"])
        if let error = error {
            NSLog("[PresetManager] Failed to get user presets directory: \(error)")
            return nil
        }

        let dirpath = output.trimmingCharacters(in: .whitespacesAndNewlines)
        cachedUserPresetsDirpath = dirpath
        return dirpath
    }

    /// Get the system presets directory path.
    func systemPresetsDirpath() -> String {
        return "\(serversDirpath())/presets"
    }

    // MARK: - Preset Management

    /// Duplicate a preset with a new name.
    /// - Parameters:
    ///   - preset: The source preset to duplicate
    ///   - newName: The name for the new preset
    /// - Returns: The ID of the created preset, or nil on failure
    func duplicatePreset(_ preset: MachinePreset, newName: String) async -> String? {
        let arguments = ["create-preset", "--name", newName, "--from", preset.presetId]
        let (output, error) = await runCli(executable: preset.coreExecutablePath, arguments: arguments)
        if let error = error {
            NSLog("[PresetManager] Failed to duplicate preset: \(error)")
            return nil
        }

        let newPresetId = output.trimmingCharacters(in: .whitespacesAndNewlines)
        NSLog("[PresetManager] Created preset: \(newPresetId)")

        // Reload presets to pick up the new one
        await discoverPresets()

        return newPresetId
    }

    /// Delete a user preset.
    /// - Parameter preset: The preset to delete (must be a user preset)
    /// - Returns: true if deletion succeeded
    func deletePreset(_ preset: MachinePreset) async -> Bool {
        guard preset.isEditable else {
            NSLog("[PresetManager] Cannot delete system preset: \(preset.presetId)")
            return false
        }

        let (_, error) = await runCli(executable: preset.coreExecutablePath, arguments: ["delete-preset", preset.presetId])
        if let error = error {
            NSLog("[PresetManager] Failed to delete preset: \(error)")
            return false
        }

        NSLog("[PresetManager] Deleted preset: \(preset.presetId)")

        // Reload presets
        await discoverPresets()

        return true
    }

    /// Export a preset to a file.
    /// - Parameters:
    ///   - preset: The preset to export
    ///   - filepath: The destination file path
    /// - Returns: true if export succeeded
    func exportPreset(_ preset: MachinePreset, to filepath: String) async -> Bool {
        let (_, error) = await runCli(executable: preset.coreExecutablePath, arguments: ["export-preset", preset.presetId, "--output", filepath])
        if let error = error {
            NSLog("[PresetManager] Failed to export preset: \(error)")
            return false
        }

        NSLog("[PresetManager] Exported preset to: \(filepath)")
        return true
    }

    /// Import a preset from a file.
    /// - Parameters:
    ///   - filepath: The source file path
    ///   - executablePath: The executable to use for import (determines model)
    /// - Returns: The ID of the imported preset, or nil on failure
    func importPreset(from filepath: String, using executablePath: String) async -> String? {
        let (output, error) = await runCli(executable: executablePath, arguments: ["import-preset", filepath])
        if let error = error {
            NSLog("[PresetManager] Failed to import preset: \(error)")
            return nil
        }

        let newPresetId = output.trimmingCharacters(in: .whitespacesAndNewlines)
        NSLog("[PresetManager] Imported preset: \(newPresetId)")

        // Reload presets
        await discoverPresets()

        return newPresetId
    }

    /// Reveal a preset's directory in Finder.
    /// - Parameter preset: The preset to reveal
    func revealInFinder(_ preset: MachinePreset) {
        let dirpath = URL(fileURLWithPath: preset.presetFilepath).deletingLastPathComponent().path
        NSWorkspace.shared.selectFile(preset.presetFilepath, inFileViewerRootedAtPath: dirpath)
    }

    // MARK: - CLI Execution

    /// Run a CLI command and return (stdout, errorMessage).
    private func runCli(executable: String, arguments: [String]) async -> (String, String?) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
            process.waitUntilExit()

            let stdoutData = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
            let stdout = String(data: stdoutData, encoding: .utf8) ?? ""

            guard process.terminationStatus == 0 else {
                let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
                let stderrText = String(data: stderrData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                let errorMsg = stderrText.isEmpty
                    ? "exit status \(process.terminationStatus)"
                    : stderrText
                return (stdout, errorMsg)
            }

            return (stdout, nil)
        } catch {
            return ("", error.localizedDescription)
        }
    }
}
