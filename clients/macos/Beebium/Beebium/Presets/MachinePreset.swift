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

/// A machine preset representing a configuration that can be used to launch an emulator.
///
/// Default presets are auto-discovered from core executables and are immutable.
/// User presets are copies that can be edited and saved.
struct MachinePreset: Identifiable, Hashable {
    let id: UUID
    var name: String
    let coreExecutablePath: String
    let isDefault: Bool
    let modelName: String
    let modelDescription: String?
    var configuration: [String: String]

    static func == (lhs: MachinePreset, rhs: MachinePreset) -> Bool {
        lhs.id == rhs.id
    }

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }
}
