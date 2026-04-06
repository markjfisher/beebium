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

#include "PlatformSleep.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace beebium {

/// Measure the platform's actual sleep quantum.
///
/// Requests a short sleep (100us) using PlatformSleep and measures
/// the actual wall-clock duration over many samples, returning the
/// median. This captures the real minimum sleep duration including
/// platform overhead and timer granularity:
///
/// - macOS: ~130us (nanosleep overshoots 100us slightly)
/// - Linux: ~150us (similar)
/// - Windows + HIGH_RES + NtSetTimerResolution: ~496us
/// - Windows without HIGH_RES: ~15.6ms (system timer)
///
/// The pacing clock sleeps for this measured quantum each tick, and
/// the deficit controller uses it to compute the correct number of
/// cycles per tick. The measurement must reflect what the timer
/// thread will actually experience.
///
/// Called once at emulator startup. Takes a fraction of a second.
inline std::chrono::nanoseconds measure_sleep_quantum(
        const PlatformSleep& sleeper, int samples = 100) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;

    // Sleep for a short target duration. On all platforms this is
    // within the "floor" zone where the actual duration is dominated
    // by the platform's minimum sleep granularity, which is exactly
    // the quantum we want to measure.
    constexpr auto target = microseconds(100);

    std::vector<int64_t> durations;
    durations.reserve(samples);

    // Warm up (first few sleeps may be slower)
    for (int i = 0; i < 5; i++) {
        sleeper.sleep(target);
    }

    for (int i = 0; i < samples; i++) {
        auto before = Clock::now();
        sleeper.sleep(target);
        auto after = Clock::now();
        auto ns = duration_cast<nanoseconds>(after - before).count();
        durations.push_back(ns);
    }

    // Return the median (robust against outliers)
    std::sort(durations.begin(), durations.end());
    return nanoseconds(durations[static_cast<size_t>(samples / 2)]);
}

} // namespace beebium
