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

/// Schema describing a machine's configurable options, returned by `describe-configuration` CLI.
struct ConfigurationSchema: Codable {
    let modelName: String
    let modelDescription: String?
    let options: [ConfigOption]

    enum CodingKeys: String, CodingKey {
        case modelName = "model_name"
        case modelDescription = "model_description"
        case options
    }
}

/// A single configurable option within a configuration schema.
struct ConfigOption: Codable, Identifiable {
    let key: String
    let label: String
    let description: String?
    let type: ConfigType
    let defaultValue: String
    let choices: [String]?
    let required: Bool
    let fileFilter: String?

    var id: String { key }

    enum CodingKeys: String, CodingKey {
        case key, label, description, type
        case defaultValue = "default_value"
        case choices, required
        case fileFilter = "file_filter"
    }
}

/// Type of a configuration option, determining the UI to display.
enum ConfigType: String, Codable {
    case string
    case integer
    case boolean
    case filePath = "file_path"
    case choice
}
