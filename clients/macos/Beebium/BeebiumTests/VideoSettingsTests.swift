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

import XCTest
@testable import Beebium

@MainActor
final class VideoSettingsTests: XCTestCase {

    // MARK: - Defaults

    func testDefaultStylesAreDebugAndStandard() {
        let settings = VideoSettings()
        XCTAssertEqual(settings.availableStyles.map { $0.id }, ["debug", "standard"])
    }

    func testDefaultActiveStyleIsDebug() {
        // For commit B, Debug is still the default. Commit C flips this to
        // Standard; the test there will be updated to match.
        let settings = VideoSettings()
        XCTAssertEqual(settings.activeStyleID, "debug")
        XCTAssertEqual(settings.activeStyle.id, "debug")
    }

    // MARK: - Custom initialisation

    func testCustomInitialStyleIsRespected() {
        let settings = VideoSettings(initialStyleID: "standard")
        XCTAssertEqual(settings.activeStyleID, "standard")
        XCTAssertEqual(settings.activeStyle.id, "standard")
    }

    func testUnknownInitialStyleFallsBackToFirstAvailable() {
        let settings = VideoSettings(
            styles: [DebugDisplayStyle(), StandardDisplayStyle()],
            initialStyleID: "definitely-not-a-real-style"
        )
        XCTAssertEqual(settings.activeStyleID, "debug")  // First style in the list
    }

    func testCustomStyleListIsPreserved() {
        // Order in the picker should match the order passed in.
        let settings = VideoSettings(
            styles: [StandardDisplayStyle(), DebugDisplayStyle()],
            initialStyleID: "standard"
        )
        XCTAssertEqual(settings.availableStyles.map { $0.id }, ["standard", "debug"])
    }

    // MARK: - Selection

    func testSelectStyleUpdatesActiveID() {
        let settings = VideoSettings()
        settings.selectStyle(id: "standard")
        XCTAssertEqual(settings.activeStyleID, "standard")
        XCTAssertEqual(settings.activeStyle.id, "standard")
    }

    func testSelectStyleIgnoresUnknownID() {
        let settings = VideoSettings()
        let originalID = settings.activeStyleID
        settings.selectStyle(id: "nonexistent")
        XCTAssertEqual(settings.activeStyleID, originalID)
    }

    func testSelectStyleEmitsObservableObjectChange() {
        // Verify that selecting a different style fires objectWillChange so
        // SwiftUI views observing VideoSettings get re-rendered.
        let settings = VideoSettings()
        let exp = expectation(description: "objectWillChange fires")
        let cancellable = settings.objectWillChange.sink { _ in exp.fulfill() }
        settings.selectStyle(id: "standard")
        wait(for: [exp], timeout: 1.0)
        _ = cancellable
    }

    // MARK: - Active style resolution

    func testActiveStylePointsToTheRightInstance() {
        // After selectStyle, activeStyle should be the actual instance from
        // availableStyles, not a copy.
        let debug = DebugDisplayStyle()
        let standard = StandardDisplayStyle()
        let settings = VideoSettings(styles: [debug, standard], initialStyleID: "debug")
        XCTAssertTrue(settings.activeStyle === debug)
        settings.selectStyle(id: "standard")
        XCTAssertTrue(settings.activeStyle === standard)
    }
}
