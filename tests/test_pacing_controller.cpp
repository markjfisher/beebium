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

// Tests for the PacingController PI controller.
//
// Uses simulated time sequences to verify that the controller maintains
// correct average clock rate, repays debt from I/O bursts, and converges
// without oscillation. Also used for gain tuning.

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

// Simulate a sequence of ticks and return (wall_time, total_cycles) pairs.
// The simulation models what happens when the emulation thread runs
// cycles_per_tick cycles per tick, then sleeps for whatever the controller
// recommends. The "execution time" for each batch is computed from the
// emulated clock rate (instant, negligible wall time on modern hardware).
struct TickResult {
    int64_t wall_ns;
    uint64_t cycles;
    int64_t sleep_ns;
    double drift;
    double integral;
};

std::vector<TickResult> simulate(
    PacingController& ctrl,
    int num_ticks,
    // io_burst_start..io_burst_end are ticks where io_pending=true and
    // the emulation runs an extra burst of cycles (simulating I/O-skipped ticks
    // where cycles execute at CPU speed, not wall-clock speed).
    int io_burst_start = -1,
    int io_burst_end = -1,
    // How long execution of cycles_per_tick actually takes in wall-clock ns.
    // On modern hardware this is ~0.5ms for 10,000 2MHz cycles.
    int64_t execution_ns = 500'000)
{
    std::vector<TickResult> results;
    int64_t wall_ns = 0;
    uint64_t total_cycles = 0;
    uint32_t cpt = ctrl.cycles_per_tick();

    for (int i = 0; i < num_ticks; i++) {
        bool io = (i >= io_burst_start && i < io_burst_end);

        // Execute one tick's worth of cycles
        total_cycles += cpt;
        wall_ns += execution_ns;

        // Ask controller how long to sleep
        int64_t sleep = ctrl.update(wall_ns, total_cycles, io);

        // If I/O is pending, the pacing clock skips sleep (sleep=0 requested
        // by the io_pending flag, separate from the controller). Model this
        // by not adding sleep time during I/O bursts.
        if (io) {
            sleep = 0;
        }

        wall_ns += sleep;

        results.push_back({
            wall_ns, total_cycles, sleep,
            ctrl.last_drift(), ctrl.integral()
        });
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
    PacingController ctrl(2'000'000, 200);  // 2 MHz, 200 Hz

    // Run 1000 ticks with no I/O -- should converge to 2 MHz
    auto results = simulate(ctrl, 1000);

    // Average clock rate over last 500 ticks should be very close to 2 MHz
    double rate = average_clock_rate(results, 500, 1000);
    REQUIRE_THAT(rate, WithinAbs(2'000'000, 50'000));  // Within 2.5%

    // Drift should be near zero in steady state
    REQUIRE(std::abs(results.back().drift) < 1000);
}


TEST_CASE("PacingController repays debt from I/O burst", "[pacing]") {
    PacingController ctrl(2'000'000, 200);

    // 100 ticks steady state, then 50 ticks I/O burst, then 850 ticks recovery
    auto results = simulate(ctrl, 1000, 100, 150);

    SECTION("drift is positive during I/O burst (emulation ahead)") {
        // At end of burst (tick 149), emulation should be ahead
        REQUIRE(results[149].drift > 0);
    }

    SECTION("drift recovers to near zero after burst") {
        // After recovery, drift should be small
        REQUIRE(std::abs(results[999].drift) < 1000);
    }

    SECTION("integral returns to near zero (debt fully repaid)") {
        REQUIRE(std::abs(results[999].integral) < 50'000);
    }

    SECTION("average rate over entire run matches target") {
        double rate = average_clock_rate(results, 0, 1000);
        REQUIRE_THAT(rate, WithinAbs(2'000'000, 100'000));  // Within 5%
    }

    SECTION("no tick sleeps more than 2x base interval") {
        double max_sleep = ctrl.base_interval_ns() * 2.0;
        for (auto& r : results) {
            REQUIRE(r.sleep_ns <= static_cast<int64_t>(max_sleep) + 1);
        }
    }
}


TEST_CASE("PacingController handles repeated short bursts", "[pacing]") {
    PacingController ctrl(2'000'000, 200);

    // Simulate periodic I/O: 10 ticks normal, 5 ticks burst, repeated
    int num_ticks = 2000;
    int64_t wall_ns = 0;
    uint64_t total_cycles = 0;
    uint32_t cpt = ctrl.cycles_per_tick();
    int64_t execution_ns = 500'000;

    for (int i = 0; i < num_ticks; i++) {
        bool io = (i % 15) >= 10;  // burst every 15 ticks

        total_cycles += cpt;
        wall_ns += execution_ns;

        int64_t sleep = ctrl.update(wall_ns, total_cycles, io);
        if (io) sleep = 0;
        wall_ns += sleep;
    }

    // Average rate over entire run should be close to 2 MHz
    double wall_secs = static_cast<double>(wall_ns) / 1e9;
    double rate = static_cast<double>(total_cycles) / wall_secs;
    REQUIRE_THAT(rate, WithinAbs(2'000'000, 200'000));  // Within 10%

    // Integral should not diverge
    REQUIRE(std::abs(ctrl.integral()) < 500'000);
}


TEST_CASE("PacingController 3 MHz parasite", "[pacing]") {
    PacingController ctrl(3'000'000, 200);  // 3 MHz, 200 Hz

    auto results = simulate(ctrl, 1000, 100, 150, 300'000);

    double rate = average_clock_rate(results, 500, 1000);
    REQUIRE_THAT(rate, WithinAbs(3'000'000, 100'000));
}


TEST_CASE("PacingController gain tuning search", "[pacing][tuning]") {
    // Search a grid of Kp/Ki values and report which give the best
    // convergence after a 50-tick I/O burst. "Best" = lowest drift
    // and integral after 200 recovery ticks, with no oscillation.

    struct GainResult {
        double kp, ki;
        double final_drift;
        double final_integral;
        double max_sleep_ratio;  // max sleep / base interval
        double recovery_rate;    // avg MHz during recovery
        bool oscillates;         // sign changes in drift during recovery
    };

    std::vector<GainResult> results;

    for (double kp : {100.0, 250.0, 500.0, 750.0, 1000.0}) {
        for (double ki : {10.0, 25.0, 50.0, 100.0, 200.0}) {
            PacingController ctrl(2'000'000, 200, kp, ki);

            auto ticks = simulate(ctrl, 500, 50, 100);

            // Check for oscillation: count sign changes in drift during recovery
            int sign_changes = 0;
            for (size_t i = 101; i < ticks.size(); i++) {
                if ((ticks[i].drift > 0) != (ticks[i-1].drift > 0) &&
                    std::abs(ticks[i].drift) > 100) {
                    sign_changes++;
                }
            }

            double max_ratio = 0;
            for (auto& t : ticks) {
                double ratio = static_cast<double>(t.sleep_ns) / ctrl.base_interval_ns();
                max_ratio = std::max(max_ratio, ratio);
            }

            double rate = average_clock_rate(ticks, 200, 500);

            results.push_back({
                kp, ki,
                ticks.back().drift,
                ticks.back().integral,
                max_ratio,
                rate,
                sign_changes > 5
            });
        }
    }

    // Print results table for manual inspection
    std::cout << "\n  Gain tuning results (50-tick burst, 400 ticks recovery):\n";
    std::cout << "  " << std::string(90, '-') << "\n";
    std::cout << "  Kp      Ki      Drift     Integral   MaxSleep  Rate(MHz)  Osc?\n";
    std::cout << "  " << std::string(90, '-') << "\n";

    GainResult best = results[0];
    double best_score = 1e18;

    for (auto& r : results) {
        double score = std::abs(r.final_drift) + std::abs(r.final_integral) / 100.0
                     + (r.oscillates ? 1e6 : 0);

        std::cout << "  " << std::setw(6) << r.kp
                  << "  " << std::setw(6) << r.ki
                  << "  " << std::setw(9) << std::fixed << std::setprecision(0) << r.final_drift
                  << "  " << std::setw(10) << r.final_integral
                  << "  " << std::setw(8) << std::setprecision(2) << r.max_sleep_ratio
                  << "  " << std::setw(9) << std::setprecision(0) << r.recovery_rate / 1e6
                  << "  " << (r.oscillates ? "YES" : "no") << "\n";

        if (score < best_score) {
            best_score = score;
            best = r;
        }
    }

    std::cout << "\n  Best: Kp=" << best.kp << " Ki=" << best.ki
              << " (drift=" << best.final_drift
              << " integral=" << best.final_integral << ")\n\n";

    // Assert the best gains produce acceptable results
    REQUIRE_FALSE(best.oscillates);
    REQUIRE(std::abs(best.final_drift) < 5000);
}
