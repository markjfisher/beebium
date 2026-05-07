// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Small reset-to-default chevron used beside resettable controls.
///
/// Always present in the layout (so toggling default state does not shift
/// neighbouring controls), but disabled and dimmed when the bound value is
/// already at default.
struct ResetToDefaultButton: View {
    let isAtDefault: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: "arrow.counterclockwise")
                .font(.system(size: 11, weight: .semibold))
        }
        .buttonStyle(.borderless)
        .disabled(isAtDefault)
        .help("Reset to default")
        .accessibilityLabel("Reset to default")
    }
}

/// SwiftUI ColorPicker with a small reset chevron beside it and a
/// "Reset to Default" entry in its right-click context menu. Both reset
/// affordances are disabled when the current colour already matches the
/// default.
///
/// Equality is by sRGB hex, so two Colors with identical sRGB components but
/// different underlying providers (e.g. system catalog vs literal) compare
/// equal here.
struct ResettableColorPicker: View {
    /// Accessibility label for the picker. Hidden visually so the caller
    /// controls the surrounding layout.
    let accessibilityLabel: String
    @Binding var color: Color
    let defaultColor: Color
    var supportsOpacity: Bool = false

    private var isAtDefault: Bool {
        color.sRGBHex == defaultColor.sRGBHex
    }

    private func reset() {
        color = defaultColor
    }

    var body: some View {
        HStack(spacing: 6) {
            ColorPicker(accessibilityLabel,
                        selection: $color,
                        supportsOpacity: supportsOpacity)
                .labelsHidden()
                .contextMenu {
                    Button("Reset to Default", action: reset)
                        .disabled(isAtDefault)
                }
            ResetToDefaultButton(isAtDefault: isAtDefault, action: reset)
        }
    }
}

/// Stepper bound to an integer percentage with a sibling reset chevron.
struct ResettablePercentStepper: View {
    @Binding var value: Int
    let defaultValue: Int
    let range: ClosedRange<Int>

    private var isAtDefault: Bool { value == defaultValue }

    private func reset() {
        value = defaultValue
    }

    var body: some View {
        HStack(spacing: 6) {
            Text("\(value)%")
                .foregroundColor(.secondary)
                .frame(width: 36, alignment: .trailing)
                .monospacedDigit()
            Stepper("", value: $value, in: range)
                .labelsHidden()
                .contextMenu {
                    Button("Reset to Default", action: reset)
                        .disabled(isAtDefault)
                }
            ResetToDefaultButton(isAtDefault: isAtDefault, action: reset)
        }
    }
}
