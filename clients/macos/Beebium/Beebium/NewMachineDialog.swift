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

import SwiftUI

struct NewMachineDialog: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow

    @StateObject private var presetManager = PresetManager.shared
    @ObservedObject private var windowState = ConnectWindowState.shared

    // Preset selection - persisted via @AppStorage
    @AppStorage("lastSelectedPresetId") private var lastSelectedPresetId: String = ""
    @State private var selectedPreset: MachinePreset?

    // Configuration state
    @State private var showConfiguration = false
    @StateObject private var storageConfig = StorageConfigurationState()
    @State private var storageSchema: StorageSchemaSection?
    @StateObject private var memoryConfig = MemoryConfigurationState()
    @State private var memorySchema: SidewaysSchemaSection?
    @State private var isLoadingSchema = false

    // Save as preset
    @State private var saveAsNewPreset = false
    @State private var newPresetName = ""

    // Launch state
    @State private var isLaunching = false
    @State private var launchError: String?

    private var dialogWidth: CGFloat {
        showConfiguration ? 520 : 380
    }

    var body: some View {
        VStack(spacing: 0) {
            // Content
            VStack(alignment: .leading, spacing: 16) {
                presetPickerSection
                descriptionSection
                configurationSection
                if showConfiguration {
                    saveAsPresetSection
                }
                errorSection
            }
            .padding(20)

            Divider()

            // Buttons
            buttonBar
        }
        .frame(width: dialogWidth)
        .animation(.easeInOut(duration: 0.2), value: showConfiguration)
        .task {
            if presetManager.systemPresets.isEmpty {
                await presetManager.discoverPresets()
            }
            restoreLastSelection()
        }
        .onChange(of: selectedPreset) { newPreset in
            if let preset = newPreset {
                loadSchemaForPreset(preset)
            }
        }
    }

    // MARK: - Preset Picker

    private var presetPickerSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Machine Preset")
                .font(.subheadline)
                .foregroundColor(.secondary)

            Picker("", selection: $selectedPreset) {
                if !presetManager.systemPresets.isEmpty {
                    Section("Built-in") {
                        ForEach(presetManager.systemPresets) { preset in
                            Text(preset.name).tag(Optional(preset))
                        }
                    }
                }
                if !presetManager.userPresets.isEmpty {
                    Section("My Presets") {
                        ForEach(presetManager.userPresets) { preset in
                            Text(preset.name).tag(Optional(preset))
                        }
                    }
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .disabled(presetManager.isDiscovering || isLaunching)
            .onChange(of: selectedPreset) { newValue in
                if let preset = newValue {
                    lastSelectedPresetId = preset.presetId
                }
            }
        }
    }

    // MARK: - Description

    private var descriptionSection: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let preset = selectedPreset, let description = preset.modelDescription {
                Text(description)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else if presetManager.isDiscovering {
                HStack(spacing: 8) {
                    ProgressView()
                        .scaleEffect(0.7)
                    Text("Discovering presets...")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            } else if presetManager.systemPresets.isEmpty {
                Text("No presets available")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .italic()
            } else {
                Text("Select a preset to see its description")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .italic()
            }
        }
        .frame(height: 40, alignment: .top)
    }

    // MARK: - Configuration Section

    private var configurationSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Disclosure button
            Button {
                withAnimation {
                    showConfiguration.toggle()
                }
            } label: {
                HStack(spacing: 4) {
                    Image(systemName: showConfiguration ? "chevron.down" : "chevron.right")
                        .font(.caption)
                        .frame(width: 12)
                    Text("Configuration")
                        .font(.subheadline)
                }
                .foregroundColor(.primary)
            }
            .buttonStyle(.plain)
            .disabled(selectedPreset == nil)

            // Configuration editor (shown when expanded)
            if showConfiguration {
                if isLoadingSchema {
                    HStack {
                        ProgressView()
                            .scaleEffect(0.7)
                        Text("Loading configuration...")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .frame(height: 200)
                } else {
                    ConfigurationEditor(
                        storageConfig: storageConfig,
                        storageSchema: storageSchema,
                        memoryConfig: memoryConfig,
                        memorySchema: memorySchema,
                        modelId: selectedPreset?.modelName ?? "model-b"
                    )
                    // Grow with the window (resizable) so the sideways list can
                    // show more rows; 220 is the floor.
                    .frame(minHeight: 220, maxHeight: .infinity)
                }
            }
        }
    }

    // MARK: - Save as Preset Section

    private var saveAsPresetSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("Save as new preset", isOn: $saveAsNewPreset)
                .toggleStyle(.checkbox)

            if saveAsNewPreset {
                TextField("Preset name", text: $newPresetName)
                    .textFieldStyle(.roundedBorder)
            }
        }
    }

    // MARK: - Error Section

    @ViewBuilder
    private var errorSection: some View {
        if let error = launchError {
            HStack(spacing: 8) {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundColor(.yellow)
                Text(error)
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
            }
            .padding(8)
            .background(Color.yellow.opacity(0.1))
            .cornerRadius(6)
        }
    }

    // MARK: - Button Bar

    private var buttonBar: some View {
        HStack {
            Spacer()

            Button("Cancel") {
                dismiss()
            }
            .keyboardShortcut(.cancelAction)
            .disabled(isLaunching)

            Button(isLaunching ? "Creating..." : "Create") {
                Task {
                    await createMachine()
                }
            }
            .keyboardShortcut(.defaultAction)
            .disabled(selectedPreset == nil || isLaunching || presetManager.isDiscovering ||
                     (saveAsNewPreset && newPresetName.trimmingCharacters(in: .whitespaces).isEmpty))
        }
        .padding(16)
    }

    // MARK: - Actions

    private func restoreLastSelection() {
        let allPresets = presetManager.systemPresets + presetManager.userPresets

        // Try to restore last selection
        if !lastSelectedPresetId.isEmpty,
           let preset = allPresets.first(where: { $0.presetId == lastSelectedPresetId }) {
            selectedPreset = preset
        } else if let first = presetManager.systemPresets.first {
            // Default to first system preset
            selectedPreset = first
        }
    }

    private func loadSchemaForPreset(_ preset: MachinePreset) {
        isLoadingSchema = true
        storageSchema = nil
        memorySchema = nil

        // Reset storage config to defaults
        storageConfig.fdcSocketId = "none"
        storageConfig.clearAllDrives()

        Task {
            let manager = presetManager
            let executablePath = preset.coreExecutablePath
            let schema = await manager.fetchStorageSchema(for: preset)
            let sidewaysSchema = await manager.fetchSidewaysSchema(for: preset)
            let presetSlots = manager.sidewaysSlots(for: preset)

            await MainActor.run {
                storageSchema = schema

                // Update drive count based on schema
                let driveCount = schema?.floppyDrives?.count ?? 2
                storageConfig.drives = (0..<driveCount).map {
                    StorageConfigurationState.DriveConfig(id: $0, imageFilepath: nil)
                }

                // Build the Memory tab from the machine's sockets and the
                // preset's own sideways assignments.
                memorySchema = sidewaysSchema
                if let sidewaysSchema = sidewaysSchema {
                    memoryConfig.configure(schema: sidewaysSchema, presetSlots: presetSlots)
                    memoryConfig.headerResolver = { @MainActor image in
                        await manager.fetchRomHeader(image: image, executablePath: executablePath)
                    }
                } else {
                    memoryConfig.sockets = []
                    memoryConfig.headerResolver = nil
                }

                isLoadingSchema = false
            }

            // Resolve ROM titles (one describe-rom per loaded image) so the
            // Memory tab shows real ROM names rather than image filenames.
            await memoryConfig.resolveTitles()
        }
    }

    private func createMachine() async {
        guard let preset = selectedPreset else { return }

        isLaunching = true
        launchError = nil

        // TODO: If saveAsNewPreset, create the preset first (shared follow-up
        // with the Storage tab). For now, launch with the selected preset plus
        // the storage and memory configuration applied via CLI overrides.

        // Capture the manager reference before async call to avoid @StateObject wrapper issues
        let manager = presetManager
        let result = await manager.launchCore(preset, storageConfig: storageConfig, memoryConfig: memoryConfig)

        switch result {
        case .success(let core):
            MachineManager.shared.register(
                process: core.process,
                port: core.port,
                provenanceUUID: core.provenanceUUID
            )
            let target = ConnectionTarget(host: "127.0.0.1", port: core.port)
            windowState.pendingTarget = target
            windowState.pendingNeedsRun = true
            windowState.pendingProvenanceUUID = core.provenanceUUID
            openWindow(id: "main")
            dismiss()

        case .failure(let error):
            launchError = error.localizedDescription
            isLaunching = false
        }
    }
}
