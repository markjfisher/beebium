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

import Foundation
import GRPC

/// Live sideways-memory state for the running emulator, fed by
/// `SidewaysService.GetSlotStatus` and refreshed on `SubscribeEvents`.
@MainActor
final class SidewaysClient: ObservableObject, Disconnectable {

    enum SocketKind: Sendable { case empty, rom, ram }

    /// Parsed sideways ROM header for a slot's current contents, when one is
    /// recognised. Mirrors the gRPC RomHeader message and the describe-rom
    /// JSON (in the configurator path).
    struct RomHeader: Sendable, Equatable {
        let title: String
        let version: String
        let copyright: String
        let containsRomfs: Bool
        let kinds: [String]
    }

    /// One physical socket on the running machine.
    struct Socket: Identifiable, Sendable, Equatable {
        let socketIndex: UInt32
        let label: String
        let slots: [Int]
        let kind: SocketKind
        let populated: Bool
        let imageName: String
        let romHeader: RomHeader?

        var id: UInt32 { socketIndex }
        /// Effective boot priority: the highest slot the socket answers.
        var priority: Int { slots.max() ?? 0 }
    }

    @Published private(set) var sockets: [Socket] = []
    @Published private(set) var selectedBank: UInt32 = 0
    @Published private(set) var hasAliasing: Bool = false
    @Published private(set) var isLoaded: Bool = false
    @Published private(set) var errorMessage: String?

    private var client: Beebium_SidewaysServiceNIOClient?
    private var subscriptionTask: Task<Void, Never>?

    /// Connect using an existing gRPC channel (same pattern as the other clients).
    /// Fetches GetSlotStatus once and then subscribes to SubscribeEvents, so the
    /// live view refreshes when a slot is reconfigured.
    func connect(channel: GRPCChannel) {
        client = Beebium_SidewaysServiceNIOClient(channel: channel)
        subscriptionTask = Task { [weak self] in
            await self?.fetchSlotStatus()
            await self?.subscribeEvents()
        }
    }

    func disconnect() {
        subscriptionTask?.cancel()
        subscriptionTask = nil
        client = nil
        sockets = []
        selectedBank = 0
        hasAliasing = false
        isLoaded = false
        errorMessage = nil
    }

    // MARK: - Fetch + subscribe

    private func fetchSlotStatus() async {
        guard let client = client else { return }
        do {
            let response = try await client.getSlotStatus(.init()).response.get()
            let updated = response.sockets.map(Self.mapSocket)
            await MainActor.run {
                // Highest priority (highest slot) first - mirrors the MOS scan
                // and the New Machine dialog's Memory tab ordering.
                self.sockets = updated.sorted { $0.priority > $1.priority }
                self.selectedBank = response.selectedBank
                self.hasAliasing = response.hasAliasing_p
                self.isLoaded = true
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Sideways status failed: \(error.localizedDescription)"
            }
        }
    }

    private func subscribeEvents() async {
        guard let client = client else { return }
        var request = Beebium_SubscribeEventsRequest()
        request.minIntervalMs = 100

        let call = client.subscribeEvents(request) { [weak self] event in
            Task { @MainActor [weak self] in
                self?.handleEvent(event)
            }
        }
        do {
            _ = try await call.status.get()
        } catch {
            // Stream ended (disconnect or server shutdown). Not an error worth
            // surfacing - the host will tear us down via disconnect().
        }
    }

    private func handleEvent(_ event: Beebium_SidewaysEvent) {
        guard let payload = event.event else { return }
        switch payload {
        case .bankSelected(let bs):
            selectedBank = bs.bank
        case .slotConfigured:
            // A slot was reconfigured via ConfigureSlot: re-fetch so the
            // parsed rom_header reflects the new contents.
            Task { [weak self] in await self?.fetchSlotStatus() }
        }
    }

    // MARK: - Proto -> Swift mapping

    private static func mapSocket(_ status: Beebium_SocketStatus) -> Socket {
        Socket(
            socketIndex: status.socketIndex,
            label: status.socketLabel,
            slots: status.aliasedSlots.map(Int.init),
            kind: mapKind(status.type),
            populated: status.populated,
            imageName: status.imageName,
            romHeader: status.hasRomHeader ? mapHeader(status.romHeader) : nil)
    }

    private static func mapKind(_ type: Beebium_SidewaysSlotType) -> SocketKind {
        switch type {
        case .rom: return .rom
        case .ram: return .ram
        default:   return .empty
        }
    }

    private static func mapHeader(_ h: Beebium_RomHeader) -> RomHeader? {
        guard h.recognised else { return nil }
        return RomHeader(
            title: h.title,
            version: h.version,
            copyright: h.copyright,
            containsRomfs: h.containsRomfs,
            kinds: h.kinds)
    }
}
