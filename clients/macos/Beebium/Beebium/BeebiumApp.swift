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

@main
struct BeebiumApp: App {
    @AppStorage("showStatusBar") private var showStatusBar = true
    @AppStorage("showSidebar") private var showSidebar = true
    @AppStorage("sidebarMode") private var sidebarMode: SidebarMode = .storage
    @StateObject private var keyboardMappingManager = KeyboardMappingManager()

    init() {
        NSLog("[BeebiumApp] Starting...")
    }

    var body: some Scene {
        WindowGroup("Beebium") {
            ContentView(
                showStatusBar: $showStatusBar,
                showSidebar: $showSidebar,
                keyboardMappingManager: keyboardMappingManager
            )
        }
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(after: .toolbar) {
                Toggle("Show Status Bar", isOn: $showStatusBar)
                    .keyboardShortcut("/", modifiers: .command)
            }
            CommandGroup(before: .sidebar) {
                Button(showSidebar ? "Hide Sidebar" : "Show Sidebar") {
                    withAnimation { showSidebar.toggle() }
                }
                .keyboardShortcut("s", modifiers: [.control, .command])
            }
            CommandGroup(after: .sidebar) {
                ForEach(SidebarMode.allCases) { mode in
                    Button(mode.label) {
                        sidebarMode = mode
                        if !showSidebar {
                            withAnimation { showSidebar = true }
                        }
                    }
                    .keyboardShortcut(mode.shortcutKey, modifiers: .command)
                }
            }
            CommandGroup(after: .textEditing) {
                Divider()
                if let target = keyboardMappingManager.toggleTargetMapping {
                    Button("Switch to \(target.name) Keyboard Mapping") {
                        keyboardMappingManager.toggleToDefaultLogical()
                    }
                    .keyboardShortcut("k", modifiers: .command)
                } else {
                    Button("Switch to Previous Keyboard Mapping") {
                    }
                    .keyboardShortcut("k", modifiers: .command)
                    .disabled(true)
                }
            }
        }
    }
}
