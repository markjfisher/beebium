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

/// Unit tests for DebugDisplayStyle. The style's uniform-packing logic is pure
/// data math, so these tests run without an MTLDevice.
final class DebugDisplayStyleTests: XCTestCase {

    // MARK: - Test Fixtures

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
        let style = DebugDisplayStyle()
        XCTAssertEqual(style.id, "debug")
        XCTAssertEqual(style.displayName, "Debug")
    }

    // MARK: - Geometry

    func testTotalSizeIncludesBorders() {
        // Debug style aspect-fits the active+border rectangle, so totalSize
        // must include the border dimensions.
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(frame: makeFrame(), drawable: makeDrawable())
        XCTAssertEqual(u.totalSize, SIMD2<Float>(704, 288))  // 32+640+32, 16+256+16
    }

    func testBorderOffsetMatchesLeftAndTop() {
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(frame: makeFrame(leftBorder: 40, topBorder: 24),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.borderOffset, SIMD2<Float>(40, 24))
    }

    func testTextureAndDisplaySizePropagateIndependently() {
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(
            frame: makeFrame(textureWidth: 320, textureHeight: 256,
                             displayWidth: 640, displayHeight: 256),
            drawable: makeDrawable()
        )
        // textureSize is the buffer size (logical pixels); displaySize is the
        // target after horizontal scaling.
        XCTAssertEqual(u.textureSize, SIMD2<Float>(320, 256))
        XCTAssertEqual(u.displaySize, SIMD2<Float>(640, 256))
    }

    func testZeroBordersGiveTotalEqualToDisplay() {
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(
            frame: makeFrame(leftBorder: 0, rightBorder: 0, topBorder: 0, bottomBorder: 0),
            drawable: makeDrawable()
        )
        XCTAssertEqual(u.totalSize, u.displaySize)
        XCTAssertEqual(u.borderOffset, SIMD2<Float>(0, 0))
    }

    func testDrawableSizePropagates() {
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(frame: makeFrame(),
                                   drawable: makeDrawable(width: 800, height: 600))
        XCTAssertEqual(u.drawableSize, SIMD2<Float>(800, 600))
    }

    // MARK: - Mode flags

    func testInterlacedFlagPropagates() {
        let style = DebugDisplayStyle()
        let progressive = style.makeUniforms(frame: makeFrame(interlaced: false),
                                             drawable: makeDrawable())
        let interlaced = style.makeUniforms(frame: makeFrame(interlaced: true),
                                            drawable: makeDrawable())
        XCTAssertEqual(progressive.interlaced, 0)
        XCTAssertEqual(interlaced.interlaced, 1)
    }

    func testParScalePropagates() {
        let style = DebugDisplayStyle()
        let authentic = style.makeUniforms(frame: makeFrame(),
                                           drawable: makeDrawable(parScale: 0.96))
        let crisp = style.makeUniforms(frame: makeFrame(),
                                       drawable: makeDrawable(parScale: 1.0))
        XCTAssertEqual(authentic.parScale, 0.96)
        XCTAssertEqual(crisp.parScale, 1.0)
    }

    // MARK: - Display Regions

    func testSingleRegionPacksIntoFirstSlot() {
        let style = DebugDisplayStyle()
        let regions = [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)]
        let u = style.makeUniforms(frame: makeFrame(regions: regions),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.regionCount, 1)
        XCTAssertEqual(u.regions.0.startLine, 0)
        XCTAssertEqual(u.regions.0.endLine, 256)
        XCTAssertEqual(u.regions.0.pixelWidth, 320)
    }

    func testTwoRegionsForSplitScreenLayout() {
        // Elite-style split: 320 px upper band, 160 px lower (dashboard) band.
        let style = DebugDisplayStyle()
        let regions = [
            DisplayRegion(startLine: 0, endLine: 192, pixelWidth: 320),
            DisplayRegion(startLine: 192, endLine: 248, pixelWidth: 160)
        ]
        let u = style.makeUniforms(frame: makeFrame(regions: regions),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.regionCount, 2)
        XCTAssertEqual(u.regions.0.pixelWidth, 320)
        XCTAssertEqual(u.regions.1.startLine, 192)
        XCTAssertEqual(u.regions.1.endLine, 248)
        XCTAssertEqual(u.regions.1.pixelWidth, 160)
    }

    func testEmptyRegionsLeaveZeroCountAndZeroedSlots() {
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(frame: makeFrame(regions: []),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.regionCount, 0)
        XCTAssertEqual(u.regions.0.startLine, 0)
        XCTAssertEqual(u.regions.0.endLine, 0)
        XCTAssertEqual(u.regions.0.pixelWidth, 0)
    }

    func testMoreRegionsThanMaxAreClampedToMax() {
        // The shader has a fixed-size regions array; producing more than that
        // would silently overflow the uniform buffer.
        let style = DebugDisplayStyle()
        let regions = (0..<(maxDisplayRegions + 2)).map {
            DisplayRegion(startLine: $0 * 10, endLine: ($0 + 1) * 10,
                          pixelWidth: 320 + $0)
        }
        let u = style.makeUniforms(frame: makeFrame(regions: regions),
                                   drawable: makeDrawable())
        XCTAssertEqual(u.regionCount, UInt32(maxDisplayRegions))
        // Last packed region should be index 7, pixelWidth 320 + 7 = 327
        XCTAssertEqual(u.regions.7.pixelWidth, 327)
    }

    // MARK: - Border colours

    func testDebugBorderColoursAreNonZero() {
        // Sanity: Debug style must paint visible borders, not transparent ones.
        let style = DebugDisplayStyle()
        let u = style.makeUniforms(frame: makeFrame(), drawable: makeDrawable())
        XCTAssertGreaterThan(u.leftBorderColor.x, 0,
                             "Left border should have a non-zero red channel (dark red)")
        XCTAssertGreaterThan(u.rightBorderColor.y, 0,
                             "Right border should have a non-zero green channel (dark green)")
        XCTAssertGreaterThan(u.topBorderColor.z, 0,
                             "Top border should have a non-zero blue channel (dark blue)")
        XCTAssertGreaterThan(u.bottomBorderColor.x, 0,
                             "Bottom border should have a non-zero red channel (dark yellow)")
        XCTAssertGreaterThan(u.bottomBorderColor.y, 0,
                             "Bottom border should have a non-zero green channel (dark yellow)")
        // All borders should be fully opaque
        XCTAssertEqual(u.leftBorderColor.w, 1.0)
        XCTAssertEqual(u.rightBorderColor.w, 1.0)
        XCTAssertEqual(u.topBorderColor.w, 1.0)
        XCTAssertEqual(u.bottomBorderColor.w, 1.0)
    }

    func testCustomisedBorderColoursPropagateIntoUniforms() {
        // Future UI may tweak the four debug colours; verify changes propagate.
        let style = DebugDisplayStyle()
        let cyan = SIMD4<Float>(0, 1, 1, 1)
        style.leftBorderColor = cyan
        let u = style.makeUniforms(frame: makeFrame(), drawable: makeDrawable())
        XCTAssertEqual(u.leftBorderColor, cyan)
        // Other colours retain their defaults
        XCTAssertEqual(u.rightBorderColor, SIMD4<Float>(0.0, 0.5, 0.0, 1.0))
    }

    // MARK: - Determinism

    func testRepeatedCallsProduceIdenticalUniforms() {
        // makeUniforms must be a pure function of its inputs.
        let style = DebugDisplayStyle()
        let frame = makeFrame()
        let drawable = makeDrawable()
        let a = style.makeUniforms(frame: frame, drawable: drawable)
        let b = style.makeUniforms(frame: frame, drawable: drawable)
        XCTAssertEqual(a.totalSize, b.totalSize)
        XCTAssertEqual(a.displaySize, b.displaySize)
        XCTAssertEqual(a.textureSize, b.textureSize)
        XCTAssertEqual(a.borderOffset, b.borderOffset)
        XCTAssertEqual(a.parScale, b.parScale)
        XCTAssertEqual(a.interlaced, b.interlaced)
        XCTAssertEqual(a.regionCount, b.regionCount)
    }
}
