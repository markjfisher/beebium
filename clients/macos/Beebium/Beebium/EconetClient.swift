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

import Foundation
import GRPC

enum EconetError: LocalizedError {
    case notConnected
    case operationFailed(String)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "Not connected to server"
        case .operationFailed(let message):
            return message
        }
    }
}

@MainActor
final class EconetClient: ObservableObject, Disconnectable {

    @Published private(set) var hasEconetSocket: Bool = false
    @Published private(set) var enabled: Bool = false
    @Published private(set) var stationId: UInt32 = 0
    @Published private(set) var aunMode: Bool = false
    @Published private(set) var connected: Bool = false
    @Published private(set) var aunPort: UInt32 = 0
    @Published private(set) var peerCount: UInt32 = 0
    @Published private(set) var peers: [Beebium_EconetPeer] = []
    @Published private(set) var isLoaded: Bool = false
    @Published private(set) var errorMessage: String?

    private var client: Beebium_EconetServiceNIOClient?
    private var fetchTask: Task<Void, Never>?

    func connect(channel: GRPCChannel) {
        client = Beebium_EconetServiceNIOClient(channel: channel)
        fetchTask = Task { [weak self] in
            await self?.fetchStatusAndPeers()
        }
    }

    func disconnect() {
        fetchTask?.cancel()
        fetchTask = nil
        client = nil
        isLoaded = false
        hasEconetSocket = false
        enabled = false
        stationId = 0
        aunMode = false
        connected = false
        aunPort = 0
        peerCount = 0
        peers = []
        errorMessage = nil
    }

    // MARK: - Mutations

    func setStationId(_ newId: UInt32) async -> Result<Void, EconetError> {
        guard let client = client else { return .failure(.notConnected) }
        var request = Beebium_SetStationIdRequest()
        request.stationID = newId
        do {
            let response = try await client.setStationId(request).response.get()
            if response.success {
                await refreshStatus()
                return .success(())
            } else {
                return .failure(.operationFailed(response.error))
            }
        } catch {
            return .failure(.operationFailed(error.localizedDescription))
        }
    }

    func connectNetwork() async -> Result<Void, EconetError> {
        guard let client = client else { return .failure(.notConnected) }
        var request = Beebium_SetConnectedRequest()
        request.connected = true
        do {
            let response = try await client.setConnected(request).response.get()
            if response.success {
                await refreshStatus()
                return .success(())
            } else {
                return .failure(.operationFailed(response.error))
            }
        } catch {
            return .failure(.operationFailed(error.localizedDescription))
        }
    }

    func disconnectNetwork() async -> Result<Void, EconetError> {
        guard let client = client else { return .failure(.notConnected) }
        var request = Beebium_SetConnectedRequest()
        request.connected = false
        do {
            let response = try await client.setConnected(request).response.get()
            if response.success {
                await refreshStatus()
                return .success(())
            } else {
                return .failure(.operationFailed(response.error))
            }
        } catch {
            return .failure(.operationFailed(error.localizedDescription))
        }
    }

    // MARK: - Private

    private func fetchStatusAndPeers() async {
        guard let client = client else { return }

        do {
            let request = Beebium_GetEconetStatusRequest()
            let response = try await client.getEconetStatus(request).response.get()

            await MainActor.run {
                self.hasEconetSocket = response.hasEconetSocket_p
                self.enabled = response.enabled
                self.stationId = response.stationID
                self.aunMode = response.aunMode
                self.connected = response.connected
                self.aunPort = response.aunPort
                self.peerCount = response.peerCount
                self.isLoaded = true
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Failed to get Econet status: \(error.localizedDescription)"
                self.isLoaded = false
            }
            return
        }

        do {
            let request = Beebium_ListPeersRequest()
            let response = try await client.listPeers(request).response.get()
            await MainActor.run {
                self.peers = response.peers
            }
        } catch {
            NSLog("[EconetClient] Failed to list peers: %@", error.localizedDescription)
        }
    }

    private func refreshStatus() async {
        guard let client = client else { return }

        do {
            let request = Beebium_GetEconetStatusRequest()
            let response = try await client.getEconetStatus(request).response.get()

            await MainActor.run {
                self.hasEconetSocket = response.hasEconetSocket_p
                self.enabled = response.enabled
                self.stationId = response.stationID
                self.aunMode = response.aunMode
                self.connected = response.connected
                self.aunPort = response.aunPort
                self.peerCount = response.peerCount
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Failed to refresh: \(error.localizedDescription)"
            }
        }

        do {
            let request = Beebium_ListPeersRequest()
            let response = try await client.listPeers(request).response.get()
            await MainActor.run {
                self.peers = response.peers
            }
        } catch {
            NSLog("[EconetClient] Failed to refresh peers: %@", error.localizedDescription)
        }
    }
}
