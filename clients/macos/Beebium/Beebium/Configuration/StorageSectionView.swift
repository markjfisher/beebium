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

/// Storage configuration section view.
///
/// Shows FDC socket picker (for Model B only) and floppy drive configuration.
struct StorageSectionView: View {
    /// Storage configuration state being edited
    @ObservedObject var storageConfig: StorageConfigurationState

    /// Schema describing available options (nil if not yet loaded)
    let schema: StorageSchemaSection?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                // FDC Socket picker (only if schema has fdc_socket)
                if let fdcSchema = schema?.fdcSocket {
                    fdcSocketSection(schema: fdcSchema)
                }

                // Floppy drives
                floppyDrivesSection
            }
            .padding(12)
        }
    }

    // MARK: - FDC Socket Section

    @ViewBuilder
    private func fdcSocketSection(schema: FDCSocketSchema) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Disc Controller")
                .font(.headline)

            Picker("", selection: $storageConfig.fdcSocketId) {
                ForEach(schema.options) { option in
                    Text(option.label).tag(option.id)
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()

            // Show description for selected option
            if let selectedOption = schema.options.first(where: { $0.id == storageConfig.fdcSocketId }),
               let description = selectedOption.description {
                Text(description)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }

        Divider()
    }

    // MARK: - Floppy Drives Section

    private var floppyDrivesSection: some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(storageConfig.drives.indices, id: \.self) { index in
                FloppyDriveConfigView(
                    driveNumber: storageConfig.drives[index].id,
                    imageFilepath: Binding(
                        get: { storageConfig.drives[index].imageFilepath },
                        set: { storageConfig.drives[index].imageFilepath = $0 }
                    )
                )

                if index < storageConfig.drives.count - 1 {
                    Divider()
                        .padding(.horizontal, 12)
                }
            }
        }
    }
}
