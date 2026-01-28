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

struct ConnectDialog: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "network")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("Connect to Machine")
                .font(.headline)
            Text("This feature will be available in a future update.")
                .foregroundColor(.secondary)
            Button("OK") { dismiss() }
                .keyboardShortcut(.defaultAction)
        }
        .padding(40)
        .frame(minWidth: 300)
    }
}
