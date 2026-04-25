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

import AppKit
import SwiftUI
import XCTest

@testable import Beebium

/// Drive the AppKit notification dance against the Coordinator
/// directly so the editing flag, dropdown-selection commit, and
/// end-editing commit paths can be verified without an attached
/// SwiftUI view hierarchy. The Coordinator is the highest-risk piece
/// of the ComboBoxRepresentable bridge -- AppKit notifications can
/// interleave (begin-editing, change, end-editing, target/action) and
/// a regression in any of those handlers would surface only at runtime
/// on a real edit, not in any compile-time check.
final class ComboBoxRepresentableTests: XCTestCase {

    /// Helper that owns the captured value + commit count so tests
    /// can construct a ComboBoxRepresentable with a Binding that
    /// targets the helper's storage.
    private final class CaptureBox {
        var value: String = ""
        var commits: Int = 0
    }

    @MainActor
    private func makeRepresentable(initial: String,
                                   options: [String],
                                   capture: CaptureBox)
        -> ComboBoxRepresentable
    {
        capture.value = initial
        return ComboBoxRepresentable(
            value: Binding(
                get: { capture.value },
                set: { capture.value = $0 }
            ),
            options: options,
            placeholder: "",
            onCommit: { capture.commits += 1 })
    }

    @MainActor
    func testDropdownSelectionWritesToBindingAndCommits() async {
        let capture = CaptureBox()
        let representable = makeRepresentable(
            initial: "",
            options: ["alpha", "beta", "gamma"],
            capture: capture)
        let coordinator = representable.makeCoordinator()

        // Build an NSComboBox the Coordinator can read selection from.
        let combo = NSComboBox()
        combo.addItems(withObjectValues: ["alpha", "beta", "gamma"])
        combo.selectItem(at: 1)  // pick "beta"

        let notification = Notification(
            name: NSComboBox.selectionDidChangeNotification,
            object: combo)
        coordinator.comboBoxSelectionDidChange(notification)

        XCTAssertEqual(capture.value, "beta")

        // onCommit fires via DispatchQueue.main.async so we yield to
        // the run loop once before asserting.
        await Task.yield()
        // Spin briefly: DispatchQueue.main.async may not have run by
        // a single Task.yield. Wait up to ~50ms.
        let deadline = Date().addingTimeInterval(0.05)
        while capture.commits == 0 && Date() < deadline {
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTAssertEqual(capture.commits, 1)
    }

    @MainActor
    func testBeginEditingSetsEditingFlag() {
        let capture = CaptureBox()
        let representable = makeRepresentable(
            initial: "initial",
            options: [],
            capture: capture)
        let coordinator = representable.makeCoordinator()

        XCTAssertFalse(coordinator.editing)

        let combo = NSComboBox()
        let notification = Notification(
            name: NSControl.textDidBeginEditingNotification,
            object: combo)
        coordinator.controlTextDidBeginEditing(notification)

        XCTAssertTrue(coordinator.editing)
    }

    @MainActor
    func testEndEditingClearsFlagWritesValueAndCommits() {
        let capture = CaptureBox()
        let representable = makeRepresentable(
            initial: "old",
            options: [],
            capture: capture)
        let coordinator = representable.makeCoordinator()
        coordinator.editing = true  // simulate prior begin-editing

        let combo = NSComboBox()
        combo.stringValue = "new typed value"

        let notification = Notification(
            name: NSControl.textDidEndEditingNotification,
            object: combo)
        coordinator.controlTextDidEndEditing(notification)

        XCTAssertFalse(coordinator.editing)
        XCTAssertEqual(capture.value, "new typed value")
        XCTAssertEqual(capture.commits, 1)
    }

    @MainActor
    func testCommitFromActionWritesValueAndCommits() {
        // Triggered when the user presses Return on the editable
        // text field; AppKit invokes the target/action separately
        // from the end-editing notification.
        let capture = CaptureBox()
        let representable = makeRepresentable(
            initial: "old",
            options: [],
            capture: capture)
        let coordinator = representable.makeCoordinator()

        let combo = NSComboBox()
        combo.stringValue = "return-pressed"
        coordinator.commitFromAction(combo)

        XCTAssertEqual(capture.value, "return-pressed")
        XCTAssertEqual(capture.commits, 1)
    }

    @MainActor
    func testEndEditingWithUnchangedValueStillCommitsButDoesntRewrite() {
        // The implementation only writes to the binding when the new
        // string differs from the current value; onCommit fires
        // unconditionally so the consumer can still react (e.g. flush
        // a pending dispatch).
        let capture = CaptureBox()
        let representable = makeRepresentable(
            initial: "same",
            options: [],
            capture: capture)
        let coordinator = representable.makeCoordinator()
        coordinator.editing = true

        let combo = NSComboBox()
        combo.stringValue = "same"
        coordinator.controlTextDidEndEditing(Notification(
            name: NSControl.textDidEndEditingNotification,
            object: combo))

        XCTAssertEqual(capture.value, "same")
        XCTAssertEqual(capture.commits, 1)
    }
}
