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

#pragma once

#include <algorithm>
#include <cstdint>

namespace beebium {

/// Deficit-based pacing controller.
///
/// Computes how many cycles to execute by calculating the deficit between
/// target cumulative cycles (from wall-clock time) and actual cumulative
/// cycles. The result naturally adapts to variable tick periods:
///
/// - Normal tick (500us quantum): deficit ≈ 1,000 cycles (at 2 MHz)
/// - After OS sleep overshoot (1.5ms): deficit ≈ 3,000 (catches up)
/// - After running ahead: deficit ≈ 0 (pauses until wall-clock catches up)
///
/// The max_cycles clamp prevents concentrated catch-up after deficits,
/// spreading recovery across many ticks imperceptibly.
///
/// Pure computation -- no threading, no clocks, no side effects.
///
class PacingController {
public:
    /// @param target_clock_hz  Target crystal frequency (e.g., 2000000)
    /// @param quantum_ns       Sleep quantum in nanoseconds (from measurement)
    /// @param max_ratio        Maximum cycles as multiple of nominal per-quantum
    ///                         cycles. 1.015 = allow 1.5% overshoot for smooth
    ///                         catch-up after deficits.
    PacingController(uint32_t target_clock_hz, int64_t quantum_ns,
                     double max_ratio = 3.0)
        : target_clock_hz_(target_clock_hz)
        , nominal_cycles_(static_cast<uint32_t>(
              static_cast<double>(target_clock_hz) * quantum_ns / 1e9))
        , max_cycles_(static_cast<uint32_t>(nominal_cycles_ * max_ratio)) {}

    /// Compute cycles to execute in the next tick.
    ///
    /// @param wall_elapsed_ns  Wall-clock nanoseconds since start/reset.
    /// @param total_cycles     Total crystal ticks executed since start/reset.
    /// @return Number of cycles to execute. Zero if ahead of real-time.
    uint32_t update(int64_t wall_elapsed_ns, uint64_t total_cycles) {
        double target = (static_cast<double>(wall_elapsed_ns) / 1e9)
                      * target_clock_hz_;
        double deficit = target - static_cast<double>(total_cycles);

        last_deficit_ = deficit;
        ++tick_count_;

        if (deficit <= 0.0) return 0;
        if (deficit > static_cast<double>(max_cycles_)) return max_cycles_;
        return static_cast<uint32_t>(deficit);
    }

    void reset() {
        last_deficit_ = 0.0;
        tick_count_ = 0;
    }

    // --- Monitoring ---
    double last_deficit() const { return last_deficit_; }
    uint64_t tick_count() const { return tick_count_; }
    uint32_t nominal_cycles() const { return nominal_cycles_; }
    uint32_t max_cycles() const { return max_cycles_; }

private:
    uint32_t target_clock_hz_;
    uint32_t nominal_cycles_;   // cycles per quantum at 1x speed
    uint32_t max_cycles_;       // ceiling per tick

    double last_deficit_ = 0.0;
    uint64_t tick_count_ = 0;
};

} // namespace beebium
