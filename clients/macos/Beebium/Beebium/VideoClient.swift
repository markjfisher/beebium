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
import NIO
import SwiftProtobuf

/// Connection state for the video client
enum ConnectionState: Equatable {
    case disconnected
    case connecting
    case connected
    case error(String)
}

/// Client for streaming video frames from beebium-server via gRPC
@MainActor
final class VideoClient: ObservableObject, Disconnectable {
    /// Current connection state
    @Published private(set) var connectionState: ConnectionState = .disconnected

    /// Latest received frame data (BGRA32 pixels)
    @Published private(set) var currentFrame: Data?

    /// Frame dimensions (logical pixels)
    @Published private(set) var frameWidth: Int = 736
    @Published private(set) var frameHeight: Int = 576

    /// Display dimensions (target after scaling)
    @Published private(set) var displayWidth: Int = 736
    @Published private(set) var displayHeight: Int = 576

    /// Frame counter for debugging
    @Published private(set) var frameCount: UInt64 = 0

    private var group: EventLoopGroup?
    private var _channel: GRPCChannel?
    private var streamTask: Task<Void, Never>?

    private var host: String
    private var port: Int

    /// Current connection target
    @Published private(set) var target: ConnectionTarget

    /// Metal renderer for direct frame updates (bypasses SwiftUI update batching)
    weak var renderer: MetalRenderer?

    /// Expose the gRPC channel for other clients (e.g., KeyboardClient) to share
    var channel: GRPCChannel? { _channel }

    init(target: ConnectionTarget = .localhost) {
        self.target = target
        self.host = target.host
        self.port = target.port
    }

    /// Reconnect to a different target
    func reconnect(to newTarget: ConnectionTarget) {
        disconnect()
        target = newTarget
        host = newTarget.host
        port = newTarget.port
        connect()
    }

    /// Connect to the beebium-server and start streaming frames
    func connect() {
        guard connectionState == .disconnected || connectionState != .connecting else { return }

        connectionState = .connecting

        streamTask = Task { [weak self] in
            await self?.runConnection()
        }
    }

    /// Disconnect from the server
    func disconnect() {
        streamTask?.cancel()
        streamTask = nil

        let channelToClose = _channel
        let groupToShutdown = group

        // Close channel and shutdown event loop group on background thread
        DispatchQueue.global().async {
            try? channelToClose?.close().wait()
            try? groupToShutdown?.syncShutdownGracefully()
        }

        _channel = nil
        group = nil
        connectionState = .disconnected
    }

    private func runConnection() async {
        let eventLoopGroup = MultiThreadedEventLoopGroup(numberOfThreads: 1)

        do {
            let grpcChannel = try GRPCChannelPool.with(
                target: .host(host, port: port),
                transportSecurity: .plaintext,
                eventLoopGroup: eventLoopGroup
            )

            await MainActor.run {
                self.group = eventLoopGroup
                self._channel = grpcChannel
            }

            // Get video config first
            let client = Beebium_VideoServiceNIOClient(channel: grpcChannel)

            do {
                let config = try await client.getConfig(Beebium_GetConfigRequest()).response.get()
                await MainActor.run {
                    self.frameWidth = Int(config.width)
                    self.frameHeight = Int(config.height)
                    self.connectionState = .connected
                }
            } catch {
                await MainActor.run {
                    self.connectionState = .error("Failed to get config: \(error.localizedDescription)")
                }
                return
            }

            // Start streaming frames
            let call = client.subscribeFrames(Beebium_SubscribeFramesRequest()) { [weak self] frame in
                Task { @MainActor [weak self] in
                    self?.handleFrame(frame)
                }
            }

            // Wait for stream to complete or be cancelled
            do {
                _ = try await call.status.get()
            } catch {
                await MainActor.run {
                    if case .connecting = self.connectionState {
                        self.connectionState = .error("Stream ended: \(error.localizedDescription)")
                    } else if case .connected = self.connectionState {
                        self.connectionState = .error("Connection lost: \(error.localizedDescription)")
                    }
                }
            }

        } catch {
            await MainActor.run {
                self.connectionState = .error("Connection failed: \(error.localizedDescription)")
            }
        }
    }

    private func handleFrame(_ frame: Beebium_Frame) {
        frameCount = frame.frameNumber
        frameWidth = Int(frame.width)
        frameHeight = Int(frame.height)
        displayWidth = Int(frame.displayWidth)
        displayHeight = Int(frame.displayHeight)
        currentFrame = frame.pixels

        // Determine interlace state from field_order
        // PROGRESSIVE = non-interlaced (bitmap modes), EVEN_FIRST/ODD_FIRST = interlaced (MODE 7)
        let isInterlaced = frame.fieldOrder != .progressive

        // Update renderer directly to bypass SwiftUI update batching
        renderer?.updateFrame(
            data: frame.pixels,
            width: Int(frame.width),
            height: Int(frame.height),
            displayWidth: Int(frame.displayWidth),
            displayHeight: Int(frame.displayHeight),
            leftBorder: Int(frame.leftBorder),
            rightBorder: Int(frame.rightBorder),
            topBorder: Int(frame.topBorder),
            bottomBorder: Int(frame.bottomBorder),
            interlaced: isInterlaced
        )
    }
}
