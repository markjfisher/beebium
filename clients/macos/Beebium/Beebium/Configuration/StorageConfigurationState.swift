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

/// Observable state model for storage configuration editing.
///
/// This holds the mutable state for the storage section of the configuration editor,
/// including FDC selection and floppy drive image paths.
class StorageConfigurationState: ObservableObject {
    /// Selected FDC socket ID (e.g., "none", "acorn-1770", "opus-8272")
    /// Only applicable for Model B which has an FDC socket.
    @Published var fdcSocketId: String = "none"

    /// Configured floppy drives with optional image paths
    @Published var drives: [DriveConfig] = []

    /// Configuration for a single floppy drive
    struct DriveConfig: Identifiable {
        /// Drive number (0, 1, 2, or 3)
        let id: Int
        /// Path to the disc image file, or nil if empty
        var imageFilepath: String?

        /// Filename extracted from the image path, for display
        var imageFilename: String? {
            guard let filepath = imageFilepath else { return nil }
            return URL(fileURLWithPath: filepath).lastPathComponent
        }
    }

    /// Initialize with a specified number of drives (all empty)
    init(driveCount: Int = 2) {
        drives = (0..<driveCount).map { DriveConfig(id: $0, imageFilepath: nil) }
    }

    /// Check if any configuration has been changed from defaults
    var hasChanges: Bool {
        if fdcSocketId != "none" { return true }
        return drives.contains { $0.imageFilepath != nil }
    }

    /// Clear all drive images
    func clearAllDrives() {
        for i in drives.indices {
            drives[i].imageFilepath = nil
        }
    }

    /// Set the image path for a specific drive
    func setDriveImage(drive: Int, filepath: String?) {
        if let index = drives.firstIndex(where: { $0.id == drive }) {
            drives[index].imageFilepath = filepath
        }
    }
}
