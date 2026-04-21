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
        case .none:
            return AnyView(EmptyView())
        }
    }

    // MARK: - Primitives

    private func renderLabel(id: String, label: Beebium_Label) -> some View {
        Text(label.text)
            .frame(maxWidth: .infinity, alignment: .leading)
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
        switch state {
        case .ok:      return .green
        case .warn:    return .yellow
        case .error:   return .red
        case .unknown: return .gray
        case .UNRECOGNIZED:
            return .gray
        }
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
