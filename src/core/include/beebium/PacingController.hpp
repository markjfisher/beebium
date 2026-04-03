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

/// Deficit-based pacing controller (PWM mode).
///
/// Computes the number of cycles to execute per tick by calculating how
/// many cycles are "owed" at the current wall-clock time: the difference
/// between target cumulative cycles and actual cumulative cycles.
///
/// This naturally adapts to variable tick periods:
/// - During a normal 5ms tick: deficit ≈ 10,000 cycles (at 2 MHz)
/// - During a 200μs I/O-interrupted tick: deficit ≈ 400 cycles
/// - After an I/O burst: deficit is reduced by excess cycles, causing
///   one or two recovery ticks at reduced speed, then back to normal
///
/// No PI gains, no integral, no anti-windup. The deficit IS the control
/// signal -- it directly tells the emulation how many cycles to run.
///
/// The controller is a pure computation -- no threading, no clocks, no
/// side effects. This makes it unit-testable with synthetic time sequences.
///
class PacingController {
public:
    /// @param target_clock_hz  Target emulated clock frequency (e.g., 2000000)
    /// @param pacing_hz        Tick rate (e.g., 200) -- used only for base_cycles
    /// @param max_cycles_ratio Ceiling as multiple of base cycles (e.g., 1.5 = 150%)
    PacingController(uint32_t target_clock_hz, uint32_t pacing_hz,
                     double max_cycles_ratio = 1.5)
        : target_clock_hz_(target_clock_hz)
        , base_cycles_(target_clock_hz / pacing_hz)
        , max_cycles_(static_cast<uint32_t>(base_cycles_ * max_cycles_ratio)) {}

    /// Compute cycles to execute in the next tick.
    ///
    /// @param wall_elapsed_ns  Wall-clock nanoseconds since start/reset.
    /// @param total_cycles     Total emulated cycles executed since start/reset.
    /// @return Number of cycles to execute. Zero if ahead of real-time.
    uint32_t update(int64_t wall_elapsed_ns, uint64_t total_cycles) {
        // How many cycles SHOULD have been executed by now?
        double target = (static_cast<double>(wall_elapsed_ns) / 1e9)
                      * target_clock_hz_;

        // Deficit: how many cycles are "owed". Positive = behind, need to run.
        double deficit = target - static_cast<double>(total_cycles);

        last_drift_ = -deficit;  // drift = ahead (positive = ahead, for monitoring)

        ++tick_count_;

        return static_cast<uint32_t>(std::clamp(deficit, 0.0,
            static_cast<double>(max_cycles_)));
    }

    /// Reset the controller state (e.g., after a pause/resume).
    void reset() {
        tick_count_ = 0;
        last_drift_ = 0.0;
    }

    // --- Accessors for monitoring/testing ---

    double last_drift() const { return last_drift_; }
    double integral() const { return 0.0; }  // No integral in deficit mode
    uint64_t tick_count() const { return tick_count_; }
    uint32_t base_cycles() const { return base_cycles_; }

private:
    uint32_t target_clock_hz_;
    uint32_t base_cycles_;
    uint32_t max_cycles_;

    double last_drift_ = 0.0;
    uint64_t tick_count_ = 0;
};

} // namespace beebium
