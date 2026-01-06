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

/// Xcode-style icon toolbar for switching between sidebar modes
struct SidebarModeToolbar: View {
    @Binding var selectedMode: SidebarMode

    var body: some View {
        HStack(spacing: 2) {
            ForEach(SidebarMode.allCases) { mode in
                ModeButton(
                    mode: mode,
                    isSelected: selectedMode == mode,
                    action: { selectedMode = mode }
                )
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
    }
}

/// Individual mode button with icon, selection state, and tooltip
private struct ModeButton: View {
    let mode: SidebarMode
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: mode.icon)
                .font(.system(size: 14))
                .frame(width: 24, height: 24)
        }
        .buttonStyle(ModeButtonStyle(isSelected: isSelected))
        .help(mode.label)
    }
}

/// Custom button style for mode buttons with selection highlight
private struct ModeButtonStyle: ButtonStyle {
    let isSelected: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundColor(isSelected ? .accentColor : .secondary)
            .background(
                RoundedRectangle(cornerRadius: 4)
                    .fill(backgroundColor(for: configuration))
            )
    }

    private func backgroundColor(for configuration: Configuration) -> Color {
        if configuration.isPressed {
            return Color.accentColor.opacity(0.2)
        } else if isSelected {
            return Color.accentColor.opacity(0.15)
        } else {
            return Color.clear
        }
    }
}

#if DEBUG
struct SidebarModeToolbar_Previews: PreviewProvider {
    static var previews: some View {
        SidebarModeToolbar(selectedMode: .constant(.storage))
            .frame(width: 220)
            .background(Color(nsColor: .windowBackgroundColor))
    }
}
#endif
