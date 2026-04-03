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
#include <cmath>
#include <cstdint>

namespace beebium {

/// PI controller for emulation pacing.
///
/// Computes the sleep duration for each pacing tick to keep the emulated
/// clock rate tracking wall-clock time at the target frequency. Uses a
/// proportional-integral controller with uncapped integral (debt) that
/// ensures time debt from I/O-skipped ticks is fully repaid over time.
///
/// The controller is a pure computation -- no threading, no clocks, no
/// side effects. This makes it unit-testable with synthetic time sequences.
///
/// Usage:
///   PacingController ctrl(2'000'000, 200);  // 2 MHz, 200 Hz ticks
///   // Each tick:
///   auto sleep_ns = ctrl.update(wall_elapsed_ns, cycles_executed, io_pending);
///   // sleep for sleep_ns nanoseconds, then run cycles_per_tick more cycles
///
class PacingController {
public:
    /// @param target_clock_hz  Target emulated clock frequency (e.g., 2000000)
    /// @param pacing_hz        Tick rate (e.g., 200)
    /// @param kp               Proportional gain (cycles of drift → ns of sleep adjustment)
    /// @param ki               Integral gain (accumulated drift → ns of sleep adjustment)
    PacingController(uint32_t target_clock_hz, uint32_t pacing_hz,
                     double kp = 750.0, double ki = 100.0)
        : target_clock_hz_(target_clock_hz)
        , cycles_per_tick_(target_clock_hz / pacing_hz)
        , base_interval_ns_(1'000'000'000.0 / pacing_hz)
        , kp_(kp)
        , ki_(ki) {}

    /// Update the controller with the latest tick's measurements.
    ///
    /// @param wall_elapsed_ns  Wall-clock nanoseconds since the controller was
    ///                         created (or reset). Monotonically increasing.
    /// @param total_cycles     Total emulated cycles executed since creation/reset.
    /// @param io_pending       True if I/O was pending this tick (informational).
    /// @return Recommended sleep duration in nanoseconds for this tick.
    ///         Zero means "run the next tick immediately" (catching up).
    int64_t update(int64_t wall_elapsed_ns, uint64_t total_cycles, bool io_pending) {
        (void)io_pending;  // Could be used for adaptive Ki in future

        // How many cycles SHOULD have been executed by now?
        double target_cycles = (static_cast<double>(wall_elapsed_ns) / 1e9)
                             * target_clock_hz_;

        // Drift: positive = ahead of real-time (need to slow down)
        double drift = static_cast<double>(total_cycles) - target_cycles;

        // Accumulate integral (uncapped -- debt is tracked fully)
        integral_ += drift;

        // PI output: base interval adjusted by proportional and integral terms
        double sleep_ns = base_interval_ns_
                        + kp_ * drift
                        + ki_ * integral_;

        // Clamp to [0, max_sleep]. Never sleep negative. Limit maximum
        // to 2x the base interval to avoid starving the emulation.
        double max_sleep = base_interval_ns_ * 2.0;
        sleep_ns = std::clamp(sleep_ns, 0.0, max_sleep);

        ++tick_count_;
        last_drift_ = drift;

        return static_cast<int64_t>(sleep_ns);
    }

    /// Reset the controller state (e.g., after a pause/resume).
    void reset() {
        integral_ = 0.0;
        tick_count_ = 0;
        last_drift_ = 0.0;
    }

    // --- Accessors for monitoring/testing ---

    double integral() const { return integral_; }
    double last_drift() const { return last_drift_; }
    uint64_t tick_count() const { return tick_count_; }
    uint32_t cycles_per_tick() const { return cycles_per_tick_; }
    double base_interval_ns() const { return base_interval_ns_; }
    double kp() const { return kp_; }
    double ki() const { return ki_; }

    void set_gains(double kp, double ki) { kp_ = kp; ki_ = ki; }

private:
    uint32_t target_clock_hz_;
    uint32_t cycles_per_tick_;
    double base_interval_ns_;
    double kp_;
    double ki_;

    // Controller state
    double integral_ = 0.0;
    double last_drift_ = 0.0;
    uint64_t tick_count_ = 0;
};

} // namespace beebium
