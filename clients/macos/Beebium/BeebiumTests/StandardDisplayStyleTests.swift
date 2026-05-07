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
import simd
@testable import Beebium

/// Unit tests for StandardDisplayStyle's pure uniform-packing logic.
final class StandardDisplayStyleTests: XCTestCase {

    private func makeFrame(
        textureWidth: Int = 320,
        textureHeight: Int = 256,
        displayWidth: Int = 640,
        displayHeight: Int = 256,
        leftBorder: Int = 32,
        rightBorder: Int = 32,
        topBorder: Int = 16,
        bottomBorder: Int = 16,
        interlaced: Bool = false,
        regions: [DisplayRegion] = [
            DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)
        ]
    ) -> FrameContext {
        FrameContext(
            textureWidth: textureWidth,
            textureHeight: textureHeight,
            displayWidth: displayWidth,
            displayHeight: displayHeight,
            leftBorder: leftBorder,
            rightBorder: rightBorder,
            topBorder: topBorder,
            bottomBorder: bottomBorder,
            interlaced: interlaced,
            regions: regions
        )
    }

    private func makeDrawable(
        width: CGFloat = 1920,
        height: CGFloat = 1080,
        parScale: Float = 0.96
    ) -> DrawableContext {
        DrawableContext(
            drawableSize: CGSize(width: width, height: height),
            parScale: parScale
        )
    }

    // MARK: - Identity

    func testIdentityFields() {
        let style = StandardDisplayStyle()
        XCTAssertEqual(style.id, "standard")
        XCTAssertEqual(style.displayName, "Standard")
    }

    func testHasOptions() {
        // Standard exposes Edge Margin Size and Colour, so the sidebar
        // renders its options section.
        XCTAssertTrue(StandardDisplayStyle().hasOptions)
    }

    // MARK: - Geometry

    func testTotalSizeEqualsDisplaySize() {
        // The Standard style fits ONLY the active pixel area; borders are not
        // part of the picture. totalSize == displaySize.
        let style = StandardDisplayStyle()
        let u = style.makeUniforms(
            frame: makeFrame(displayWidth: 640, displayHeight: 256,
                             leftBorder: 64, rightBorder: 64,
                             topBorder: 32, bottomBorder: 32),
            drawable: makeDrawable()
        )
        XCTAssertEqual(u.totalSize, SIMD2<Float>(640, 256))
        XCTAssertEqual(u.totalSize, u.displaySize)
    }

    func testBorderOffsetIsZero() {
        // Standard never paints borders, so the content origin is (0, 0).
        let style = StandardDisplayStyle()
        let u = style.makeUniforms(
            frame: makeFrame(leftBorder: 64, topBorder: 32),
            drawable: makeDrawable()
        )
        XCTAssertEqual(u.borderOffset, SIMD2<Float>(0, 0))
    }

    func testTextureAndDisplaySizePropagateIndependently() {
        let style = StandardDisplayStyle()
        let u = style.makeUniforms(
            frame: makeFrame(textureWidth: 320, textureHeight: 256,
                             displayWidth: 640, displayHeight: 256),
            drawable: makeDrawable()
        )
        XCTAssertEqual(u.textureSize, SIMD2<Float>(320, 256))
        XCTAssertEqual(u.displaySize, SIMD2<Float>(640, 256))
    }

    func testActiveAreaSizeStaysConstantAcrossDifferentBorders() {
        // The whole point of Standard: changing CRTC borders should NOT
        // affect the displayed picture's apparent size on screen.
        let style = StandardDisplayStyle()
        let smallBorders = style.makeUniforms(
            frame: makeFrame(leftBorder: 8, rightBorder: 8,
                             topBorder: 4, bottomBorder: 4),
            drawable: makeDrawable()
        )
        let largeBorders = style.makeUniforms(
            frame: makeFrame(leftBorder: 96, rightBorder: 96,
                             topBorder: 48, bottomBorder: 48),
            drawable: makeDrawable()
        )
        XCTAssertEqual(smallBorders.totalSize, largeBorders.totalSize)
    }

    // MARK: - Mode flags

    func testInterlacedFlagPropagates() {
        let style = StandardDisplayStyle()
        let progressive = style.makeUniforms(frame: makeFrame(interlaced: false),
                                             drawable: makeDrawable())
        let interlaced = style.makeUniforms(frame: makeFrame(interlaced: true),
                                            drawable: makeDrawable())
        XCTAssertEqual(progressive.interlaced, 0)
        XCTAssertEqual(interlaced.interlaced, 1)
    }

    func testParScalePropagates() {
        let style = StandardDisplayStyle()
        let authentic = style.makeUniforms(frame: makeFrame(),
                                           drawable: makeDrawable(parScale: 0.96))
        let crisp = style.makeUniforms(frame: makeFrame(),
                                       drawable: makeDrawable(parScale: 1.0))
        XCTAssertEqual(authentic.parScale, 0.96)
        XCTAssertEqual(crisp.parScale, 1.0)
    }

    // MARK: - Border colours zeroed for hygiene

    func testBorderColoursAreZeroed() {
        // Standard's fragment shader doesn't read border colours, but we zero
        // them anyway so a future shader change can't pick up stale values.
        let style = StandardDisplayStyle()
        let u = style.makeUniforms(frame: makeFrame(), drawable: makeDrawable())
        XCTAssertEqual(u.leftBorderColor, SIMD4<Float>(0, 0, 0, 0))
        XCTAssertEqual(u.rightBorderColor, SIMD4<Float>(0, 0, 0, 0))
        XCTAssertEqual(u.topBorderColor, SIMD4<Float>(0, 0, 0, 0))
        XCTAssertEqual(u.bottomBorderColor, SIMD4<Float>(0, 0, 0, 0))
    }

    // MARK: - Display Regions

    func testSingleRegionPacksIntoFirstSlot() {
        let style = StandardDisplayStyle()
        let regions = [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)]
        let u = style.makeUniforms(frame: makeFrame(regions: regions),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.regionCount, 1)
        XCTAssertEqual(u.regions.0.pixelWidth, 320)
    }

    func testTwoRegionsForSplitScreenLayout() {
        let style = StandardDisplayStyle()
        let regions = [
            DisplayRegion(startLine: 0, endLine: 192, pixelWidth: 320),
            DisplayRegion(startLine: 192, endLine: 248, pixelWidth: 160)
        ]
        let u = style.makeUniforms(frame: makeFrame(regions: regions),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.regionCount, 2)
        XCTAssertEqual(u.regions.0.pixelWidth, 320)
        XCTAssertEqual(u.regions.1.pixelWidth, 160)
    }

    func testMoreRegionsThanMaxAreClampedToMax() {
        let style = StandardDisplayStyle()
        let regions = (0..<(maxDisplayRegions + 2)).map {
            DisplayRegion(startLine: $0 * 10, endLine: ($0 + 1) * 10,
                          pixelWidth: 320 + $0)
        }
        let u = style.makeUniforms(frame: makeFrame(regions: regions),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.regionCount, UInt32(maxDisplayRegions))
        XCTAssertEqual(u.regions.7.pixelWidth, 327)
    }
}
