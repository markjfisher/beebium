// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Parsed sideways ROM header, decoded from the `describe-rom` CLI output.
///
/// Lets the UI show a ROM's real title and version (e.g. "Acorn DFS 2.26")
/// rather than its image filename.
struct RomHeaderInfo: Codable {
    /// True if this is a recognised sideways ROM (a valid "(C)" copyright marker).
    let recognised: Bool
    let title: String
    let version: String
    let copyright: String
    let isLanguage: Bool
    let isServiceOnly: Bool

    enum CodingKeys: String, CodingKey {
        case recognised, title, version, copyright
        case isLanguage = "is_language"
        case isServiceOnly = "is_service_only"
    }
}
