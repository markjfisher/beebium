// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

final class MemoryConfigurationStateTests: XCTestCase {

    /// A stock Model B: four aliased sockets, BASIC default in slot 15.
    private func modelBSchema() -> SidewaysSchemaSection {
        SidewaysSchemaSection(
            type: "sideways_bank",
            hasAliasing: true,
            sockets: [
                SidewaysSocketSchema(label: "IC52",  slots: [0, 4, 8, 12],  capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
                SidewaysSocketSchema(label: "IC88",  slots: [1, 5, 9, 13],  capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
                SidewaysSocketSchema(label: "IC100", slots: [2, 6, 10, 14], capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
                SidewaysSocketSchema(label: "IC101", slots: [3, 7, 11, 15], capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
            ],
            defaultRoms: [
                SidewaysDefaultRom(slot: 15, image: "bbc-basic_2.rom", role: "language")
            ])
    }

    func testResolvesPresetDefaultAndEmptySockets() {
        let state = MemoryConfigurationState()
        // Disc preset: DFS in slot 14.
        let presetSlots = [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")]
        state.configure(schema: modelBSchema(), presetSlots: presetSlots)

        // Ordered by priority (highest slot) first: IC101(15), IC100(14), IC88(13), IC52(12).
        XCTAssertEqual(state.sockets.map(\.label), ["IC101", "IC100", "IC88", "IC52"])
        XCTAssertEqual(state.sockets.map(\.priority), [15, 14, 13, 12])

        // IC101: BASIC from the machine default.
        XCTAssertEqual(state.sockets[0].content, .rom(image: "bbc-basic_2.rom"))
        XCTAssertEqual(state.sockets[0].sourceSlot, 15)
        // IC100: DFS from the preset.
        XCTAssertEqual(state.sockets[1].content, .rom(image: "acorn-dfs_2_26.rom"))
        XCTAssertEqual(state.sockets[1].sourceSlot, 14)
        // IC88, IC52: empty (no preset, no default).
        XCTAssertEqual(state.sockets[2].content, .empty)
        XCTAssertNil(state.sockets[2].sourceSlot)
        XCTAssertEqual(state.sockets[3].content, .empty)

        XCTAssertFalse(state.hasChanges)
        XCTAssertTrue(state.sidewaysLaunchArguments().isEmpty)
    }

    func testChangingSocketEmitsSidewaysAtSourceSlot() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(),
                        presetSlots: [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")])

        // Remove DFS (IC100). It must emit at slot 14 (the preset's slot) so the
        // server overrides that exact slot rather than conflicting via aliasing.
        state.sockets[1].content = .empty
        XCTAssertTrue(state.sockets[1].isChanged)
        XCTAssertTrue(state.hasChanges)
        XCTAssertEqual(state.sidewaysLaunchArguments(), ["--sideways", "14:empty"])
    }

    func testChangingEmptySocketEmitsAtPrioritySlot() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(), presetSlots: [])

        // IC52 was empty (priority 12). Load a ROM file into it.
        let ic52 = state.sockets.firstIndex { $0.label == "IC52" }!
        state.sockets[ic52].content = .rom(image: "/tmp/toolkit.rom")
        XCTAssertEqual(state.sidewaysLaunchArguments(), ["--sideways", "12:rom:/tmp/toolkit.rom"])
    }

    func testRevertAllRestoresInitialContent() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(),
                        presetSlots: [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")])

        state.sockets[1].content = .empty
        state.sockets[0].content = .ram(imageFilepath: nil)
        XCTAssertTrue(state.hasChanges)

        state.revertAll()
        XCTAssertFalse(state.hasChanges)
        XCTAssertEqual(state.sockets[1].content, .rom(image: "acorn-dfs_2_26.rom"))
        XCTAssertEqual(state.sockets[0].content, .rom(image: "bbc-basic_2.rom"))
    }
}
