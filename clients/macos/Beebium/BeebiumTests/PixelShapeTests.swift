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

final class PixelShapeTests: XCTestCase {

    func testAuthenticParScaleIs096() {
        // Matches the BBC PAL CRT geometry. Changing this constant changes
        // the displayed picture's authenticity, so any change should be a
        // deliberate decision and not an accident.
        XCTAssertEqual(PixelShape.authentic.parScale, 0.96, accuracy: 1e-6)
    }

    func testCrispParScaleIs1() {
        // Square pixels for clean integer scaling on modern displays.
        XCTAssertEqual(PixelShape.crisp.parScale, 1.0, accuracy: 1e-6)
    }

    func testRawValuesAreStable() {
        // Used as @AppStorage keys; changing them silently invalidates user
        // preferences from previous app versions.
        XCTAssertEqual(PixelShape.authentic.rawValue, "authentic")
        XCTAssertEqual(PixelShape.crisp.rawValue, "crisp")
    }

    func testAllCasesIncludesAuthenticAndCrisp() {
        let ids = PixelShape.allCases.map { $0.id }
        XCTAssertTrue(ids.contains("authentic"))
        XCTAssertTrue(ids.contains("crisp"))
        XCTAssertEqual(PixelShape.allCases.count, 2)
    }

    func testDisplayNamesAreUserFacing() {
        // Sanity check that the user-visible labels are not the raw values
        // (which are lowercase and look like code, not UI text).
        XCTAssertEqual(PixelShape.authentic.displayName, "Authentic")
        XCTAssertEqual(PixelShape.crisp.displayName, "Crisp")
    }

    func testRoundTripFromRawValue() {
        for shape in PixelShape.allCases {
            XCTAssertEqual(PixelShape(rawValue: shape.rawValue), shape)
        }
    }

    func testInvalidRawValueReturnsNil() {
        XCTAssertNil(PixelShape(rawValue: "stretchy"))
    }
}
