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

/// Sidebar mode for selecting different configuration panels
enum SidebarMode: Int, CaseIterable, Identifiable {
    case storage = 1
    case memory = 2
    case peripherals = 3
    case video = 4
    case sound = 5
    case keyboard = 6
    case coprocessor = 7
    case network = 8

    var id: Int { rawValue }

    /// SF Symbol name for the mode icon
    var icon: String {
        switch self {
        case .storage: return "externaldrive"
        case .memory: return "memorychip"
        case .peripherals: return "puzzlepiece.extension"
        case .video: return "display"
        case .sound: return "speaker.wave.2"
        case .keyboard: return "keyboard"
        case .coprocessor: return "cpu"
        case .network: return "network"
        }
    }

    /// Human-readable label for tooltips and accessibility
    var label: String {
        switch self {
        case .storage: return "Storage"
        case .memory: return "Memory"
        case .peripherals: return "Peripherals"
        case .video: return "Video"
        case .sound: return "Sound"
        case .keyboard: return "Keyboard"
        case .coprocessor: return "Coprocessor"
        case .network: return "Network"
        }
    }

    /// Keyboard shortcut key (used with Command modifier)
    var shortcutKey: KeyEquivalent {
        KeyEquivalent(Character(String(rawValue)))
    }
}

// MARK: - AppStorage Support

extension SidebarMode: RawRepresentable {
    // Already RawRepresentable via Int, but we need explicit conformance
    // for @AppStorage to work properly
}

// MARK: - FocusedValue Support

struct SidebarModeFocusedValueKey: FocusedValueKey {
    typealias Value = Binding<SidebarMode>
}

struct ShowSidebarFocusedValueKey: FocusedValueKey {
    typealias Value = Binding<Bool>
}

struct ShowStatusBarFocusedValueKey: FocusedValueKey {
    typealias Value = Binding<Bool>
}

extension FocusedValues {
    var sidebarMode: Binding<SidebarMode>? {
        get { self[SidebarModeFocusedValueKey.self] }
        set { self[SidebarModeFocusedValueKey.self] = newValue }
    }

    var showSidebar: Binding<Bool>? {
        get { self[ShowSidebarFocusedValueKey.self] }
        set { self[ShowSidebarFocusedValueKey.self] = newValue }
    }

    var showStatusBar: Binding<Bool>? {
        get { self[ShowStatusBarFocusedValueKey.self] }
        set { self[ShowStatusBarFocusedValueKey.self] = newValue }
    }
}
