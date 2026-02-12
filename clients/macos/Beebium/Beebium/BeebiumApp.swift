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

// MARK: - App Delegate

/// Handles app quit by delegating to MachineManager for shutdown decisions.
///
/// Quit rule: shut down a core only if BOTH (a) this app launched it AND (b) it's the
/// sole client. Everything else is silently disconnected. No quit dialog ever.
class BeebiumAppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        let machineManager = MachineManager.shared
        let action = machineManager.quitAction()

        switch action {
        case .terminateNow:
            return .terminateNow
        case .shutdownThenTerminate(let machineIds):
            // Terminate all sole-client servers via SIGTERM, then allow quit
            machineManager.terminateServers(ids: machineIds)
            return .terminateNow
        }
    }
}

@main
struct BeebiumApp: App {
    @NSApplicationDelegateAdaptor(BeebiumAppDelegate.self) var appDelegate
    @FocusedBinding(\.showStatusBar) private var showStatusBar
    @FocusedBinding(\.showSidebar) private var showSidebar
    @FocusedBinding(\.sidebarMode) private var sidebarMode
    @StateObject private var keyboardMappingManager = KeyboardMappingManager()
    @StateObject private var connectWindowState = ConnectWindowState.shared

    init() {
        NSLog("[BeebiumApp] Starting...")
    }

    var body: some Scene {
        WindowGroup("Beebium", id: "main") {
            ContentView(
                keyboardMappingManager: keyboardMappingManager
            )
        }
        .windowToolbarStyle(.unified)
        .commands {
            FileCommands()
            CommandGroup(after: .toolbar) {
                Button(showStatusBar == true ? "Hide Status Bar" : "Show Status Bar") {
                    showStatusBar?.toggle()
                }
                .keyboardShortcut("/", modifiers: .command)
                .disabled(showStatusBar == nil)
            }
            CommandGroup(before: .sidebar) {
                Button(showSidebar == true ? "Hide Sidebar" : "Show Sidebar") {
                    withAnimation { showSidebar?.toggle() }
                }
                .keyboardShortcut("s", modifiers: [.control, .command])
                .disabled(showSidebar == nil)
            }
            CommandGroup(after: .sidebar) {
                ForEach(SidebarMode.allCases) { mode in
                    Button(mode.label) {
                        sidebarMode = mode
                        if showSidebar == false {
                            withAnimation { showSidebar = true }
                        }
                    }
                    .keyboardShortcut(mode.shortcutKey, modifiers: .command)
                    .disabled(sidebarMode == nil)
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

        // Welcome window (singleton, shown on bare launch)
        Window("Welcome to Beebium", id: "welcome") {
            WelcomeWindowContent()
        }
        .windowResizability(.contentSize)
        .defaultPosition(.center)
        .windowStyle(.hiddenTitleBar)

        // New Machine dialog window (singleton, opened from Welcome Window's "New Machine...")
        Window("New Machine", id: "new-machine") {
            NewMachineDialog()
        }
        .windowResizability(.contentSize)
        .defaultPosition(.center)

        // Connect window (singleton, non-modal)
        Window("Connect to Machine", id: "connect") {
            ConnectWindowContent()
        }
        .windowResizability(.contentSize)
        .defaultPosition(.center)

        // Settings window (singleton, accessed via Cmd+, or Beebium menu)
        Settings {
            SettingsView()
        }
    }
}
