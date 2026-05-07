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

    // MARK: - Window background

    func testDefaultWindowBackgroundIsTheBuiltInDarkGrey() {
        // New users should see the same dark grey that shipped before the
        // setting was configurable.
        let settings = VideoSettings()
        XCTAssertEqual(settings.windowBackground.sRGBHex,
                       VideoSettings.defaultWindowBackground.sRGBHex)
    }

    func testCustomInitialWindowBackgroundIsRespected() {
        let pink = Color(sRGBHex: "#FFC0CB")!
        let settings = VideoSettings(initialWindowBackground: pink)
        XCTAssertEqual(settings.windowBackground.sRGBHex, "#FFC0CB")
    }

    func testWindowBackgroundChangeEmitsObservableObjectChange() {
        let settings = VideoSettings()
        let exp = expectation(description: "objectWillChange fires")
        let cancellable = settings.objectWillChange.sink { _ in exp.fulfill() }
        settings.windowBackground = Color(sRGBHex: "#102030")!
        wait(for: [exp], timeout: 1.0)
        _ = cancellable
    }
}

// MARK: - Edge margin defaults

@MainActor
final class StandardDisplayStyleEdgeMarginTests: XCTestCase {

    private func makeFrame() -> FrameContext {
        FrameContext(
            textureWidth: 320, textureHeight: 256,
            displayWidth: 640, displayHeight: 256,
            leftBorder: 0, rightBorder: 0,
            topBorder: 0, bottomBorder: 0,
            interlaced: false,
            regions: [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)]
        )
    }

    private func makeDrawable() -> DrawableContext {
        DrawableContext(drawableSize: CGSize(width: 1920, height: 1080), parScale: 0.96)
    }

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
        let u = style.makeUniforms(frame: makeFrame(), drawable: makeDrawable())
        XCTAssertEqual(u.edgeMargin, 0.05, accuracy: 1e-6)
    }

    func testDebugStyleAlwaysSetsEdgeMarginToZero() {
        // Debug fills the entire content rectangle with active+border content
        // so an edge margin would create a margin around the borders, which
        // is confusing.
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(
            frame: FrameContext(
                textureWidth: 320, textureHeight: 256,
                displayWidth: 640, displayHeight: 256,
                leftBorder: 32, rightBorder: 32,
                topBorder: 16, bottomBorder: 16,
                interlaced: false,
                regions: [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)]
            ),
            drawable: makeDrawable()
        )
        XCTAssertEqual(u.edgeMargin, 0)
    }

    // MARK: - Edge margin colour

    func testDefaultEdgeMarginColorIsOpaqueBlack() {
        // The default colour must clearly differ from any reasonable window
        // background, otherwise users would not see the frame.
        let style = StandardDisplayStyle()
        let c = style.edgeMarginColor.simd4
        XCTAssertEqual(c.x, 0.0, accuracy: 1e-6)
        XCTAssertEqual(c.y, 0.0, accuracy: 1e-6)
        XCTAssertEqual(c.z, 0.0, accuracy: 1e-6)
        XCTAssertEqual(c.w, 1.0, accuracy: 1e-6)
    }

    func testEdgeMarginColorPropagatesIntoUniforms() {
        let style = StandardDisplayStyle()
        let cyan = Color(.sRGB, red: 0, green: 1, blue: 1, opacity: 1)
        style.edgeMarginColor = cyan
        let u = style.makeUniforms(frame: makeFrame(), drawable: makeDrawable())
        XCTAssertEqual(u.edgeMarginColor.x, 0.0, accuracy: 1e-3)
        XCTAssertEqual(u.edgeMarginColor.y, 1.0, accuracy: 1e-3)
        XCTAssertEqual(u.edgeMarginColor.z, 1.0, accuracy: 1e-3)
        XCTAssertEqual(u.edgeMarginColor.w, 1.0, accuracy: 1e-3)
    }

    func testEdgeMarginColorChangeEmitsObservableObjectChange() {
        let style = StandardDisplayStyle()
        let exp = expectation(description: "objectWillChange fires")
        let cancellable = style.objectWillChange.sink { _ in exp.fulfill() }
        style.edgeMarginColor = Color(.sRGB, red: 1, green: 0, blue: 0, opacity: 1)
        wait(for: [exp], timeout: 1.0)
        _ = cancellable
    }

    func testDebugStyleZeroesEdgeMarginColorForHygiene() {
        // Debug's fragment shader does not read edgeMarginColor; zeroing it
        // protects against future shader changes that might.
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(frame: makeFrame(), drawable: makeDrawable())
        XCTAssertEqual(u.edgeMarginColor, SIMD4<Float>(0, 0, 0, 0))
    }
}
