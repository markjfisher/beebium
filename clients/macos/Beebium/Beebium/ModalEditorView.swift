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

/// Renders a `Beebium_ModalEditor` as an inline anchor + pencil button
/// that opens a SwiftUI popover containing the editor tree. Sub-control
/// values are held in a local buffer until the user commits; Save
/// bundles every buffered value into an `EditorCommit` dispatched to
/// the server, Cancel discards the buffers (the server never sees
/// aborted edits).
///
/// Used by `ExtensionViewRenderer` for the `.modalEditor` arm.
struct ModalEditorView: View {
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
