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
    /// Machine display name (e.g., "BBC Model B+ 64K")
    @Published private(set) var machineDisplayName: String = ""

    /// Machine type identifier (e.g., "ModelBPlus")
    @Published private(set) var machineType: String = ""

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
        machineDisplayName = ""
        machineType = ""
        errorMessage = nil
    }

    /// Fetch system information from the server
    private func fetchSystemInfo() {
        guard let client = client else { return }

        Task { [weak self] in
            do {
                let request = Beebium_GetSystemInfoRequest()
                let response = try await client.getSystemInfo(request).response.get()

                await MainActor.run {
                    self?.machineType = response.machineType
                    self?.machineDisplayName = response.machineDisplayName
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
}
