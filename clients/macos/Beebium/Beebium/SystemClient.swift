// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

/// Client for querying system/machine information from beebium-server via gRPC
@MainActor
final class SystemClient: ObservableObject {
    // MARK: - Machine Identity

    /// Machine UUID (RFC 4122 v4), stable for machine lifetime
    @Published private(set) var machineUUID: String = ""

    /// User-assignable machine label
    @Published private(set) var machineName: String = ""

    /// Machine model type identifier (e.g., "ModelBPlus")
    @Published private(set) var machineType: String = ""

    /// Machine model display name (e.g., "BBC Model B+ 64K")
    @Published private(set) var machineDisplayName: String = ""

    // MARK: - Connection State

    /// Whether system info has been successfully loaded
    @Published private(set) var isLoaded: Bool = false

    /// Error message if loading failed
    @Published private(set) var errorMessage: String?

    private var client: Beebium_SystemServiceNIOClient?

    /// Connect to the server using an existing gRPC channel and fetch system info
    func connect(channel: GRPCChannel) {
        client = Beebium_SystemServiceNIOClient(channel: channel)
        fetchSystemInfo()
    }

    /// Disconnect from the server
    func disconnect() {
        client = nil
        isLoaded = false
        machineUUID = ""
        machineName = ""
        machineType = ""
        machineDisplayName = ""
        errorMessage = nil
    }

    /// Set the machine's user-assignable name
    func setMachineName(_ name: String) {
        guard let client = client else { return }

        Task { [weak self] in
            do {
                var request = Beebium_SetMachineNameRequest()
                request.name = name
                let response = try await client.setMachineName(request).response.get()

                await MainActor.run {
                    self?.updateIdentity(response.identity)
                }
            } catch {
                await MainActor.run {
                    self?.errorMessage = error.localizedDescription
                }
            }
        }
    }

    /// Fetch system information from the server
    private func fetchSystemInfo() {
        guard let client = client else { return }

        Task { [weak self] in
            do {
                let request = Beebium_GetSystemInfoRequest()
                let response = try await client.getSystemInfo(request).response.get()

                await MainActor.run {
                    self?.updateIdentity(response.identity)
                    self?.isLoaded = true
                    self?.errorMessage = nil
                }
            } catch {
                await MainActor.run {
                    self?.errorMessage = error.localizedDescription
                    self?.isLoaded = false
                }
            }
        }
    }

    /// Update local identity state from server response
    private func updateIdentity(_ identity: Beebium_MachineIdentity) {
        machineUUID = identity.uuid
        machineName = identity.name
        machineType = identity.modelType
        machineDisplayName = identity.modelName
    }
}
