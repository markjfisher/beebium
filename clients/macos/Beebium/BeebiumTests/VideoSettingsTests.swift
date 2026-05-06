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

    func testDefaultStylesAreStandardThenDebug() {
        // Standard is the user-facing default; Debug is also offered. Order
        // is the order that appears in the sidebar picker.
        let settings = VideoSettings()
        XCTAssertEqual(settings.availableStyles.map { $0.id }, ["standard", "debug"])
    }

    func testDefaultActiveStyleIsStandard() {
        // New windows start with the user-friendly Standard style.
        let settings = VideoSettings()
        XCTAssertEqual(settings.activeStyleID, "standard")
        XCTAssertEqual(settings.activeStyle.id, "standard")
    }

    // MARK: - Custom initialisation

    func testCustomInitialStyleIsRespected() {
        let settings = VideoSettings(initialStyleID: "debug")
        XCTAssertEqual(settings.activeStyleID, "debug")
        XCTAssertEqual(settings.activeStyle.id, "debug")
    }

    func testUnknownInitialStyleFallsBackToFirstAvailable() {
        let settings = VideoSettings(
            styles: [DebugDisplayStyle(), StandardDisplayStyle()],
            initialStyleID: "definitely-not-a-real-style"
        )
        // Falls back to the first style in the supplied list, which here is
        // Debug because the test passes an explicit non-default order.
        XCTAssertEqual(settings.activeStyleID, "debug")
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
        settings.selectStyle(id: "debug")
        XCTAssertEqual(settings.activeStyleID, "debug")
        XCTAssertEqual(settings.activeStyle.id, "debug")
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
        settings.selectStyle(id: "debug")
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

    // MARK: - Pixel shape

    func testDefaultPixelShapeIsAuthentic() {
        // Authentic matches the original BBC PAL CRT and is what existing
        // users will expect when the new toggle ships.
        let settings = VideoSettings()
        XCTAssertEqual(settings.pixelShape, .authentic)
    }

    func testParScaleConvenienceMatchesPixelShape() {
        let settings = VideoSettings(initialPixelShape: .authentic)
        XCTAssertEqual(settings.parScale, 0.96, accuracy: 1e-6)
        settings.pixelShape = .crisp
        XCTAssertEqual(settings.parScale, 1.0, accuracy: 1e-6)
    }

    func testPixelShapeChangeEmitsObservableObjectChange() {
        let settings = VideoSettings()
        let exp = expectation(description: "objectWillChange fires")
        let cancellable = settings.objectWillChange.sink { _ in exp.fulfill() }
        settings.pixelShape = .crisp
        wait(for: [exp], timeout: 1.0)
        _ = cancellable
    }
}

// MARK: - Edge margin defaults

@MainActor
final class StandardDisplayStyleEdgeMarginTests: XCTestCase {
    func testDefaultEdgeMarginIsNonZero() {
        // The whole point of the default-on edge margin is to lift the BBC's
        // active text area away from the inner window edge. A zero default
        // would defeat that.
        let style = StandardDisplayStyle()
        XCTAssertGreaterThan(style.edgeMargin, 0)
        XCTAssertEqual(style.edgeMargin, StandardDisplayStyle.defaultEdgeMargin)
    }

    func testDefaultEdgeMarginIsAReasonableValue() {
        // Sanity check: default margin should be small enough that the
        // picture is still clearly the dominant element of the window.
        // Less than 5% per edge is comfortable.
        XCTAssertLessThan(StandardDisplayStyle.defaultEdgeMargin, 0.05)
    }

    func testEdgeMarginPropagatesIntoUniforms() {
        let style = StandardDisplayStyle()
        style.edgeMargin = 0.05
        let frame = FrameContext(
            textureWidth: 320, textureHeight: 256,
            displayWidth: 640, displayHeight: 256,
            leftBorder: 0, rightBorder: 0,
            topBorder: 0, bottomBorder: 0,
            interlaced: false,
            regions: [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)]
        )
        let drawable = DrawableContext(
            drawableSize: CGSize(width: 1920, height: 1080),
            parScale: 0.96
        )
        let u = style.makeUniforms(frame: frame, drawable: drawable)
        XCTAssertEqual(u.edgeMargin, 0.05, accuracy: 1e-6)
    }

    func testDebugStyleAlwaysSetsEdgeMarginToZero() {
        // Debug fills the entire content rectangle with active+border content
        // so an edge margin would create a margin around the borders, which
        // is confusing.
        let style = DebugDisplayStyle()
        let frame = FrameContext(
            textureWidth: 320, textureHeight: 256,
            displayWidth: 640, displayHeight: 256,
            leftBorder: 32, rightBorder: 32,
            topBorder: 16, bottomBorder: 16,
            interlaced: false,
            regions: [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)]
        )
        let drawable = DrawableContext(
            drawableSize: CGSize(width: 1920, height: 1080),
            parScale: 0.96
        )
        let u = style.makeUniforms(frame: frame, drawable: drawable)
        XCTAssertEqual(u.edgeMargin, 0)
    }
}
