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

// Tests for the deficit-based PacingController.
//
// The controller outputs cycles-per-tick based on how many cycles are
// "owed" at the current wall-clock time. Tests simulate tick sequences
// with I/O bursts (shortened tick periods) and verify correct average rate.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <beebium/PacingController.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace beebium;
using Catch::Matchers::WithinAbs;

namespace {

struct TickResult {
    int64_t wall_ns;
    uint64_t cycles;
    uint32_t cycles_this_tick;
    double drift;
};

/// Simulate ticks with optional I/O burst (shortened tick periods).
std::vector<TickResult> simulate(
    PacingController& ctrl,
    int num_ticks,
    int io_burst_start = -1,
    int io_burst_end = -1,
    int64_t base_interval_ns = 5'000'000,
    int64_t io_interval_ns = 200'000)
{
    std::vector<TickResult> results;
    int64_t wall_ns = 0;
    uint64_t total_cycles = 0;

    for (int i = 0; i < num_ticks; i++) {
        bool io = (i >= io_burst_start && i < io_burst_end);

        uint32_t cycles = ctrl.update(wall_ns, total_cycles);
        total_cycles += cycles;
        wall_ns += io ? io_interval_ns : base_interval_ns;

        results.push_back({wall_ns, total_cycles, cycles, ctrl.last_drift()});
    }
    return results;
}

double average_clock_rate(const std::vector<TickResult>& results,
                          int from_tick, int to_tick) {
    if (from_tick >= to_tick || to_tick > static_cast<int>(results.size()))
        return 0;
    auto& first = results[static_cast<size_t>(from_tick)];
    auto& last = results[static_cast<size_t>(to_tick - 1)];
    double wall_secs = static_cast<double>(last.wall_ns - first.wall_ns) / 1e9;
    double cycles = static_cast<double>(last.cycles - first.cycles);
    return wall_secs > 0 ? cycles / wall_secs : 0;
}

} // namespace


TEST_CASE("PacingController steady state matches target rate", "[pacing]") {
    PacingController ctrl(2'000'000, 200);

    auto results = simulate(ctrl, 1000);

    double rate = average_clock_rate(results, 500, 1000);
    REQUIRE_THAT(rate, WithinAbs(2'000'000, 10'000));

    // Cycles per tick should be exactly base (10,000) in steady state
    REQUIRE(results.back().cycles_this_tick == 10'000);
}


TEST_CASE("PacingController adapts to shortened I/O tick", "[pacing]") {
    PacingController ctrl(2'000'000, 200);

    auto results = simulate(ctrl, 1000, 100, 150);

    SECTION("cycles reduce during burst") {
        // During burst: 200us tick at 2 MHz = 400 cycles target
        // First burst tick runs 10,000 (hasn't seen shortened tick yet)
        // Second burst tick onwards: deficit is ~400
        REQUIRE(results[102].cycles_this_tick <= 1000);
    }

    SECTION("burst rate is close to target") {
        // Skip first 2 burst ticks (transient), check rest
        double rate = average_clock_rate(results, 103, 150);
        REQUIRE_THAT(rate, WithinAbs(2'000'000, 100'000));
    }

    SECTION("recovery is fast") {
        // After burst ends, should return to ~10,000 within a few ticks
        REQUIRE(results[155].cycles_this_tick >= 8'000);
        REQUIRE(results[160].cycles_this_tick >= 9'500);
    }

    SECTION("overall rate matches target") {
        double rate = average_clock_rate(results, 0, 1000);
        REQUIRE_THAT(rate, WithinAbs(2'000'000, 50'000));
    }
}


TEST_CASE("PacingController handles repeated short bursts", "[pacing]") {
    PacingController ctrl(2'000'000, 200);

    int num_ticks = 2000;
    int64_t wall_ns = 0;
    uint64_t total_cycles = 0;

    for (int i = 0; i < num_ticks; i++) {
        bool io = (i % 15) >= 10;
        uint32_t cycles = ctrl.update(wall_ns, total_cycles);
        total_cycles += cycles;
        wall_ns += io ? 200'000 : 5'000'000;
    }

    double wall_secs = static_cast<double>(wall_ns) / 1e9;
    double rate = static_cast<double>(total_cycles) / wall_secs;
    REQUIRE_THAT(rate, WithinAbs(2'000'000, 100'000));
}


TEST_CASE("PacingController 3 MHz parasite", "[pacing]") {
    PacingController ctrl(3'000'000, 200);

    auto results = simulate(ctrl, 1000, 100, 150);

    double rate = average_clock_rate(results, 200, 1000);
    REQUIRE_THAT(rate, WithinAbs(3'000'000, 100'000));

    // Base cycles should be 15,000 in steady state
    REQUIRE(results.back().cycles_this_tick == 15'000);
}


TEST_CASE("PacingController recovery from burst is bounded", "[pacing]") {
    PacingController ctrl(2'000'000, 200);

    // Long burst: 200 ticks at 200us
    auto results = simulate(ctrl, 500, 50, 250);

    SECTION("first burst tick overshoots") {
        // First tick still runs base cycles (deficit = 10,000 before burst)
        REQUIRE(results[50].cycles_this_tick == 10'000);
    }

    SECTION("subsequent burst ticks are correct") {
        // Deficit for 200us tick at 2 MHz = 400 cycles
        // After first overshoot, controller should settle near 400
        REQUIRE(results[55].cycles_this_tick <= 1000);
    }

    SECTION("drift after burst is bounded") {
        // Excess from first burst tick only: ~9,600 cycles
        REQUIRE(std::abs(results[260].drift) < 20'000);
    }

    SECTION("recovery completes within a few ticks") {
        // After burst, cycles should return to ~10,000 within 5 ticks
        REQUIRE(results[255].cycles_this_tick >= 5'000);
    }
}
