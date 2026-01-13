// Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version. Beebium is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details. You should have received a copy of the GNU General Public License along with
// Beebium. If not, see <https://www.gnu.org/licenses/>.

import Foundation
import GRPC

/// Audio source information parsed from server
struct AudioSourceInfo: Identifiable {
    let id: UInt32
    let name: String
    let channelNames: [String]
    let groupId: UInt32
}

/// Channel group information for UI organization
struct ChannelGroupInfo: Identifiable {
    let id: UInt32
    let name: String
    let description: String
    let color: String
}

/// Per-channel state for visualization and DC bias reconstruction
struct ChannelStateInfo {
    let channelId: UInt32
    let channelName: String
    let frequencyHz: Float
    let volume: UInt32
    let amplitude: Int32
    let dcBiasVoltage: Float
    let peakVoltage: Float
    let troughVoltage: Float
}

/// Client for streaming audio from beebium-server via gRPC.
///
/// Follows the established client pattern:
/// - @MainActor for UI updates
/// - Connects via shared GRPCChannel
/// - Manages audio streaming, format metadata, and channel introspection
@MainActor
final class AudioClient: ObservableObject {

    // MARK: - Audio Format Metadata

    /// Sample rate from server (typically 48000)
    @Published private(set) var sampleRate: UInt32 = 48000

    /// Number of 32-bit source fields per sample
    @Published private(set) var sourceCount: UInt32 = 4

    /// Audio source metadata
    @Published private(set) var sources: [AudioSourceInfo] = []

    /// Channel groups for UI organization
    @Published private(set) var groups: [ChannelGroupInfo] = []

    // MARK: - Channel Introspection

    /// Current state of all channels (for visualization and DC bias)
    @Published private(set) var channelStates: [ChannelStateInfo] = []

    // MARK: - Connection State

    /// Whether format metadata has been loaded
    @Published private(set) var isLoaded: Bool = false

    /// Error message if connection failed
    @Published private(set) var errorMessage: String?

    // MARK: - Statistics

    /// Number of audio chunks received
    @Published private(set) var chunksReceived: UInt64 = 0

    /// Number of samples dropped due to buffer overflow
    @Published private(set) var samplesDropped: UInt64 = 0

    /// Last sequence number for drop detection
    private var lastSequence: UInt64 = 0

    // MARK: - Audio Pipeline Components

    /// Ring buffer shared with audio renderer
    let ringBuffer: AudioRingBuffer

    /// Audio renderer (DSP pipeline)
    let renderer: AudioRenderer

    /// Audio engine (AVAudioEngine wrapper)
    private var engine: AudioEngine?

    // MARK: - gRPC Client

    private var client: Beebium_AudioServiceNIOClient?
    private var streamTask: Task<Void, Never>?
    private var channelStateTask: Task<Void, Never>?

    // MARK: - Initialization

    init() {
        ringBuffer = AudioRingBuffer()
        renderer = AudioRenderer(ringBuffer: ringBuffer)
    }

    // MARK: - Connection Lifecycle

    /// Connect to the server using an existing gRPC channel
    func connect(channel: GRPCChannel) {
        client = Beebium_AudioServiceNIOClient(channel: channel)

        // Start audio engine
        engine = AudioEngine(renderer: renderer)
        do {
            try engine?.start()
        } catch {
            errorMessage = "Failed to start audio engine: \(error.localizedDescription)"
            return
        }

        // Fetch format and start streaming
        streamTask = Task { [weak self] in
            await self?.fetchFormatAndSubscribe()
        }

        // Start periodic channel state polling for DC bias metadata
        startChannelStatePolling()
    }

    /// Disconnect from the server
    func disconnect() {
        // Stop polling
        channelStateTask?.cancel()
        channelStateTask = nil

        // Stop streaming
        streamTask?.cancel()
        streamTask = nil

        // Stop audio engine
        engine?.stop()
        engine = nil

        // Clear state
        client = nil
        isLoaded = false
        sources = []
        groups = []
        channelStates = []
        errorMessage = nil
        chunksReceived = 0
        samplesDropped = 0
        lastSequence = 0

        // Reset ring buffer
        ringBuffer.reset()
    }

    // MARK: - Audio Format and Streaming

    private func fetchFormatAndSubscribe() async {
        guard let client = client else { return }

        // First, fetch audio format metadata
        do {
            let request = Beebium_GetAudioFormatRequest()
            let response = try await client.getAudioFormat(request).response.get()

            let newSources = response.sources.map { source in
                AudioSourceInfo(
                    id: source.sourceIndex,
                    name: source.sourceName,
                    channelNames: source.channelNames,
                    groupId: source.groupID
                )
            }

            let newGroups = response.groups.map { group in
                ChannelGroupInfo(
                    id: group.groupID,
                    name: group.groupName,
                    description: group.description_p,
                    color: group.color
                )
            }

            await MainActor.run {
                self.sampleRate = response.sampleRate
                self.sourceCount = response.sourceCount
                self.sources = newSources
                self.groups = newGroups
                self.isLoaded = true
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Failed to get audio format: \(error.localizedDescription)"
                self.isLoaded = false
            }
            return
        }

        // Then subscribe to audio stream
        var request = Beebium_SubscribeAudioRequest()
        request.chunkSize = 1024  // Samples per chunk

        let call = client.subscribeAudio(request) { [weak self] chunk in
            self?.handleAudioChunk(chunk)
        }

        // Wait for stream to complete or be cancelled
        do {
            _ = try await call.status.get()
        } catch {
            await MainActor.run {
                if self.isLoaded {
                    self.errorMessage = "Audio stream ended: \(error.localizedDescription)"
                }
            }
        }
    }

    private func handleAudioChunk(_ chunk: Beebium_AudioChunk) {
        // Detect dropped chunks via sequence gap
        if lastSequence > 0 && chunk.sequence > lastSequence + 1 {
            let gap = chunk.sequence - lastSequence - 1
            Task { @MainActor in
                self.samplesDropped += UInt64(gap) * UInt64(chunk.sampleCount)
            }
        }
        lastSequence = chunk.sequence

        // The samples field contains: sample_count * source_count * 4 bytes
        // Each sample has MAX_SOURCES (4) × 32-bit fields
        // We only need source 0 (SN76489) - extract it
        let data = chunk.samples
        let sampleCount = Int(chunk.sampleCount)
        let sourceCount = Int(sourceCount)  // Usually 4
        let bytesPerSample = sourceCount * 4  // 16 bytes per sample

        // Extract only source 0 from each sample
        var source0Data = Data(capacity: sampleCount * 4)
        data.withUnsafeBytes { rawBuffer in
            let bytes = rawBuffer.bindMemory(to: UInt8.self)
            for i in 0..<sampleCount {
                let offset = i * bytesPerSample
                // Source 0 is at the beginning of each sample (4 bytes)
                if offset + 4 <= bytes.count {
                    source0Data.append(bytes[offset])
                    source0Data.append(bytes[offset + 1])
                    source0Data.append(bytes[offset + 2])
                    source0Data.append(bytes[offset + 3])
                }
            }
        }

        let written = ringBuffer.write(data: source0Data)

        Task { @MainActor in
            self.chunksReceived += 1
            if written < sampleCount {
                self.samplesDropped += UInt64(sampleCount - written)
            }
        }
    }

    // MARK: - Channel State Polling (for DC bias)

    private func startChannelStatePolling() {
        channelStateTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.refreshChannelStates()
                try? await Task.sleep(nanoseconds: 16_666_667)  // ~60Hz
            }
        }
    }

    /// Refresh channel states from server (for DC bias and visualization)
    func refreshChannelStates() async {
        guard let client = client else { return }

        do {
            let request = Beebium_GetChannelStatesRequest()
            let response = try await client.getChannelStates(request).response.get()

            let states = response.channels.map { channel in
                ChannelStateInfo(
                    channelId: channel.channelID,
                    channelName: channel.channelName,
                    frequencyHz: channel.frequencyHz,
                    volume: channel.volume,
                    amplitude: channel.amplitude,
                    dcBiasVoltage: channel.dcBiasVoltage,
                    peakVoltage: channel.peakVoltage,
                    troughVoltage: channel.troughVoltage
                )
            }

            await MainActor.run {
                self.channelStates = states
            }

            // Update renderer with DC bias metadata
            for state in states {
                renderer.setDCBias(
                    channel: Int(state.channelId),
                    dcBias: state.dcBiasVoltage,
                    peak: state.peakVoltage,
                    trough: state.troughVoltage
                )
            }
        } catch {
            // Silently ignore errors - polling will retry
        }
    }

    // MARK: - Volume Control Passthrough

    /// Set master volume (0.0 to 1.0)
    func setMasterVolume(_ volume: Float) {
        renderer.setMasterVolume(volume)
    }

    /// Set per-channel volume (0.0 to 1.0)
    func setChannelVolume(_ channel: Int, volume: Float) {
        renderer.setChannelVolume(channel, volume: volume)
    }

    /// Set mute state
    func setMuted(_ muted: Bool) {
        renderer.setMuted(muted)
    }

    /// Set per-channel pan position (-1.0 to 1.0)
    func setPanPosition(_ channel: Int, pan: Float) {
        renderer.setPanPosition(channel, pan: pan)
    }

    // MARK: - Meter Access

    /// Get current meter state for a channel
    func getMeterState(channel: Int) -> AudioRenderer.MeterState {
        return renderer.getMeterState(channel: channel)
    }

    /// Get all channel meter states
    func getAllMeterStates() -> [AudioRenderer.MeterState] {
        return renderer.getAllMeterStates()
    }
}
