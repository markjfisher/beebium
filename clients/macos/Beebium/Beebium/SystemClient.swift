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
import os

/// Client for querying system/machine information from beebium-server via gRPC
@MainActor
final class SystemClient: ObservableObject, Disconnectable {
    private static let log = Logger(subsystem: "com.beebium", category: "protocol")

    // MARK: - Machine Identity

    /// Machine UUID (RFC 4122 v4), stable for machine lifetime
    @Published private(set) var machineUUID: String = ""

    /// User-assignable machine label
    @Published private(set) var machineName: String = ""

    /// Machine model type identifier (e.g., "ModelBPlus")
    @Published private(set) var machineType: String = ""

    /// Machine model display name (e.g., "BBC Model B+ 64K")
    @Published private(set) var machineDisplayName: String = ""

    // MARK: - Connection Tracking

    /// Number of clients with active WatchServerStatus streams on the server
    @Published private(set) var clientCount: Int = 0

    /// Whether the server has signaled it is shutting down
    @Published private(set) var isServerShuttingDown: Bool = false

    // MARK: - Connection State

    /// Whether system info has been successfully loaded
    @Published private(set) var isLoaded: Bool = false

    /// Error message if loading failed
    @Published private(set) var errorMessage: String?

    /// Set when the connected server's protocol fingerprint does not match the
    /// fingerprint this app was built against -- i.e. the server is a different
    /// (often stale) build. The client and server protocols evolve together, so
    /// a mismatch means RPCs may be missing or behave differently; surface it
    /// loudly rather than letting features silently misbehave. Drives a UI alert.
    @Published var protocolMismatchMessage: String?

    private var client: Beebium_SystemServiceNIOClient?
    private var provenanceUUID: String?
    private var statusStreamCall: ServerStreamingCall<Beebium_WatchServerStatusRequest, Beebium_ServerStatusEvent>?

    /// Connect to the server using an existing gRPC channel and fetch system info
    /// - Parameters:
    ///   - channel: The gRPC channel to use
    ///   - provenanceUUID: Provenance UUID for shutdown authorization (nil for external connections)
    func connect(channel: GRPCChannel, provenanceUUID: String? = nil) {
        client = Beebium_SystemServiceNIOClient(channel: channel)
        self.provenanceUUID = provenanceUUID
        fetchSystemInfo()
        startStatusStream()
    }

    /// Disconnect from the server
    func disconnect() {
        statusStreamCall?.cancel(promise: nil)
        statusStreamCall = nil
        client = nil
        provenanceUUID = nil
        isLoaded = false
        clientCount = 0
        isServerShuttingDown = false
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

    // MARK: - Shutdown

    /// Request server shutdown with provenance-based authorization
    /// - Returns: true if the server accepted the shutdown request
    func requestShutdown(mode: Beebium_ShutdownMode = .shutdownGraceful, gracePeriodMs: Int32 = 5000) async -> Bool {
        guard let client = client else { return false }

        var request = Beebium_ShutdownRequest()
        request.mode = mode
        request.gracePeriodMs = gracePeriodMs

        var callOptions = CallOptions()
        if let uuid = provenanceUUID {
            callOptions.customMetadata.add(name: "x-beebium-instance-uuid", value: uuid)
        }

        do {
            let response = try await client.requestShutdown(request, callOptions: callOptions).response.get()
            NSLog("[SystemClient] RequestShutdown: accepted=%d message=%@", response.accepted, response.message)
            return response.accepted
        } catch {
            NSLog("[SystemClient] RequestShutdown failed: %@", error.localizedDescription)
            return false
        }
    }

    // MARK: - Client Count

    /// Fetch the current client count from the server
    func fetchClientCount() async -> Int {
        guard let client = client else { return 0 }
        do {
            let request = Beebium_GetSystemInfoRequest()
            let response = try await client.getSystemInfo(request).response.get()
            let count = Int(response.connections.clientCount)
            self.clientCount = count
            return count
        } catch {
            NSLog("[SystemClient] fetchClientCount failed: %@", error.localizedDescription)
            return 0
        }
    }

    // MARK: - Emulation Speed

    /// Set the runtime emulation speed multiplier (0.0 = unlimited, 1.0 =
    /// real-time). Returns the resulting multiplier echoed by the server, or nil
    /// on failure.
    @discardableResult
    func setSpeedMultiplier(_ multiplier: Double) async -> Double? {
        guard let client = client else { return nil }
        do {
            var request = Beebium_SetSpeedMultiplierRequest()
            request.speedMultiplier = multiplier
            let response = try await client.setSpeedMultiplier(request).response.get()
            return response.speedMultiplier
        } catch {
            NSLog("[SystemClient] setSpeedMultiplier failed: %@", error.localizedDescription)
            return nil
        }
    }

    /// Fetch a pacing snapshot: configured, achieved, and estimated-max speed
    /// multipliers (sampled over the server's ~5s window), or nil on failure.
    func getPacingStats() async -> Beebium_PacingStats? {
        guard let client = client else { return nil }
        do {
            return try await client.getPacingStats(Beebium_GetPacingStatsRequest()).response.get()
        } catch {
            NSLog("[SystemClient] getPacingStats failed: %@", error.localizedDescription)
            return nil
        }
    }

    // MARK: - Private

    /// Start the WatchServerStatus stream.
    /// This serves two purposes:
    /// 1. The server's ConnectionTracker counts active streams, so this makes us a counted client
    /// 2. We receive shutdown notifications and identity changes
    private func startStatusStream() {
        guard let client = client else { return }

        let request = Beebium_WatchServerStatusRequest()
        let call = client.watchServerStatus(request) { [weak self] event in
            Task { @MainActor [weak self] in
                self?.handleStatusEvent(event)
            }
        }

        statusStreamCall = call

        call.status.whenComplete { [weak self] result in
            Task { @MainActor [weak self] in
                switch result {
                case .success(let status):
                    if status.code != .ok && status.code != .cancelled {
                        NSLog("[SystemClient] Status stream ended: %@", status.description)
                    }
                case .failure(let error):
                    NSLog("[SystemClient] Status stream error: %@", error.localizedDescription)
                }
                self?.statusStreamCall = nil
            }
        }
    }

    /// Handle a server status event from the WatchServerStatus stream
    private func handleStatusEvent(_ event: Beebium_ServerStatusEvent) {
        switch event.status {
        case .serverStatusReady:
            isServerShuttingDown = false
        case .serverStatusShuttingDown:
            isServerShuttingDown = true
        case .serverStatusIdentityChanged:
            if event.hasIdentity {
                updateIdentity(event.identity)
            }
        case .serverStatusShutdownProgress:
            break
        case .UNRECOGNIZED:
            break
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
                    self?.checkProtocolFingerprint(response.protocolFingerprint)
                    self?.updateIdentity(response.identity)
                    self?.clientCount = Int(response.connections.clientCount)
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

    /// Compare the server's protocol fingerprint against this app's and surface
    /// any mismatch loudly. An empty server fingerprint means a server too old
    /// to report one, which is also a mismatch.
    private func checkProtocolFingerprint(_ serverFingerprint: String) {
        guard serverFingerprint != ProtocolFingerprint.value else {
            protocolMismatchMessage = nil
            return
        }
        let reported = serverFingerprint.isEmpty ? "(none reported)" : serverFingerprint
        Self.log.fault(
            "protocol fingerprint mismatch: server=\(reported, privacy: .public) app=\(ProtocolFingerprint.value, privacy: .public)")
        protocolMismatchMessage = """
            The connected server is a different build from this app \
            (protocol fingerprint \(reported) vs \(ProtocolFingerprint.value)). \
            Rebuild the bundled server so the two match -- e.g. run \
            scripts/build-macos-app.sh. Until then some features may not work.
            """
    }
}
