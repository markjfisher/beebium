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

struct ShowConnectDialogKey: FocusedValueKey {
    typealias Value = Binding<Bool>
}

struct ShowNewMachineDialogKey: FocusedValueKey {
    typealias Value = Binding<Bool>
}

struct OpenNewWindowActionKey: FocusedValueKey {
    typealias Value = () -> Void
}

extension FocusedValues {
    var openNewWindow: (() -> Void)? {
        get { self[OpenNewWindowActionKey.self] }
        set { self[OpenNewWindowActionKey.self] = newValue }
    }
    var showConnectDialog: Binding<Bool>? {
        get { self[ShowConnectDialogKey.self] }
        set { self[ShowConnectDialogKey.self] = newValue }
    }

    var showNewMachineDialog: Binding<Bool>? {
        get { self[ShowNewMachineDialogKey.self] }
        set { self[ShowNewMachineDialogKey.self] = newValue }
    }
}
