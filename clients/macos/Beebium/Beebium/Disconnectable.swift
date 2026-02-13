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

/// Protocol for gRPC clients that can be disconnected from the server.
@MainActor
protocol Disconnectable: AnyObject {
    func disconnect()
}

/// Manages an ordered collection of gRPC clients for bulk disconnect operations.
///
/// Clients are disconnected in reverse registration order. VideoClient is registered
/// separately and always disconnected last because it owns the shared GRPCChannel.
@MainActor
final class ClientGroup {
    private var clients: [Disconnectable] = []
    private weak var videoClient: VideoClient?

    /// Register a non-video client. Clients are disconnected in reverse registration order.
    func register(_ client: Disconnectable) {
        clients.append(client)
    }

    /// Register the VideoClient separately. It will always be disconnected last.
    func registerVideoClient(_ client: VideoClient) {
        videoClient = client
    }

    /// Disconnect all non-video clients in reverse registration order.
    /// Used when VideoClient's connectionState changes to .disconnected or .error,
    /// since VideoClient has already lost its connection.
    func disconnectNonVideoClients() {
        for client in clients.reversed() {
            client.disconnect()
        }
    }

    /// Disconnect all clients including VideoClient (which is disconnected last).
    /// Used for orderly shutdown (close button, quit).
    func disconnectAll() {
        disconnectNonVideoClients()
        videoClient?.disconnect()
    }
}
