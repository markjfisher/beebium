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

import AppKit
import SwiftUI

/// Status bar displayed at the bottom of the main window
struct StatusBarView: View {
    @ObservedObject var systemClient: SystemClient
    @ObservedObject var indicatorClient: IndicatorClient
    @ObservedObject var keyboardClient: KeyboardClient
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager

    /// Display order for indicators (keyboard LEDs first, then drives)
    private let indicatorOrder = [
        "caps-lock-led",
        "shift-lock-led",
        "floppy-0-activity-led",
        "floppy-1-activity-led"
    ]

    var body: some View {
        HStack(spacing: 12) {
            // Indicators on left
            if indicatorClient.isLoaded {
                ForEach(indicatorOrder, id: \.self) { name in
                    if let meta = indicatorClient.metadata[name] {
                        IndicatorView(
                            value: indicatorClient.values[name] ?? 0,
                            label: meta.label,
                            color: meta.color,
                            relatedKey: meta.relatedKey,
                            onTap: meta.relatedKey != nil ? { handleIndicatorTap(name: name, keyName: meta.relatedKey) } : nil
                        )
                    }
                }
            }

            Spacer()

            // Keyboard mapping name
            if let mappingName = keyboardMappingManager.activeMapping?.name {
                HStack(spacing: 3) {
                    Image(systemName: "keyboard")
                        .font(.system(size: 10))
                    Text(mappingName)
                        .font(.system(size: 11, design: .default))
                }
                .foregroundColor(.secondary)
            }

            // Machine name on right
            if systemClient.isLoaded {
                Text(systemClient.machineDisplayName)
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
            }
        }
        .frame(height: 22)
        .padding(.horizontal, 8)
        .background(.bar)
    }

    private func handleIndicatorTap(name: String, keyName: String?) {
        guard let keyName = keyName else { return }

        // Special case: Caps Lock with sync enabled
        if name == "caps-lock-led" && keyboardMappingManager.isCapsLockSyncEnabled {
            NSSound.beep()
            return
        }

        keyboardClient.tapKeyByName(keyName)
    }
}

#if DEBUG
struct StatusBarView_Previews: PreviewProvider {
    static var previews: some View {
        StatusBarView(
            systemClient: SystemClient(),
            indicatorClient: IndicatorClient(),
            keyboardClient: KeyboardClient(),
            keyboardMappingManager: KeyboardMappingManager()
        )
        .frame(width: 600)
    }
}
#endif
