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

/// Container view that displays content for the selected sidebar mode
struct SidebarModeContent: View {
    let mode: SidebarMode
    @ObservedObject var discClient: DiscClient

    var body: some View {
        switch mode {
        case .storage:
            StorageModeView(discClient: discClient)
        case .memory:
            MemoryModeView()
        case .peripherals:
            PeripheralsModeView()
        case .video:
            VideoModeView()
        case .sound:
            SoundModeView()
        case .keyboard:
            KeyboardModeView()
        case .coprocessor:
            CoprocessorModeView()
        case .network:
            NetworkModeView()
        }
    }
}

// MARK: - Placeholder Mode Views

/// Placeholder view for Memory mode (sideways ROM slots, memory config)
struct MemoryModeView: View {
    var body: some View {
        ModePlaceholder(mode: .memory)
    }
}

/// Placeholder view for Peripherals mode (1MHz bus, user port, printer, serial, analogue)
struct PeripheralsModeView: View {
    var body: some View {
        ModePlaceholder(mode: .peripherals)
    }
}

/// Placeholder view for Video mode
struct VideoModeView: View {
    var body: some View {
        ModePlaceholder(mode: .video)
    }
}

/// Placeholder view for Sound mode
struct SoundModeView: View {
    var body: some View {
        ModePlaceholder(mode: .sound)
    }
}

/// Placeholder view for Keyboard mode
struct KeyboardModeView: View {
    var body: some View {
        ModePlaceholder(mode: .keyboard)
    }
}

/// Placeholder view for Coprocessor mode (Tube coprocessors)
struct CoprocessorModeView: View {
    var body: some View {
        ModePlaceholder(mode: .coprocessor)
    }
}

/// Placeholder view for Network mode
struct NetworkModeView: View {
    var body: some View {
        ModePlaceholder(mode: .network)
    }
}

// MARK: - Common Placeholder

/// Generic placeholder view showing mode icon and name
private struct ModePlaceholder: View {
    let mode: SidebarMode

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: mode.icon)
                .font(.system(size: 32))
                .foregroundColor(.secondary)
            Text(mode.label)
                .font(.headline)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

#if DEBUG
struct SidebarModeContent_Previews: PreviewProvider {
    static var previews: some View {
        SidebarModeContent(mode: .storage, discClient: DiscClient())
            .frame(width: 220, height: 300)
            .background(Color(nsColor: .windowBackgroundColor))
    }
}
#endif
