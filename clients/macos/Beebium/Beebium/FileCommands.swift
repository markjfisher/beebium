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

struct FileCommands: Commands {
    @FocusedBinding(\.showConnectDialog) private var showConnectDialog
    @FocusedBinding(\.showNewMachineDialog) private var showNewMachineDialog
    @FocusedValue(\.openNewWindow) private var openNewWindow

    var body: some Commands {
        CommandGroup(replacing: .newItem) {
            Button("New...") {
                showNewMachineDialog = true
            }
            .keyboardShortcut("n", modifiers: .command)
            .disabled(showNewMachineDialog == nil)

            Button("New Window") {
                openNewWindow?()
            }
            .keyboardShortcut("n", modifiers: [.command, .shift])
            .disabled(openNewWindow == nil)
        }

        CommandGroup(after: .newItem) {
            Button("Open...") { }
            .keyboardShortcut("o", modifiers: .command)
            .disabled(true)

            Menu("Open Recent") {
                Text("No Recent Items")
            }
            .disabled(true)

            Divider()

            Button("Connect...") {
                showConnectDialog = true
            }
            .disabled(showConnectDialog == nil)
        }

        CommandGroup(replacing: .saveItem) {
            Button("Save") { }
            .keyboardShortcut("s", modifiers: .command)
            .disabled(true)

            Button("Save As...") { }
            .keyboardShortcut("s", modifiers: [.command, .shift])
            .disabled(true)

            Menu("Revert To") {
                Text("No Saved Versions")
            }
            .disabled(true)
        }
        // Close (Cmd-W) provided automatically by SwiftUI
    }
}
