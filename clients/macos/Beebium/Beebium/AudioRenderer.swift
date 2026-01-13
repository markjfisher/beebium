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
import Atomics

/// Audio signal processing pipeline for BBC Micro audio.
///
/// Handles the full DSP chain:
/// 1. Unpack 4 x 8-bit channels from packed samples
/// 2. DC bias reconstruction using voltage metadata
/// 3. High-pass filter to remove DC offset
/// 4. Low-pass filter for anti-aliasing
/// 5. Per-channel volume with exponential scaling
/// 6. Per-channel stereo panning
/// 7. Mix to stereo with master volume
/// 8. Level metering (RMS/peak per channel)
///
/// Thread safety:
/// - Volume, mute, and DC bias state are accessed atomically
/// - Filter state is only accessed from the audio render thread
/// - Meter state is written from audio thread, read from main thread
final class AudioRenderer: @unchecked Sendable {

    // MARK: - Constants

    /// Number of audio channels (Tone0, Tone1, Tone2, Noise)
    static let channelCount = 4

    /// Default lowpass cutoff frequency (based on beebjit research)
    static let defaultLowpassCutoffHz: Float = 7200.0

    /// Highpass cutoff for DC removal
    static let highpassCutoffHz: Float = 20.0

    /// Sample rate
    let sampleRate: Float

    // MARK: - Filters

    private var lowpassFilters: BiquadFilterBank
    private var highpassFilters: BiquadFilterBank

    // MARK: - DC Bias Metadata (from GetChannelStates)

    /// Per-channel DC bias voltage (atomic for thread-safe access)
    private var dcBiasVoltage: [ManagedAtomic<UInt32>]
    private var peakVoltage: [ManagedAtomic<UInt32>]
    private var troughVoltage: [ManagedAtomic<UInt32>]

    /// Default DC bias when no metadata available
    static let defaultDCBias: Float = 0.8

    // MARK: - Volume Control (atomic for thread-safe access)

    private let atomicMasterVolume = ManagedAtomic<UInt32>(floatToUInt32(1.0))
    private var atomicChannelVolumes: [ManagedAtomic<UInt32>]
    private let atomicMuted = ManagedAtomic<Bool>(false)

    // MARK: - Pan Positions (atomic, -1.0 = left, 0.0 = center, 1.0 = right)

    private var atomicPanPositions: [ManagedAtomic<UInt32>]

    // MARK: - Level Metering

    /// Per-channel meter state
    struct MeterState: Sendable {
        var rms: Float = 0.0
        var peak: Float = 0.0
        var peakHold: Float = 0.0
    }

    /// Atomic storage for meter states (using packed UInt64 for 2 floats)
    private var meterStates: [ManagedAtomic<UInt64>]

    /// Decay rates for metering
    private let peakDecayRate: Float = 0.9995  // ~200ms decay at 48kHz
    private let rmsAlpha: Float = 0.001

    // MARK: - Ring Buffer Reference

    private let ringBuffer: AudioRingBuffer

    // MARK: - Scratch Buffers (allocated once, reused)

    private var packedSamples: [UInt32]
    private let maxFrameCount: Int

    // MARK: - Initialization

    init(ringBuffer: AudioRingBuffer, sampleRate: Float = 48000.0, maxFrameCount: Int = 1024) {
        self.ringBuffer = ringBuffer
        self.sampleRate = sampleRate
        self.maxFrameCount = maxFrameCount

        // Initialize filters
        lowpassFilters = BiquadFilterBank(
            channelCount: Self.channelCount,
            lowpassCutoffHz: Self.defaultLowpassCutoffHz,
            sampleRate: sampleRate
        )
        highpassFilters = BiquadFilterBank(
            channelCount: Self.channelCount,
            highpassCutoffHz: Self.highpassCutoffHz,
            sampleRate: sampleRate
        )

        // Initialize DC bias (default 0.8V)
        dcBiasVoltage = (0..<Self.channelCount).map { _ in
            ManagedAtomic<UInt32>(floatToUInt32(Self.defaultDCBias))
        }
        peakVoltage = (0..<Self.channelCount).map { _ in
            ManagedAtomic<UInt32>(floatToUInt32(Self.defaultDCBias + 0.4))
        }
        troughVoltage = (0..<Self.channelCount).map { _ in
            ManagedAtomic<UInt32>(floatToUInt32(Self.defaultDCBias - 0.4))
        }

        // Initialize volumes to 1.0
        atomicChannelVolumes = (0..<Self.channelCount).map { _ in
            ManagedAtomic<UInt32>(floatToUInt32(1.0))
        }

        // Initialize pan positions to center (0.0)
        atomicPanPositions = (0..<Self.channelCount).map { _ in
            ManagedAtomic<UInt32>(floatToUInt32(0.0))
        }

        // Initialize meter states
        meterStates = (0..<Self.channelCount).map { _ in
            ManagedAtomic<UInt64>(0)
        }

        // Allocate scratch buffers
        packedSamples = [UInt32](repeating: 0, count: maxFrameCount)
    }

    // MARK: - Render Callback

    /// Render audio samples for AVAudioSourceNode.
    ///
    /// This method is called from the audio render thread.
    ///
    /// - Parameters:
    ///   - frameCount: Number of frames to render
    ///   - leftBuffer: Left channel output buffer
    ///   - rightBuffer: Right channel output buffer
    /// - Returns: Number of frames actually rendered
    func render(frameCount: Int, leftBuffer: UnsafeMutablePointer<Float>, rightBuffer: UnsafeMutablePointer<Float>) -> Int {
        let framesToRender = min(frameCount, maxFrameCount)

        // Read from ring buffer
        let samplesRead = ringBuffer.read(&packedSamples, count: framesToRender)

        // Check mute state
        let isMuted = atomicMuted.load(ordering: .relaxed)

        // Load volume state
        let masterVolume = linearToExponential(uint32ToFloat(atomicMasterVolume.load(ordering: .relaxed)))
        var channelVolumes = [Float](repeating: 0, count: Self.channelCount)
        var panPositions = [Float](repeating: 0, count: Self.channelCount)

        for i in 0..<Self.channelCount {
            channelVolumes[i] = linearToExponential(uint32ToFloat(atomicChannelVolumes[i].load(ordering: .relaxed)))
            panPositions[i] = uint32ToFloat(atomicPanPositions[i].load(ordering: .relaxed))
        }

        // Load DC bias metadata
        var dcBias = [Float](repeating: Self.defaultDCBias, count: Self.channelCount)
        var peak = [Float](repeating: Self.defaultDCBias + 0.4, count: Self.channelCount)
        var trough = [Float](repeating: Self.defaultDCBias - 0.4, count: Self.channelCount)

        for i in 0..<Self.channelCount {
            dcBias[i] = uint32ToFloat(dcBiasVoltage[i].load(ordering: .relaxed))
            peak[i] = uint32ToFloat(peakVoltage[i].load(ordering: .relaxed))
            trough[i] = uint32ToFloat(troughVoltage[i].load(ordering: .relaxed))
        }

        // Process samples
        for i in 0..<framesToRender {
            var left: Float = 0
            var right: Float = 0

            if i < samplesRead && !isMuted {
                // Unpack 4 x 8-bit channels from packed sample
                let packed = packedSamples[i]
                let amplitudes = unpackChannels(packed)

                // Process each channel
                for ch in 0..<Self.channelCount {
                    // The SN76489 output is a square wave with DC bias characteristics.
                    // The amplitude value (+127 to -127) encodes both:
                    // - Polarity: positive = output high, negative = output low
                    // - Magnitude: proportional to volume register (0=max, 15=silent)
                    //
                    // The DC bias metadata (peak_v, trough_v, dc_bias_v) tells us the
                    // actual analog voltage characteristics at the current volume.
                    //
                    // For accurate analog reconstruction:
                    // - Map positive amplitude to peak voltage
                    // - Map negative amplitude to trough voltage
                    // - Map zero amplitude to DC bias (silent channel)
                    // - Scale by amplitude magnitude to account for volume level

                    var sample: Float
                    let amplitude = amplitudes[ch]

                    if amplitude == 0 {
                        // Silent channel - output DC bias (which will be filtered out)
                        sample = dcBias[ch]
                    } else {
                        // Active channel - interpolate between peak and trough
                        // based on amplitude magnitude and polarity
                        let normalizedMagnitude = abs(Float(amplitude)) / 127.0
                        if amplitude > 0 {
                            // Output high - move from dcBias toward peak
                            sample = dcBias[ch] + normalizedMagnitude * (peak[ch] - dcBias[ch])
                        } else {
                            // Output low - move from dcBias toward trough
                            sample = dcBias[ch] + normalizedMagnitude * (trough[ch] - dcBias[ch])
                        }
                    }

                    // Remove DC offset by subtracting the bias
                    sample -= dcBias[ch]

                    // Normalize to audio range. The maximum swing from dcBias is ~0.4V
                    // (half of the 0.8V peak-to-peak at max volume), so scale by 2.5
                    sample *= 2.5

                    // Apply highpass filter (remove DC)
                    sample = highpassFilters.process(sample, channel: ch)

                    // Apply lowpass filter (anti-aliasing)
                    sample = lowpassFilters.process(sample, channel: ch)

                    // Update meter
                    updateMeter(channel: ch, sample: sample)

                    // Apply per-channel volume
                    sample *= channelVolumes[ch]

                    // Apply pan to stereo (constant-power panning)
                    let pan = panPositions[ch]
                    let leftGain = cos((pan + 1.0) * Float.pi / 4.0)
                    let rightGain = sin((pan + 1.0) * Float.pi / 4.0)

                    left += sample * leftGain
                    right += sample * rightGain
                }

                // Apply master volume
                left *= masterVolume
                right *= masterVolume

                // Soft clipping to prevent harsh distortion
                left = softClip(left)
                right = softClip(right)
            }

            leftBuffer[i] = left
            rightBuffer[i] = right
        }

        return samplesRead
    }

    // MARK: - Volume Control (called from main thread)

    /// Set master volume (0.0 to 1.0, linear slider value)
    func setMasterVolume(_ volume: Float) {
        atomicMasterVolume.store(floatToUInt32(volume.clamped(to: 0...1)), ordering: .relaxed)
    }

    /// Set per-channel volume (0.0 to 1.0, linear slider value)
    func setChannelVolume(_ channel: Int, volume: Float) {
        guard channel >= 0 && channel < Self.channelCount else { return }
        atomicChannelVolumes[channel].store(floatToUInt32(volume.clamped(to: 0...1)), ordering: .relaxed)
    }

    /// Set mute state
    func setMuted(_ muted: Bool) {
        atomicMuted.store(muted, ordering: .relaxed)
    }

    /// Set per-channel pan position (-1.0 = left, 0.0 = center, 1.0 = right)
    func setPanPosition(_ channel: Int, pan: Float) {
        guard channel >= 0 && channel < Self.channelCount else { return }
        atomicPanPositions[channel].store(floatToUInt32(pan.clamped(to: -1...1)), ordering: .relaxed)
    }

    // MARK: - DC Bias Metadata (called from main thread)

    /// Update DC bias metadata for a channel
    func setDCBias(channel: Int, dcBias: Float, peak: Float, trough: Float) {
        guard channel >= 0 && channel < Self.channelCount else { return }
        dcBiasVoltage[channel].store(floatToUInt32(dcBias), ordering: .relaxed)
        peakVoltage[channel].store(floatToUInt32(peak), ordering: .relaxed)
        troughVoltage[channel].store(floatToUInt32(trough), ordering: .relaxed)
    }

    // MARK: - Level Metering

    /// Get current meter state for a channel (called from main thread)
    func getMeterState(channel: Int) -> MeterState {
        guard channel >= 0 && channel < Self.channelCount else {
            return MeterState()
        }
        let packed = meterStates[channel].load(ordering: .relaxed)
        return unpackMeterState(packed)
    }

    /// Get all channel meter states
    func getAllMeterStates() -> [MeterState] {
        return (0..<Self.channelCount).map { getMeterState(channel: $0) }
    }

    private func updateMeter(channel: Int, sample: Float) {
        let currentPacked = meterStates[channel].load(ordering: .relaxed)
        var state = unpackMeterState(currentPacked)

        let absSample = abs(sample)

        // Update peak with decay
        state.peak = max(state.peak * peakDecayRate, absSample)

        // Update peak hold (slower decay)
        if absSample > state.peakHold {
            state.peakHold = absSample
        } else {
            state.peakHold *= 0.9999  // Very slow decay
        }

        // Update RMS (exponential moving average)
        let rmsSquared = rmsAlpha * sample * sample + (1.0 - rmsAlpha) * state.rms * state.rms
        state.rms = sqrt(rmsSquared)

        // Store updated state
        meterStates[channel].store(packMeterState(state), ordering: .relaxed)
    }

    // MARK: - Filter Control

    /// Reset all filter states (call on discontinuity)
    func resetFilters() {
        lowpassFilters.reset()
        highpassFilters.reset()
    }

    // MARK: - Private Helpers

    /// Unpack 4 x 8-bit signed channels from a big-endian packed 32-bit sample
    private func unpackChannels(_ packed: UInt32) -> [Int8] {
        return [
            Int8(bitPattern: UInt8((packed >> 24) & 0xFF)),  // Tone0
            Int8(bitPattern: UInt8((packed >> 16) & 0xFF)),  // Tone1
            Int8(bitPattern: UInt8((packed >> 8) & 0xFF)),   // Tone2
            Int8(bitPattern: UInt8(packed & 0xFF))           // Noise
        ]
    }

    /// Convert linear slider value to exponential volume (40dB dynamic range)
    private func linearToExponential(_ linear: Float) -> Float {
        if linear <= 0 { return 0 }
        if linear >= 1 { return 1 }
        // -40dB to 0dB range
        return pow(10.0, (linear - 1.0) * 2.0)
    }

    /// Soft clipping to prevent harsh distortion
    private func softClip(_ x: Float) -> Float {
        if x > 1.0 {
            return 1.0 - exp(1.0 - x)
        } else if x < -1.0 {
            return -1.0 + exp(-1.0 - x)
        }
        return x
    }

    /// Pack meter state into UInt64 for atomic storage
    private func packMeterState(_ state: MeterState) -> UInt64 {
        let rmsUInt = floatToUInt32(state.rms)
        let peakUInt = floatToUInt32(state.peak)
        return UInt64(rmsUInt) | (UInt64(peakUInt) << 32)
    }

    /// Unpack meter state from UInt64
    private func unpackMeterState(_ packed: UInt64) -> MeterState {
        var state = MeterState()
        state.rms = uint32ToFloat(UInt32(packed & 0xFFFFFFFF))
        state.peak = uint32ToFloat(UInt32(packed >> 32))
        return state
    }
}

// MARK: - Float/UInt32 Conversion Helpers

/// Store float as UInt32 (bit pattern)
private func floatToUInt32(_ value: Float) -> UInt32 {
    return value.bitPattern
}

/// Load float from UInt32 (bit pattern)
private func uint32ToFloat(_ bits: UInt32) -> Float {
    return Float(bitPattern: bits)
}

// MARK: - Float Clamping Extension

extension Float {
    func clamped(to range: ClosedRange<Float>) -> Float {
        return Swift.min(Swift.max(self, range.lowerBound), range.upperBound)
    }
}
