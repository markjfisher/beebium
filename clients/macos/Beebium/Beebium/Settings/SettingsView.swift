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

enum SettingsPane: String, CaseIterable, Identifiable {
    case general
    case machines
    case keyboard
    case video

    var id: String { rawValue }

    var label: String {
        switch self {
        case .general: return "General"
        case .machines: return "Machines"
        case .keyboard: return "Keyboard"
        case .video: return "Video"
        }
    }

    var systemImage: String {
        switch self {
        case .general: return "gearshape"
        case .machines: return "desktopcomputer"
        case .keyboard: return "keyboard"
        case .video: return "display"
        }
    }
}

struct SettingsView: View {
    var body: some View {
        TabView {
            GeneralSettingsPane()
                .tabItem {
                    Label("General", systemImage: "gearshape")
                }

            MachinesSettingsPane()
                .tabItem {
                    Label("Machines", systemImage: "desktopcomputer")
                }

            KeyboardSettingsPane()
                .tabItem {
                    Label("Keyboard", systemImage: "keyboard")
                }

            VideoSettingsPane()
                .tabItem {
                    Label("Video", systemImage: "display")
                }
        }
        .frame(minWidth: 500, minHeight: 350)
    }
}
