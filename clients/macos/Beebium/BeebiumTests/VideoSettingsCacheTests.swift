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
final class VideoSettingsCacheTests: XCTestCase {

    private let machineA = "11111111-1111-1111-1111-111111111111"
    private let machineB = "22222222-2222-2222-2222-222222222222"

    private func makeSnapshot(
        styleID: String = "standard",
        pixelShape: PixelShape = .authentic,
        backgroundHex: String = "#262626"
    ) -> VideoSettingsSnapshot {
        VideoSettingsSnapshot(
            activeStyleID: styleID,
            pixelShapeRaw: pixelShape.rawValue,
            windowBackgroundHex: backgroundHex
        )
    }

    // MARK: - Empty cache

    func testEmptyCacheReturnsNil() {
        let cache = VideoSettingsCache()
        XCTAssertNil(cache.snapshot(forMachineUUID: machineA))
        XCTAssertEqual(cache.count, 0)
    }

    // MARK: - Save and retrieve

    func testSaveThenSnapshotReturnsTheSavedValue() {
        let cache = VideoSettingsCache()
        let snapshot = makeSnapshot(styleID: "debug", pixelShape: .crisp)
        cache.save(snapshot, forMachineUUID: machineA)
        XCTAssertEqual(cache.snapshot(forMachineUUID: machineA), snapshot)
        XCTAssertEqual(cache.count, 1)
    }

    func testSavingTheSameUUIDReplacesTheSnapshot() {
        let cache = VideoSettingsCache()
        cache.save(makeSnapshot(styleID: "standard"), forMachineUUID: machineA)
        cache.save(makeSnapshot(styleID: "debug"), forMachineUUID: machineA)
        XCTAssertEqual(cache.snapshot(forMachineUUID: machineA)?.activeStyleID, "debug")
        XCTAssertEqual(cache.count, 1)
    }

    // MARK: - UUID isolation

    func testDifferentUUIDsAreIndependent() {
        let cache = VideoSettingsCache()
        let aSnapshot = makeSnapshot(styleID: "standard")
        let bSnapshot = makeSnapshot(styleID: "debug", backgroundHex: "#102030")
        cache.save(aSnapshot, forMachineUUID: machineA)
        cache.save(bSnapshot, forMachineUUID: machineB)
        XCTAssertEqual(cache.snapshot(forMachineUUID: machineA), aSnapshot)
        XCTAssertEqual(cache.snapshot(forMachineUUID: machineB), bSnapshot)
        XCTAssertEqual(cache.count, 2)
    }

    // MARK: - Empty UUID handling

    func testEmptyUUIDSaveIsIgnored() {
        // Pre-connection state has an empty machineUUID; the cache must not
        // accumulate entries against the empty key.
        let cache = VideoSettingsCache()
        cache.save(makeSnapshot(), forMachineUUID: "")
        XCTAssertEqual(cache.count, 0)
    }

    func testEmptyUUIDLookupReturnsNil() {
        let cache = VideoSettingsCache()
        cache.save(makeSnapshot(), forMachineUUID: machineA)
        XCTAssertNil(cache.snapshot(forMachineUUID: ""))
    }

    // MARK: - Clear

    func testClearRemovesAllEntries() {
        let cache = VideoSettingsCache()
        cache.save(makeSnapshot(), forMachineUUID: machineA)
        cache.save(makeSnapshot(), forMachineUUID: machineB)
        cache.clear()
        XCTAssertEqual(cache.count, 0)
        XCTAssertNil(cache.snapshot(forMachineUUID: machineA))
        XCTAssertNil(cache.snapshot(forMachineUUID: machineB))
    }

    // MARK: - VideoSettings round-trip

    func testSnapshotCapturesCurrentVideoSettings() {
        let settings = VideoSettings(
            initialStyleID: "debug",
            initialPixelShape: .crisp,
            initialWindowBackground: Color(sRGBHex: "#A0B0C0")!
        )
        let snapshot = settings.makeSnapshot()
        XCTAssertEqual(snapshot.activeStyleID, "debug")
        XCTAssertEqual(snapshot.pixelShapeRaw, "crisp")
        XCTAssertEqual(snapshot.windowBackgroundHex, "#A0B0C0")
    }

    func testApplySnapshotRestoresAllFields() {
        let settings = VideoSettings()  // Defaults
        let snapshot = VideoSettingsSnapshot(
            activeStyleID: "debug",
            pixelShapeRaw: "crisp",
            windowBackgroundHex: "#102030"
        )
        settings.apply(snapshot)
        XCTAssertEqual(settings.activeStyleID, "debug")
        XCTAssertEqual(settings.pixelShape, .crisp)
        XCTAssertEqual(settings.windowBackground.sRGBHex, "#102030")
    }

    func testApplyIgnoresUnknownStyleID() {
        // Defensive against snapshots saved by a future build that introduced
        // a style we no longer ship. Falls back to keeping the current style.
        let settings = VideoSettings()
        let originalStyleID = settings.activeStyleID
        let snapshot = VideoSettingsSnapshot(
            activeStyleID: "kaleidoscope",
            pixelShapeRaw: "authentic",
            windowBackgroundHex: "#262626"
        )
        settings.apply(snapshot)
        XCTAssertEqual(settings.activeStyleID, originalStyleID)
    }

    func testApplyIgnoresUnknownPixelShape() {
        let settings = VideoSettings()
        let originalShape = settings.pixelShape
        let snapshot = VideoSettingsSnapshot(
            activeStyleID: settings.activeStyleID,
            pixelShapeRaw: "hyperreal",
            windowBackgroundHex: "#262626"
        )
        settings.apply(snapshot)
        XCTAssertEqual(settings.pixelShape, originalShape)
    }

    func testApplyIgnoresMalformedBackgroundHex() {
        let settings = VideoSettings()
        let originalHex = settings.windowBackground.sRGBHex
        let snapshot = VideoSettingsSnapshot(
            activeStyleID: settings.activeStyleID,
            pixelShapeRaw: settings.pixelShape.rawValue,
            windowBackgroundHex: "not a colour"
        )
        settings.apply(snapshot)
        XCTAssertEqual(settings.windowBackground.sRGBHex, originalHex)
    }

    func testRoundTripPreservesAllFields() {
        let original = VideoSettings(
            initialStyleID: "debug",
            initialPixelShape: .crisp,
            initialWindowBackground: Color(sRGBHex: "#FFC0CB")!
        )
        let snapshot = original.makeSnapshot()

        let restored = VideoSettings()  // starts from defaults
        restored.apply(snapshot)

        XCTAssertEqual(restored.activeStyleID, original.activeStyleID)
        XCTAssertEqual(restored.pixelShape, original.pixelShape)
        XCTAssertEqual(restored.windowBackground.sRGBHex, original.windowBackground.sRGBHex)
    }
}
