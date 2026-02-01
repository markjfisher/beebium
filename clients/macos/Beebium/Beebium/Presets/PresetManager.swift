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

/// Manages machine presets by discovering core executables and querying their configurations.
///
/// Discovers server executables via:
/// 1. `BEEBIUM_SERVERS_DIRPATH` environment variable (if set)
/// 2. Fallback to `~/Code/beebium/build/src/server/` for development
///
/// Server executables are identified by the `beebium-` prefix.
@MainActor
class PresetManager: ObservableObject {
    static let shared = PresetManager()

    @Published private(set) var defaultPresets: [MachinePreset] = []
    @Published private(set) var isDiscovering = false
    @Published private(set) var discoveryError: String?

    private init() {}

    /// Discover core executables and build default presets.
    func discoverCores() async {
        isDiscovering = true
        discoveryError = nil
        defaultPresets = []

        let dirpath = serversDirpath()
        var discovered: [MachinePreset] = []
        var configFailures: [String] = []

        NSLog("[PresetManager] Searching for servers in: \(dirpath)")

        let (executables, directoryError) = findServerExecutables(in: dirpath)
        NSLog("[PresetManager] Found \(executables.count) executable(s)")

        for executablePath in executables {
            NSLog("[PresetManager] Querying: \(executablePath)")
            let (schema, error) = await fetchPresetSchema(from: executablePath)
            if let schema = schema {
                let preset = MachinePreset(
                    id: UUID(),
                    name: schema.model.name,
                    coreExecutablePath: executablePath,
                    isDefault: true,
                    modelName: schema.model.name,
                    modelDescription: schema.model.description,
                    configuration: [:]
                )
                discovered.append(preset)
                NSLog("[PresetManager] Discovered: \(schema.model.name)")
            } else {
                let filename = URL(fileURLWithPath: executablePath).lastPathComponent
                configFailures.append(filename)
                NSLog("[PresetManager] Failed to get config from \(filename): \(error ?? "unknown error")")
            }
        }

        defaultPresets = discovered
        isDiscovering = false

        // Set appropriate error message based on what went wrong
        if let directoryError = directoryError {
            discoveryError = directoryError
        } else if executables.isEmpty {
            discoveryError = "No server executables found in \(dirpath)"
        } else if discovered.isEmpty {
            let failedList = configFailures.joined(separator: ", ")
            discoveryError = "Found \(executables.count) executable(s) but failed to retrieve configuration from: \(failedList)"
        }
    }

    /// Get the directory path where server executables are located.
    private func serversDirpath() -> String {
        // Environment variable takes precedence
        if let envPath = ProcessInfo.processInfo.environment["BEEBIUM_SERVERS_DIRPATH"] {
            return envPath
        }

        // Development fallback (relative to home directory)
        let homeDir = FileManager.default.homeDirectoryForCurrentUser
        return homeDir.appendingPathComponent("Code/beebium/build/src/server").path
    }

    /// Find server executables in a directory (files with `beebium-` prefix).
    /// Returns (executables, errorMessage) - errorMessage is nil on success.
    private func findServerExecutables(in dirpath: String) -> ([String], String?) {
        let fm = FileManager.default

        // Check if directory exists
        var isDirectory: ObjCBool = false
        guard fm.fileExists(atPath: dirpath, isDirectory: &isDirectory), isDirectory.boolValue else {
            NSLog("[PresetManager] Directory does not exist: \(dirpath)")
            return ([], "Server directory does not exist: \(dirpath)")
        }

        guard let contents = try? fm.contentsOfDirectory(atPath: dirpath) else {
            NSLog("[PresetManager] Cannot read directory: \(dirpath)")
            return ([], "Cannot read server directory: \(dirpath)")
        }

        // Server executables have the "beebium-" prefix
        let executables = contents
            .filter { $0.hasPrefix("beebium-") }
            .map { "\(dirpath)/\($0)" }
            .filter { fm.isExecutableFile(atPath: $0) }
            .sorted()  // Consistent ordering

        return (executables, nil)
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
}
