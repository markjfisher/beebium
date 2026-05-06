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

/// Tests for VideoSettings.loadFromUserDefaults using a private UserDefaults
/// suite so the host's preferences are not mutated.
@MainActor
final class VideoSettingsLoadTests: XCTestCase {
    private var defaults: UserDefaults!
    private var suiteName: String!

    override func setUp() async throws {
        try await super.setUp()
        // Use a unique suite per test to guarantee isolation.
        suiteName = "video-settings-test-\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
        XCTAssertNotNil(defaults)
    }

    override func tearDown() async throws {
        defaults.removePersistentDomain(forName: suiteName)
        defaults = nil
        suiteName = nil
        try await super.tearDown()
    }

    // MARK: - Empty defaults

    func testEmptyDefaultsProduceBuiltInFallbacks() {
        // No keys set in the suite - should yield Standard + Authentic.
        let settings = VideoSettings.loadFromUserDefaults(defaults)
        XCTAssertEqual(settings.activeStyleID, "standard")
        XCTAssertEqual(settings.pixelShape, .authentic)
    }

    // MARK: - Stored values

    func testStoredStyleIDIsRespected() {
        defaults.set("debug", forKey: VideoSettings.defaultStyleIDKey)
        let settings = VideoSettings.loadFromUserDefaults(defaults)
        XCTAssertEqual(settings.activeStyleID, "debug")
    }

    func testStoredPixelShapeIsRespected() {
        defaults.set(PixelShape.crisp.rawValue, forKey: VideoSettings.defaultPixelShapeKey)
        let settings = VideoSettings.loadFromUserDefaults(defaults)
        XCTAssertEqual(settings.pixelShape, .crisp)
    }

    func testBothKeysCombineCorrectly() {
        defaults.set("debug", forKey: VideoSettings.defaultStyleIDKey)
        defaults.set(PixelShape.crisp.rawValue, forKey: VideoSettings.defaultPixelShapeKey)
        let settings = VideoSettings.loadFromUserDefaults(defaults)
        XCTAssertEqual(settings.activeStyleID, "debug")
        XCTAssertEqual(settings.pixelShape, .crisp)
    }

    // MARK: - Recovery from bad data

    func testUnknownStyleIDFallsBackToFirstAvailable() {
        // Persisted value from a future or stale build that we no longer
        // recognise. Must not crash; must fall back gracefully.
        defaults.set("kaleidoscope", forKey: VideoSettings.defaultStyleIDKey)
        let settings = VideoSettings.loadFromUserDefaults(defaults)
        // First style in the default list is Standard.
        XCTAssertEqual(settings.activeStyleID, "standard")
    }

    func testUnknownPixelShapeFallsBackToAuthentic() {
        defaults.set("hyperreal", forKey: VideoSettings.defaultPixelShapeKey)
        let settings = VideoSettings.loadFromUserDefaults(defaults)
        XCTAssertEqual(settings.pixelShape, .authentic)
    }

    func testEmptyStringPixelShapeFallsBackToAuthentic() {
        defaults.set("", forKey: VideoSettings.defaultPixelShapeKey)
        let settings = VideoSettings.loadFromUserDefaults(defaults)
        XCTAssertEqual(settings.pixelShape, .authentic)
    }
}
