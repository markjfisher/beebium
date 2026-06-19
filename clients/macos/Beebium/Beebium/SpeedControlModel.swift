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

/// Drives the Processor panel's emulation-speed control.
///
/// Owns the speed state shown by the slider and the achieved-speed marker, and
/// polls the server's pacing stats while the panel is visible. The numeric scale
/// is a symmetric log2 axis with 1x at the centre: the upper bound is the
/// session's running-max attainable speed rounded up to a power of two, and the
/// lower bound is its reciprocal. The running max only grows, so the scale never
/// jars by shrinking, but it expands when the host frees up and a higher speed
/// becomes attainable.
@MainActor
final class SpeedControlModel: ObservableObject {
    /// Configured multiplier the slider reflects (1.0 = real-time). The source
    /// of truth for what is sent when not running unlimited.
    @Published var configuredMultiplier: Double = 1.0

    /// Most recently reported achieved multiplier (the read-only marker). ~1x
    /// under normal real-time pacing.
    @Published private(set) var achievedMultiplier: Double = 1.0

    /// Whether the emulator is running unlimited (the 0.0 wire sentinel).
    @Published private(set) var isUnlimited: Bool = false

    /// Running max of the server's estimated max-attainable speed over the
    /// session. Seeds the scale; only ever grows.
    @Published private(set) var sessionMaxAttainable: Double = 1.0

    /// Scale half-width N: the axis spans [2^-N, 2^N] with 1x centred. Floored at
    /// 2 so even a slow host still gets a usable [1/4x, 4x] range.
    var scaleExponent: Int {
        max(2, Int(ceil(log2(max(sessionMaxAttainable, 1.0)))))
    }

    var scaleMax: Double { pow(2.0, Double(scaleExponent)) }
    var scaleMin: Double { 1.0 / scaleMax }

    private weak var systemClient: SystemClient?
    private var pollTask: Task<Void, Never>?
    private var hasInitialised = false
    // Poll a little faster than the server publishes (~500ms) so each new
    // sample is picked up promptly; the marker is then animated to it. No
    // client-side smoothing: the server window already averages, and stacking
    // an EMA on top over-smooths into a laggy "wave".
    private static let pollInterval = Duration.milliseconds(250)

    /// Bind to the system client used for the RPCs. Safe to call before connect;
    /// polls simply return nothing until the client is connected.
    func bind(to systemClient: SystemClient) {
        self.systemClient = systemClient
    }

    /// Begin polling pacing stats (call when the panel appears).
    func startPolling() {
        guard pollTask == nil else { return }
        pollTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.poll()
                try? await Task.sleep(for: SpeedControlModel.pollInterval)
            }
        }
    }

    /// Stop polling (call when the panel disappears).
    func stopPolling() {
        pollTask?.cancel()
        pollTask = nil
    }

    private func poll() async {
        guard let stats = await systemClient?.getPacingStats() else { return }

        // Show the server's sample directly; the view animates the marker to it.
        // Only assign on change so an unchanged poll doesn't restart the glide.
        if stats.achievedSpeedMultiplier != achievedMultiplier {
            achievedMultiplier = stats.achievedSpeedMultiplier
        }

        // Grow the session max from the server's estimate (and the achieved rate
        // as a floor). estimated is 0 until the first window completes.
        let candidate = max(stats.estimatedMaxSpeedMultiplier, stats.achievedSpeedMultiplier)
        if candidate > sessionMaxAttainable {
            sessionMaxAttainable = candidate
        }

        // Adopt the server's configured speed once, at startup, so a machine
        // launched with --speed is reflected. After that the slider/toggle own
        // this state (the GUI is the only thing changing it).
        if !hasInitialised {
            hasInitialised = true
            if stats.speedMultiplier == 0.0 {
                isUnlimited = true
            } else {
                configuredMultiplier = stats.speedMultiplier
            }
        }
    }

    // MARK: - User actions

    /// Apply a multiplier from the slider (turns off unlimited).
    func apply(multiplier: Double) {
        configuredMultiplier = multiplier
        isUnlimited = false
        Task { await systemClient?.setSpeedMultiplier(multiplier) }
    }

    /// Toggle unlimited speed. When turned off, restores the slider's multiplier.
    func setUnlimited(_ unlimited: Bool) {
        isUnlimited = unlimited
        let target = unlimited ? 0.0 : configuredMultiplier
        Task { await systemClient?.setSpeedMultiplier(target) }
    }
}
