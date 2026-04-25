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

/// Recursive SwiftUI renderer for the server-driven Extension UI tree.
///
/// One ExtensionViewRenderer is created per top-level View; the body
/// walks the proto Control tree and produces native widgets. Stable
/// `.id(control.id)` modifiers keep widget identity across pushes so
/// SwiftUI patches in place rather than rebuilding the panel each time.
struct ExtensionViewRenderer: View {
    /// The control to render (typically a Group at the top of the tree).
    let control: Beebium_Control

    /// View revision the user is acting on. Stamped into outgoing
    /// Dispatch events so the server can reject events that reference
    /// a control the next push has already replaced.
    let viewRevision: UInt64

    /// Closure the renderer invokes when a widget produces an event.
    /// The framework / outer view layer is responsible for posting it
    /// over gRPC.
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    var body: some View {
        renderControl(control)
    }

    // Returns AnyView (type-erased) rather than `some View` because
    // this function is recursive (the .group branch calls back into
    // renderControl for each child). A recursive @ViewBuilder switch
    // returning seven different concrete View types triggers a
    // swift-frontend SIGILL deep inside ReplaceOpaqueTypesWithUnderlyingTypes
    // when whole-module optimisation is enabled (Release builds);
    // Debug builds with -Onone never substitute opaque types and do
    // not hit the bug. The cost of AnyView's type erasure is
    // negligible for a control tree of tens of nodes, and SwiftUI
    // still keeps stable widget identity via the .id(control.id)
    // modifier on each leaf.
    private func renderControl(_ control: Beebium_Control) -> AnyView {
        switch control.control {
        case .label(let label):
            return AnyView(renderLabel(id: control.id, label: label))
        case .indicator(let indicator):
            return AnyView(renderIndicator(id: control.id, indicator: indicator))
        case .toggle(let toggle):
            return AnyView(renderToggle(id: control.id, toggle: toggle))
        case .button(let button):
            return AnyView(renderButton(id: control.id, button: button))
        case .choice(let choice):
            return AnyView(renderChoice(id: control.id, choice: choice))
        case .textInput(let textInput):
            return AnyView(renderTextInput(id: control.id, textInput: textInput))
        case .group(let group):
            return AnyView(renderGroup(id: control.id, group: group))
        case .modalEditor(let modal):
            return AnyView(ModalEditorView(controlId: control.id,
                                           modal: modal,
                                           dispatch: dispatch)
                .id(control.id))
        case .editableChoice(let ec):
            return AnyView(renderEditableChoice(id: control.id, editableChoice: ec))
        case .none:
            return AnyView(EmptyView())
        }
    }

    // MARK: - Primitives

    private func renderLabel(id: String, label: Beebium_Label) -> some View {
        // secondary_text (when present) is a renderer hint for short
        // provenance / status / size metadata attached to the primary
        // line. AppKit conventions push it right and de-emphasise it,
        // so the primary text reads as the focus and the secondary
        // disambiguates without competing for attention.
        HStack(spacing: 6) {
            Text(label.text)
                .frame(maxWidth: .infinity, alignment: .leading)
            if !label.secondaryText.isEmpty {
                Text(label.secondaryText)
                    .foregroundStyle(.secondary)
                    .font(.caption)
            }
        }
        .id(id)
    }

    private func renderIndicator(id: String, indicator: Beebium_Indicator) -> some View {
        HStack(spacing: 6) {
            Image(systemName: "circle.fill")
                .font(.system(size: 8))
                .foregroundColor(indicatorColor(indicator.state))
            Text(indicator.text)
                .foregroundColor(.secondary)
            Spacer()
        }
        .id(id)
    }

    private func renderToggle(id: String, toggle: Beebium_Toggle) -> some View {
        // Bind the toggle to a local source-of-truth derived from the
        // current pushed value. SwiftUI's Toggle wants a Binding; we
        // construct one whose setter dispatches and whose getter
        // returns whatever the server most recently said.
        let binding = Binding<Bool>(
            get: { toggle.value },
            set: { newValue in
                dispatch(id, .bool(newValue))
            }
        )
        return Toggle(toggle.label, isOn: binding)
            .toggleStyle(.switch)
            .id(id)
    }

    private func renderButton(id: String, button: Beebium_Button) -> some View {
        Button(button.label) {
            dispatch(id, .none)
        }
        .disabled(!button.enabled)
        .id(id)
    }

    private func renderChoice(id: String, choice: Beebium_Choice) -> some View {
        let binding = Binding<Int>(
            get: { Int(choice.selectedIndex) },
            set: { newValue in
                dispatch(id, .index(UInt32(newValue)))
            }
        )
        return Picker(choice.label, selection: binding) {
            ForEach(Array(choice.options.enumerated()), id: \.offset) { index, option in
                Text(option).tag(index)
            }
        }
        .id(id)
    }

    private func renderTextInput(id: String, textInput: Beebium_TextInput) -> some View {
        TextInputField(controlId: id,
                       textInput: textInput,
                       dispatch: dispatch)
            .id(id)
    }

    private func renderEditableChoice(id: String,
                                      editableChoice: Beebium_EditableChoice) -> some View {
        EditableChoiceField(controlId: id,
                            editableChoice: editableChoice,
                            dispatch: dispatch)
            .id(id)
    }

    private func renderGroup(id: String, group: Beebium_Group) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            if !group.label.isEmpty {
                Text(group.label)
                    .font(.headline)
                    .foregroundColor(.secondary)
            }
            ForEach(Array(group.controls.enumerated()), id: \.element.id) { _, child in
                renderControl(child)
            }
        }
        .id(id)
    }

    private func indicatorColor(_ state: Beebium_Indicator.State) -> Color {
        extensionUiIndicatorColor(state)
    }
}

// File-scope so both the main renderer and the ModalEditor popover's
// anchor view can share one implementation.
fileprivate func extensionUiIndicatorColor(_ state: Beebium_Indicator.State) -> Color {
    switch state {
    case .ok:      return .green
    case .warn:    return .yellow
    case .error:   return .red
    case .unknown: return .gray
    case .UNRECOGNIZED:
        return .gray
    }
}

/// Local-state wrapper for TextInput so typing doesn't fire a dispatch
/// per keystroke. Dispatches on commit (Return / focus loss).
private struct TextInputField: View {
    let controlId: String
    let textInput: Beebium_TextInput
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    @State private var localValue: String = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if !textInput.label.isEmpty {
                Text(textInput.label)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            TextField(textInput.placeholder, text: $localValue)
                .textFieldStyle(.roundedBorder)
                .onSubmit {
                    dispatch(controlId, .string(localValue))
                }
                .onAppear {
                    localValue = textInput.value
                }
                .onChange(of: textInput.value) { newValue in
                    // Server pushed an authoritative new value; if the
                    // user isn't mid-edit (their local matches what the
                    // server last sent), accept the server's update.
                    if localValue == textInput.value {
                        localValue = newValue
                    }
                }
        }
    }
}

/// Local-state wrapper for EditableChoice. Wraps an NSComboBox via
/// NSViewRepresentable so the macOS frontend renders a real native
/// combobox with text editing + dropdown chevron + arrow-key
/// navigation. Dispatch fires on commit (Return / focus loss /
/// dropdown selection); per-keystroke edits stay local.
private struct EditableChoiceField: View {
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

/// NSComboBox bridge. The combobox is editable: the user can pick from
/// the dropdown OR type any custom value; on commit the bound `value`
/// holds whatever string is currently in the field. Selection from the
/// dropdown updates `value` immediately and fires onCommit; typing
/// updates `value` on Return / focus loss (not per keystroke).
fileprivate struct ComboBoxRepresentable: NSViewRepresentable {
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

/// Renders a ModalEditor as an inline anchor + pencil button that opens
/// a SwiftUI popover containing the editor tree. Sub-control values are
/// held in a local buffer until the user commits; Save bundles every
/// buffered value into an EditorCommit dispatched to the server, Cancel
/// discards the buffers (the server never sees aborted edits).
fileprivate struct ModalEditorView: View {
    let controlId: String
    let modal: Beebium_ModalEditor
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    /// Typed buffer slot for one editor sub-control's in-flight value.
    enum FieldBuffer: Sendable {
        case bool(Bool)
        case string(String)
        case index(UInt32)
    }

    @State private var isPresented: Bool = false
    @State private var buffers: [String: FieldBuffer] = [:]

    var body: some View {
        HStack(spacing: 6) {
            anchorView(modal.anchor)
                .frame(maxWidth: .infinity, alignment: .leading)
            if modal.editable {
                Button {
                    // Seed local buffers from the editor tree each time
                    // the popover opens so the initial state matches
                    // what the server most recently published.
                    buffers = collectInitialBuffers(modal.editor)
                    isPresented = true
                } label: {
                    Image(systemName: "square.and.pencil")
                }
                .buttonStyle(.borderless)
                .help("Edit")
                .popover(isPresented: $isPresented, arrowEdge: .bottom) {
                    editorPopoverBody
                }
            }
        }
    }

    // MARK: - Popover

    private var editorPopoverBody: some View {
        VStack(alignment: .leading, spacing: 10) {
            editorBody(modal.editor)
            HStack {
                if modal.showCancel {
                    Button("Cancel") {
                        isPresented = false
                    }
                    .keyboardShortcut(.cancelAction)
                }
                Spacer()
                Button(commitLabel) {
                    dispatch(controlId, .editorCommit(buildFields()))
                    isPresented = false
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding()
        .frame(minWidth: 280)
    }

    private var commitLabel: String {
        switch modal.commitRole {
        case .save:        return "Save"
        case .add:         return "Add"
        case .UNRECOGNIZED: return "Save"
        }
    }

    // MARK: - Editor tree

    // Returns AnyView for the same reason the main renderer does: the
    // editor tree can contain Groups whose children recurse back here,
    // and an opaque-type recursive switch trips a swift-frontend
    // compiler bug under Release whole-module optimisation.
    private func editorBody(_ control: Beebium_Control) -> AnyView {
        switch control.control {
        case .group(let group):
            return AnyView(VStack(alignment: .leading, spacing: 8) {
                if !group.label.isEmpty {
                    Text(group.label)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                ForEach(Array(group.controls.enumerated()), id: \.element.id) { _, child in
                    editorBody(child)
                }
            }
            .id(control.id))
        case .textInput(let ti):
            let binding = Binding<String>(
                get: {
                    if case .string(let s) = buffers[control.id] { return s }
                    return ""
                },
                set: { buffers[control.id] = .string($0) }
            )
            return AnyView(VStack(alignment: .leading, spacing: 4) {
                if !ti.label.isEmpty {
                    Text(ti.label).font(.caption).foregroundColor(.secondary)
                }
                TextField(ti.placeholder, text: binding)
                    .textFieldStyle(.roundedBorder)
            }
            .id(control.id))
        case .editableChoice(let ec):
            let binding = Binding<String>(
                get: {
                    if case .string(let s) = buffers[control.id] { return s }
                    return ""
                },
                set: { buffers[control.id] = .string($0) }
            )
            return AnyView(VStack(alignment: .leading, spacing: 4) {
                if !ec.label.isEmpty {
                    Text(ec.label).font(.caption).foregroundColor(.secondary)
                }
                ComboBoxRepresentable(value: binding,
                                      options: ec.options,
                                      placeholder: ec.placeholder,
                                      onCommit: { /* deferred to ModalEditor commit */ })
                    .frame(height: 24)
            }
            .id(control.id))
        case .choice(let ch):
            let binding = Binding<Int>(
                get: {
                    if case .index(let i) = buffers[control.id] { return Int(i) }
                    return 0
                },
                set: { buffers[control.id] = .index(UInt32($0)) }
            )
            return AnyView(Picker(ch.label, selection: binding) {
                ForEach(Array(ch.options.enumerated()), id: \.offset) { idx, option in
                    Text(option).tag(idx)
                }
            }
            .id(control.id))
        case .toggle(let tg):
            let binding = Binding<Bool>(
                get: {
                    if case .bool(let b) = buffers[control.id] { return b }
                    return false
                },
                set: { buffers[control.id] = .bool($0) }
            )
            return AnyView(Toggle(tg.label, isOn: binding)
                .toggleStyle(.switch)
                .id(control.id))
        case .label(let lbl):
            return AnyView(Text(lbl.text).id(control.id))
        default:
            // Nested ModalEditor / Button / Indicator inside an editor
            // tree are discouraged; silently skip rather than add
            // speculative surface for shapes we don't ship.
            return AnyView(EmptyView())
        }
    }

    // Anchor controls are always-visible and read-only; dispatch is
    // never fired from here. A minimal subset (Label, Indicator) covers
    // every anchor shape we have today.
    private func anchorView(_ control: Beebium_Control) -> AnyView {
        switch control.control {
        case .label(let lbl):
            return AnyView(HStack(spacing: 6) {
                Text(lbl.text)
                    .frame(maxWidth: .infinity, alignment: .leading)
                if !lbl.secondaryText.isEmpty {
                    Text(lbl.secondaryText)
                        .foregroundStyle(.secondary)
                        .font(.caption)
                }
            })
        case .indicator(let ind):
            return AnyView(HStack(spacing: 6) {
                Image(systemName: "circle.fill")
                    .font(.system(size: 8))
                    .foregroundColor(extensionUiIndicatorColor(ind.state))
                Text(ind.text)
                    .foregroundColor(.secondary)
            })
        default:
            return AnyView(EmptyView())
        }
    }

    // MARK: - Buffer plumbing

    private func collectInitialBuffers(_ control: Beebium_Control)
        -> [String: FieldBuffer]
    {
        var out: [String: FieldBuffer] = [:]
        populate(control, into: &out)
        return out
    }

    private func populate(_ control: Beebium_Control,
                          into out: inout [String: FieldBuffer]) {
        switch control.control {
        case .group(let group):
            for child in group.controls {
                populate(child, into: &out)
            }
        case .textInput(let ti):
            out[control.id] = .string(ti.value)
        case .editableChoice(let ec):
            out[control.id] = .string(ec.value)
        case .choice(let ch):
            out[control.id] = .index(ch.selectedIndex)
        case .toggle(let tg):
            out[control.id] = .bool(tg.value)
        default:
            break
        }
    }

    private func buildFields() -> [EditorFieldCommit] {
        buffers.map { (id, value) in
            let fv: EditorFieldCommit.EditorFieldValue
            switch value {
            case .bool(let b):   fv = .bool(b)
            case .string(let s): fv = .string(s)
            case .index(let i):  fv = .index(i)
            }
            return EditorFieldCommit(fieldID: id, value: fv)
        }
    }
}
