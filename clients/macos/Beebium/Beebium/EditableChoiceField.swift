// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Local-state wrapper for `Beebium_EditableChoice`. Wraps an
/// `NSComboBox` via `NSViewRepresentable` so the macOS frontend
/// renders a real native combobox with text editing + dropdown
/// chevron + arrow-key navigation. Dispatch fires on commit (Return,
/// focus loss, dropdown selection); per-keystroke edits stay local.
///
/// Used by `ExtensionViewRenderer.renderEditableChoice` and by
/// `ModalEditorView`'s editor body for `.editableChoice` sub-controls.
struct EditableChoiceField: View {
    let controlId: String
    let editableChoice: Beebium_EditableChoice
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    @State private var localValue: String = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if !editableChoice.label.isEmpty {
                Text(editableChoice.label)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            ComboBoxRepresentable(value: $localValue,
                                  options: editableChoice.options,
                                  placeholder: editableChoice.placeholder,
                                  onCommit: {
                                      dispatch(controlId, .string(localValue))
                                  })
                .frame(height: 24)
                .onAppear {
                    localValue = editableChoice.value
                }
                .onChange(of: editableChoice.value) { newValue in
                    if localValue == editableChoice.value {
                        localValue = newValue
                    }
                }
        }
    }
}

/// `NSComboBox` bridge. The combobox is editable: the user can pick
/// from the dropdown OR type any custom value; on commit the bound
/// `value` holds whatever string is currently in the field. Selection
/// from the dropdown updates `value` immediately and fires `onCommit`;
/// typing updates `value` on Return / focus loss (not per keystroke).
struct ComboBoxRepresentable: NSViewRepresentable {
    @Binding var value: String
    let options: [String]
    let placeholder: String
    let onCommit: () -> Void

    func makeNSView(context: Context) -> NSComboBox {
        let combo = NSComboBox()
        combo.usesDataSource = false
        combo.completes = true
        combo.isEditable = true
        combo.placeholderString = placeholder
        combo.delegate = context.coordinator
        combo.target = context.coordinator
        combo.action = #selector(Coordinator.commitFromAction(_:))
        return combo
    }

    func updateNSView(_ combo: NSComboBox, context: Context) {
        context.coordinator.parent = self

        // Refresh the dropdown content if the option list changed.
        let currentOptions = (combo.objectValues as? [String]) ?? []
        if currentOptions != options {
            combo.removeAllItems()
            combo.addItems(withObjectValues: options)
        }

        // Sync the field text only when the user isn't mid-edit -- the
        // coordinator's `editing` flag suppresses overwrites while a
        // text edit is in progress.
        if !context.coordinator.editing && combo.stringValue != value {
            combo.stringValue = value
        }
        combo.placeholderString = placeholder
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(parent: self)
    }

    final class Coordinator: NSObject, NSComboBoxDelegate, NSTextFieldDelegate {
        var parent: ComboBoxRepresentable
        var editing: Bool = false

        init(parent: ComboBoxRepresentable) {
            self.parent = parent
        }

        // Dropdown selection: write the picked option into the binding
        // and treat as a commit.
        func comboBoxSelectionDidChange(_ notification: Notification) {
            guard let combo = notification.object as? NSComboBox,
                  combo.indexOfSelectedItem >= 0,
                  let item = combo.objectValueOfSelectedItem as? String else {
                return
            }
            parent.value = item
            DispatchQueue.main.async { [parent] in parent.onCommit() }
        }

        func controlTextDidBeginEditing(_ notification: Notification) {
            editing = true
        }

        func controlTextDidEndEditing(_ notification: Notification) {
            editing = false
            guard let combo = notification.object as? NSComboBox else { return }
            if parent.value != combo.stringValue {
                parent.value = combo.stringValue
            }
            parent.onCommit()
        }

        // Triggered by Return on the editable text field; AppKit fires
        // the target/action separately from the end-editing notification.
        @objc func commitFromAction(_ sender: Any?) {
            guard let combo = sender as? NSComboBox else { return }
            if parent.value != combo.stringValue {
                parent.value = combo.stringValue
            }
            parent.onCommit()
        }
    }
}
