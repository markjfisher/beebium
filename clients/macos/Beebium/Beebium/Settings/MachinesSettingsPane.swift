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

struct MachinesSettingsPane: View {
    @StateObject private var presetManager = PresetManager.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            if presetManager.isDiscovering {
                ProgressView("Discovering machines...")
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if presetManager.systemPresets.isEmpty {
                emptyStateView
            } else {
                presetListView
            }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .task {
            await presetManager.discoverPresets()
        }
    }

    private var emptyStateView: some View {
        VStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("No Machine Cores Found")
                .font(.headline)
            if let error = presetManager.discoveryError {
                Text(error)
                    .foregroundColor(.secondary)
                    .font(.caption)
            } else {
                Text("Reinstall Beebium to restore default cores.")
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var presetListView: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Built-in Machine Presets")
                .font(.headline)

            ForEach(presetManager.systemPresets) { preset in
                PresetRow(preset: preset)
            }

            Divider()

            Text("My Machine Presets")
                .font(.headline)

            Text("No custom presets yet")
                .foregroundColor(.secondary)
                .font(.caption)

            Spacer()
        }
    }
}

struct PresetRow: View {
    let preset: MachinePreset

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "desktopcomputer")
                .font(.title2)
                .foregroundColor(.accentColor)

            VStack(alignment: .leading, spacing: 2) {
                Text(preset.name)
                    .font(.body)
                if let description = preset.modelDescription {
                    Text(description)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Spacer()
        }
        .padding(.vertical, 4)
    }
}
