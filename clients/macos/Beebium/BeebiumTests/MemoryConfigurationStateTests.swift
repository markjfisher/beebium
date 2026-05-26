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

    private typealias Content = MemoryConfigurationState.SocketContent

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
        let presetSlots = [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")]
        state.configure(schema: modelBSchema(), presetSlots: presetSlots)

        // Ordered by priority (highest slot) first.
        XCTAssertEqual(state.sockets.map(\.label), ["IC101", "IC100", "IC88", "IC52"])
        XCTAssertEqual(state.sockets.map(\.priority), [15, 14, 13, 12])

        XCTAssertEqual(state.sockets[0].content, Content(kind: .rom, image: "bbc-basic_2.rom"))
        XCTAssertEqual(state.sockets[0].sourceSlot, 15)
        XCTAssertEqual(state.sockets[1].content, Content(kind: .rom, image: "acorn-dfs_2_26.rom"))
        XCTAssertEqual(state.sockets[1].sourceSlot, 14)
        XCTAssertEqual(state.sockets[2].content, .empty)
        XCTAssertNil(state.sockets[2].sourceSlot)

        XCTAssertFalse(state.hasChanges)
        XCTAssertTrue(state.sidewaysLaunchArguments().isEmpty)
    }

    func testChangingSocketEmitsSidewaysAtSourceSlot() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(),
                        presetSlots: [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")])

        // Remove DFS (IC100): must emit at slot 14 (the preset's slot).
        state.sockets[1].content = .empty
        XCTAssertTrue(state.hasChanges)
        XCTAssertEqual(state.sidewaysLaunchArguments(), ["--sideways", "14:empty"])
    }

    func testLoadingRomIntoEmptySocketEmitsAtPrioritySlot() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(), presetSlots: [])

        // IC52 was empty (priority 12). Make it a ROM from a file.
        let ic52 = state.sockets.firstIndex { $0.label == "IC52" }!
        state.sockets[ic52].content = Content(kind: .rom, image: "/tmp/toolkit.rom")
        XCTAssertEqual(state.sidewaysLaunchArguments(), ["--sideways", "12:rom:/tmp/toolkit.rom"])
    }

    func testSidewaysRamWithAndWithoutPreload() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(), presetSlots: [])
        let ic52 = state.sockets.firstIndex { $0.label == "IC52" }!

        // Blank sideways RAM.
        state.sockets[ic52].content = Content(kind: .ram, image: nil)
        XCTAssertEqual(state.sidewaysLaunchArguments(), ["--sideways", "12:ram"])

        // RAM pre-loaded from an image at startup (e.g. battery-backed RAM).
        state.sockets[ic52].content = Content(kind: .ram, image: "/tmp/under-test.rom")
        XCTAssertEqual(state.sidewaysLaunchArguments(), ["--sideways", "12:ram:/tmp/under-test.rom"])
    }

    func testRomKindWithoutImageIsNotEmitted() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(), presetSlots: [])
        let ic52 = state.sockets.firstIndex { $0.label == "IC52" }!

        // Selecting ROM but choosing no image is incomplete: emit nothing so the
        // preset/default for the slot still applies.
        state.sockets[ic52].content = Content(kind: .rom, image: nil)
        XCTAssertTrue(state.sockets[ic52].isChanged)
        XCTAssertTrue(state.sidewaysLaunchArguments().isEmpty)
    }

    func testRevertAllRestoresInitialContent() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(),
                        presetSlots: [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")])

        state.sockets[1].content = .empty
        state.sockets[0].content = Content(kind: .ram, image: nil)
        XCTAssertTrue(state.hasChanges)

        state.revertAll()
        XCTAssertFalse(state.hasChanges)
        XCTAssertEqual(state.sockets[1].content, Content(kind: .rom, image: "acorn-dfs_2_26.rom"))
        XCTAssertEqual(state.sockets[0].content, Content(kind: .rom, image: "bbc-basic_2.rom"))
    }

    func testDisplayTextPrefersResolvedRomTitle() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(),
                        presetSlots: [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")])

        // IC100 holds the DFS image; before resolution, show the filename.
        XCTAssertEqual(state.sockets[1].displayText, "acorn-dfs_2_26.rom")
        // Once the header is parsed, show the title and version.
        state.sockets[1].resolvedTitle = "DFS"
        state.sockets[1].resolvedVersion = "2.26"
        XCTAssertEqual(state.sockets[1].displayText, "DFS 2.26")
        // An empty socket always reads "Empty".
        XCTAssertEqual(state.sockets[3].displayText, "Empty")
    }

    func testDisplayTextForRamWithPreloadedRomTitle() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(), presetSlots: [])
        let ic52 = state.sockets.firstIndex { $0.label == "IC52" }!

        // The RAM/ROM distinction is shown by the dropdown, so the contents
        // field just shows the image like a ROM: filename, then title.
        state.sockets[ic52].content = Content(kind: .ram, image: "/tmp/dev.rom")
        XCTAssertEqual(state.sockets[ic52].displayText, "dev.rom")
        state.sockets[ic52].resolvedTitle = "MYROM"
        state.sockets[ic52].resolvedVersion = "1.0"
        XCTAssertEqual(state.sockets[ic52].displayText, "MYROM 1.0")
    }

    func testMoveContentsReordersPayloadsAndLaunchArgs() {
        let state = MemoryConfigurationState()
        state.configure(schema: modelBSchema(),
                        presetSlots: [PresetSidewaysSlot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")])
        // Order by priority: IC101(BASIC), IC100(DFS), IC88(empty), IC52(empty).
        // Drag DFS (index 1) to the bottom (lowest priority).
        state.moveContents(fromOffsets: IndexSet(integer: 1), toOffset: 4)

        // Socket identities stay in priority order; only the contents move.
        XCTAssertEqual(state.sockets.map(\.label), ["IC101", "IC100", "IC88", "IC52"])
        XCTAssertEqual(state.sockets[0].content, Content(kind: .rom, image: "bbc-basic_2.rom"))
        XCTAssertEqual(state.sockets[1].content, .empty)                                  // IC100 lost DFS
        XCTAssertEqual(state.sockets[3].content, Content(kind: .rom, image: "acorn-dfs_2_26.rom"))  // IC52 gained it

        // Launch: IC100 emits empty at slot 14; IC52 emits DFS at slot 12.
        let args = state.sidewaysLaunchArguments()
        XCTAssertTrue(args.contains("14:empty"))
        XCTAssertTrue(args.contains("12:rom:acorn-dfs_2_26.rom"))
    }

    func testRomHeaderInfoDecodesDescribeRomOutput() throws {
        let json = """
        {
          "recognised": true, "title": "DFS", "version": "2.26",
          "copyright": "(C)1985 Acorn", "is_language": false, "is_service_only": true
        }
        """
        let info = try JSONDecoder().decode(RomHeaderInfo.self, from: Data(json.utf8))
        XCTAssertTrue(info.recognised)
        XCTAssertEqual(info.title, "DFS")
        XCTAssertEqual(info.version, "2.26")
        XCTAssertTrue(info.isServiceOnly)
        XCTAssertFalse(info.isLanguage)
    }
}
