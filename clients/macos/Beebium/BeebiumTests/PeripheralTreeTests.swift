// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

final class PeripheralTreeTests: XCTestCase {

    // MARK: - helpers

    private func info(name: String,
                      id: String,
                      attachesTo: [String],
                      provides: [String] = [],
                      config: [String: String] = [:],
                      label: String? = nil,
                      description: String = "") -> Beebium_ExtensionInfo {
        var info = Beebium_ExtensionInfo()
        info.name = name
        info.id = id
        info.label = label ?? id
        info.description_p = description
        info.attachesTo = attachesTo
        info.provides = provides
        info.config = config
        return info
    }

    // MARK: - shape

    func testEmptyInputProducesEmptyTree() {
        let tree = PeripheralTree.build(from: [])
        XCTAssertTrue(tree.groups.isEmpty)
        XCTAssertTrue(tree.orphans.isEmpty)
    }

    func testSingleExtensionAttachesToBuiltInExtensionPoint() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let tree = PeripheralTree.build(from: [scsi])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].extensionPoint, "1mhz-bus")
        XCTAssertEqual(tree.groups[0].nodes.count, 1)
        XCTAssertEqual(tree.groups[0].nodes[0].id, "acorn-scsi#1")
        XCTAssertTrue(tree.groups[0].nodes[0].children.isEmpty)
        XCTAssertEqual(tree.groups[0].nodes[0].primaryAttachment, "1mhz-bus")
        XCTAssertTrue(tree.groups[0].nodes[0].secondaryAttachments.isEmpty)
    }

    // MARK: - nesting via provides/attachesTo

    func testChildNestsUnderProvidingParent() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let hdd = info(name: "scsi-hard-disc",
                       id: "scsi-hdd#0",
                       attachesTo: ["scsi"],
                       config: ["scsi-id": "0", "image": "/foo.dat"])

        let tree = PeripheralTree.build(from: [scsi, hdd])

        XCTAssertEqual(tree.groups.count, 1)
        let root = tree.groups[0].nodes[0]
        XCTAssertEqual(root.id, "acorn-scsi#1")
        XCTAssertEqual(root.children.count, 1)

        let child = root.children[0]
        XCTAssertEqual(child.id, "scsi-hdd#0")
        XCTAssertEqual(child.primaryAttachment, "scsi")
        XCTAssertEqual(child.config["scsi-id"], "0")
        XCTAssertEqual(child.config["image"], "/foo.dat")
    }

    func testMultipleChildrenPreserveInputOrder() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let hdd0 = info(name: "scsi-hard-disc",
                        id: "scsi-hdd#0",
                        attachesTo: ["scsi"],
                        config: ["scsi-id": "0"])
        let hdd1 = info(name: "scsi-hard-disc",
                        id: "scsi-hdd#1",
                        attachesTo: ["scsi"],
                        config: ["scsi-id": "1"])

        let tree = PeripheralTree.build(from: [scsi, hdd0, hdd1])
        let children = tree.groups[0].nodes[0].children
        XCTAssertEqual(children.map { $0.id }, ["scsi-hdd#0", "scsi-hdd#1"])
    }

    func testChildCanArriveBeforeParentInInput() {
        // The server returns extensions in topological order, but the
        // tree builder must not assume that -- a HDD listed before its
        // SCSI host adapter should still nest correctly.
        let hdd = info(name: "scsi-hard-disc",
                       id: "scsi-hdd#0",
                       attachesTo: ["scsi"])
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])

        let tree = PeripheralTree.build(from: [hdd, scsi])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].nodes.count, 1)
        XCTAssertEqual(tree.groups[0].nodes[0].id, "acorn-scsi#1")
        XCTAssertEqual(tree.groups[0].nodes[0].children.map { $0.id }, ["scsi-hdd#0"])
    }

    // MARK: - multi-attach (Voltmace-style)

    func testMultiAttachExtensionAppearsOnceUnderPrimary() {
        // Voltmace Delta 14B/1: plugs into both user port and analogue
        // port. The first entry in attachesTo is treated as primary, so
        // the device shows up exactly once under that group with the
        // remaining attachment(s) listed as secondary (badge).
        let voltmace = info(name: "voltmace-delta-14b1",
                            id: "voltmace#1",
                            attachesTo: ["user-port", "analogue-port"])

        let tree = PeripheralTree.build(from: [voltmace])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].extensionPoint, "user-port")
        XCTAssertEqual(tree.groups[0].nodes[0].primaryAttachment, "user-port")
        XCTAssertEqual(tree.groups[0].nodes[0].secondaryAttachments, ["analogue-port"])

        // No phantom second appearance under analogue-port.
        let totalRoots = tree.groups.reduce(0) { $0 + $1.nodes.count }
        XCTAssertEqual(totalRoots, 1)
    }

    // MARK: - multiple roots

    func testMultipleRootsAtDifferentExtensionPointsSortedAlphabetically() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let rtc = info(name: "acorn-rtc",
                       id: "acorn-rtc#1",
                       attachesTo: ["user-port"])

        let tree = PeripheralTree.build(from: [scsi, rtc])

        XCTAssertEqual(tree.groups.map { $0.extensionPoint },
                       ["1mhz-bus", "user-port"])
    }

    // MARK: - defensive

    func testExtensionAttachingToUnknownPointBecomesRootGroup() {
        // The server's registry refuses to init unsatisfied dependencies,
        // so this case is degenerate. But if it ever leaks through, the
        // extension must remain visible -- not silently dropped.
        let oddball = info(name: "weird",
                           id: "weird#1",
                           attachesTo: ["mystery-bus"])

        let tree = PeripheralTree.build(from: [oddball])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].extensionPoint, "mystery-bus")
        XCTAssertEqual(tree.groups[0].nodes[0].id, "weird#1")
    }

    func testExtensionWithEmptyAttachesToBecomesOrphan() {
        // PeripheralExtension's contract is non-empty attachesTo. If
        // that's ever violated, surface the extension under `orphans`
        // rather than indexing it under an empty group name.
        var oddball = Beebium_ExtensionInfo()
        oddball.name = "broken"
        oddball.id = "broken#1"
        oddball.label = "broken#1"
        oddball.attachesTo = []

        let tree = PeripheralTree.build(from: [oddball])

        XCTAssertTrue(tree.groups.isEmpty)
        XCTAssertEqual(tree.orphans.count, 1)
        XCTAssertEqual(tree.orphans[0].id, "broken#1")
    }
}
