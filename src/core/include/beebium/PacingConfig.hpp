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

#pragma once

#include <chrono>
#include <cstdint>

namespace beebium {

/// Configuration for real-time pacing of emulation.
///
/// Three distinct concepts:
/// - base_clock_hz: The machine's native clock frequency (fixed per machine type)
/// - pacing_hz: Wall-clock synchronization rate (fixed, chosen for audio/latency tradeoff)
/// - speed_multiplier: Playback speed (runtime adjustable)
///
/// The pacing system uses a dedicated timer thread to control emulation speed,
/// allowing the emulation thread to run without knowledge of wall-clock time.
struct PacingConfig {
    /// The machine's native clock frequency in Hz.
    /// For BBC Model B: 2,000,000 (2 MHz)
    uint64_t base_clock_hz;

    /// Wall-clock synchronization rate in Hz.
    /// Higher values reduce latency but increase synchronization overhead.
    /// Typical values: 50 (frame-rate), 200 (audio-friendly), 500 (low-latency)
    uint32_t pacing_hz;

    /// Playback speed multiplier (runtime adjustable).
    /// 1.0 = real-time
    /// 0.5 = half-speed (slow-motion debugging)
    /// 2.0 = double-speed (fast-forward)
    /// 0.0 = unlimited (run as fast as possible, for tests)
    double speed_multiplier;

    /// Calculate effective clock frequency accounting for speed multiplier.
    constexpr uint64_t effective_clock_hz() const {
        return static_cast<uint64_t>(base_clock_hz * speed_multiplier);
    }

    /// Calculate cycles to execute per pacing tick.
    constexpr uint64_t cycles_per_tick() const {
        if (speed_multiplier == 0.0) {
            // Unlimited mode: still run in slices for responsiveness
            return base_clock_hz / pacing_hz;
        }
        return effective_clock_hz() / pacing_hz;
    }

    /// Calculate wall-clock interval between pacing ticks.
    constexpr std::chrono::nanoseconds tick_interval() const {
        return std::chrono::nanoseconds(1'000'000'000 / pacing_hz);
    }

    /// Check if running in unlimited speed mode.
    constexpr bool is_unlimited() const {
        return speed_multiplier == 0.0;
    }
};

} // namespace beebium
