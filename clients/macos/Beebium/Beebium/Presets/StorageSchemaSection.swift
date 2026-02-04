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

/// Storage section schema from `describe-preset-schema` CLI command.
///
/// Describes the storage capabilities of a machine model including
/// FDC socket options (Model B) and floppy drive configuration.
struct StorageSchemaSection: Codable {
    let type: String  // "storage"
    let fdcSocket: FDCSocketSchema?
    let floppyDrives: [FloppyDriveSchema]?

    enum CodingKeys: String, CodingKey {
        case type
        case fdcSocket = "fdc_socket"
        case floppyDrives = "floppy_drives"
    }
}

/// Schema for the FDC socket (Model B only).
///
/// The FDC socket allows different disc controllers to be installed.
struct FDCSocketSchema: Codable {
    let options: [FDCOption]
}

/// An available FDC option that can be installed in the FDC socket.
struct FDCOption: Codable, Identifiable, Hashable {
    let id: String
    let label: String
    let device: String?
    let description: String?
}

/// Schema for a floppy drive slot.
struct FloppyDriveSchema: Codable {
    /// Drive number (0, 1, 2, or 3)
    let drive: Int
    /// Supported track counts (e.g., [40, 80])
    let tracks: [Int]
    /// Number of sides/heads (1 or 2)
    let sides: Int
    /// Supported image file types (e.g., ["ssd", "dsd", "adf"])
    let imageTypes: [String]

    enum CodingKeys: String, CodingKey {
        case drive, tracks, sides
        case imageTypes = "image_types"
    }
}
