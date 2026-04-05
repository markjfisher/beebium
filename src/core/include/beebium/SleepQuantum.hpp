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

/// Measure the platform's minimum reliable sleep duration.
///
/// Fires a burst of minimum-length sleep calls using PlatformSleep
/// and returns the median actual duration. This measures the real
/// quantum we'll get in production, accounting for platform differences:
///
/// - macOS: ~2-100us (nanosleep)
/// - Linux: ~50-200us (nanosleep)
/// - Windows with HIGH_RES timer: ~5us
/// - Windows without HIGH_RES: ~15.6ms (system timer)
///
/// Called once at emulator startup. Takes a few milliseconds.
inline std::chrono::nanoseconds measure_sleep_quantum(
        const PlatformSleep& sleeper, int samples = 100) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;

    std::vector<int64_t> durations;
    durations.reserve(samples);

    // Warm up (first few sleeps may be slower)
    for (int i = 0; i < 5; i++) {
        sleeper.sleep(nanoseconds(1));
    }

    for (int i = 0; i < samples; i++) {
        auto before = Clock::now();
        sleeper.sleep(nanoseconds(1));
        auto after = Clock::now();
        auto ns = duration_cast<nanoseconds>(after - before).count();
        durations.push_back(ns);
    }

    // Return the median (robust against outliers)
    std::sort(durations.begin(), durations.end());
    auto median = durations[static_cast<size_t>(samples / 2)];

    // Floor at 100us (sanity -- no platform is faster than this reliably)
    median = std::max(median, int64_t{100'000});

    return nanoseconds(median);
}

} // namespace beebium
