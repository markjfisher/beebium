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

import AppKit
import SwiftUI
import XCTest
@testable import Beebium

final class ColorExtensionsTests: XCTestCase {

    private func sRGBComponents(of color: Color) -> (CGFloat, CGFloat, CGFloat, CGFloat) {
        let nsColor = NSColor(color).usingColorSpace(.sRGB) ?? .black
        return (nsColor.redComponent, nsColor.greenComponent,
                nsColor.blueComponent, nsColor.alphaComponent)
    }

    // MARK: - Hex parsing

    func testParseSixDigitHexWithHash() {
        let color = Color(sRGBHex: "#FF8000")
        XCTAssertNotNil(color)
        let (r, g, b, a) = sRGBComponents(of: color!)
        XCTAssertEqual(r, 1.0, accuracy: 1e-3)
        XCTAssertEqual(g, 128.0 / 255.0, accuracy: 1e-3)
        XCTAssertEqual(b, 0.0, accuracy: 1e-3)
        XCTAssertEqual(a, 1.0, accuracy: 1e-3)
    }

    func testParseSixDigitHexWithoutHash() {
        let color = Color(sRGBHex: "00AAFF")
        XCTAssertNotNil(color)
        let (r, g, b, _) = sRGBComponents(of: color!)
        XCTAssertEqual(r, 0.0, accuracy: 1e-3)
        XCTAssertEqual(g, 170.0 / 255.0, accuracy: 1e-3)
        XCTAssertEqual(b, 1.0, accuracy: 1e-3)
    }

    func testParseEightDigitHexUsesAlpha() {
        let color = Color(sRGBHex: "#FF000080")
        XCTAssertNotNil(color)
        let (r, _, _, a) = sRGBComponents(of: color!)
        XCTAssertEqual(r, 1.0, accuracy: 1e-3)
        XCTAssertEqual(a, 128.0 / 255.0, accuracy: 1e-3)
    }

    func testParseRejectsThreeDigitHex() {
        // We don't accept the abbreviated #FFF form because it adds parsing
        // surface area for no real-world benefit.
        XCTAssertNil(Color(sRGBHex: "#FFF"))
    }

    func testParseRejectsNonHexCharacters() {
        XCTAssertNil(Color(sRGBHex: "#GGHHII"))
    }

    func testParseRejectsEmptyString() {
        XCTAssertNil(Color(sRGBHex: ""))
    }

    func testParseTrimsWhitespace() {
        let color = Color(sRGBHex: "  #112233\n")
        XCTAssertNotNil(color)
    }

    // MARK: - Hex formatting

    func testHexFormatsAsSixUppercaseDigitsWithHash() {
        let color = Color(.sRGB, red: 1.0, green: 128.0/255.0, blue: 0.0, opacity: 1.0)
        XCTAssertEqual(color.sRGBHex, "#FF8000")
    }

    func testHexDropsAlpha() {
        let color = Color(.sRGB, red: 1.0, green: 0.0, blue: 0.0, opacity: 0.5)
        XCTAssertEqual(color.sRGBHex, "#FF0000")
    }

    func testHexRoundTripStability() {
        // Picking a value and round-tripping should produce a stable hex
        // representation (within 1/255 precision).
        let original = "#262626"
        let parsed = Color(sRGBHex: original)!
        XCTAssertEqual(parsed.sRGBHex, original)
    }

    func testBlackAndWhiteRoundTrip() {
        XCTAssertEqual(Color(sRGBHex: "#000000")!.sRGBHex, "#000000")
        XCTAssertEqual(Color(sRGBHex: "#FFFFFF")!.sRGBHex, "#FFFFFF")
    }

    // MARK: - MTLClearColor conversion

    func testClearColorFromOpaqueRed() {
        let color = Color(.sRGB, red: 1.0, green: 0.0, blue: 0.0, opacity: 1.0)
        let mtl = color.mtlClearColor
        XCTAssertEqual(mtl.red, 1.0, accuracy: 1e-3)
        XCTAssertEqual(mtl.green, 0.0, accuracy: 1e-3)
        XCTAssertEqual(mtl.blue, 0.0, accuracy: 1e-3)
        XCTAssertEqual(mtl.alpha, 1.0, accuracy: 1e-3)
    }

    func testClearColorPreservesDarkGreyDefault() {
        // The dark-grey default (#262626 ~= 0.149) is the value that shipped
        // before this setting was configurable. New users should see the
        // same background out of the box.
        let color = Color(sRGBHex: "#262626")!
        let mtl = color.mtlClearColor
        XCTAssertEqual(mtl.red, 38.0 / 255.0, accuracy: 1e-3)
        XCTAssertEqual(mtl.green, 38.0 / 255.0, accuracy: 1e-3)
        XCTAssertEqual(mtl.blue, 38.0 / 255.0, accuracy: 1e-3)
    }

    func testClearColorRoundTripFromHex() {
        let mtl = Color(sRGBHex: "#A0B0C0")!.mtlClearColor
        XCTAssertEqual(mtl.red, 0xA0 / 255.0, accuracy: 1e-3)
        XCTAssertEqual(mtl.green, 0xB0 / 255.0, accuracy: 1e-3)
        XCTAssertEqual(mtl.blue, 0xC0 / 255.0, accuracy: 1e-3)
    }

    // MARK: - SIMD4 packing

    func testSimd4PreservesSRGBComponents() {
        let color = Color(.sRGB, red: 0.25, green: 0.5, blue: 0.75, opacity: 1.0)
        let v = color.simd4
        XCTAssertEqual(v.x, 0.25, accuracy: 1e-3)
        XCTAssertEqual(v.y, 0.5, accuracy: 1e-3)
        XCTAssertEqual(v.z, 0.75, accuracy: 1e-3)
        XCTAssertEqual(v.w, 1.0, accuracy: 1e-3)
    }

    func testSimd4OpaqueBlack() {
        let v = Color(sRGBHex: "#000000")!.simd4
        XCTAssertEqual(v.x, 0.0, accuracy: 1e-3)
        XCTAssertEqual(v.y, 0.0, accuracy: 1e-3)
        XCTAssertEqual(v.z, 0.0, accuracy: 1e-3)
        XCTAssertEqual(v.w, 1.0, accuracy: 1e-3)
    }
}
