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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "beebium/indicators/IndicatorFilter.hpp"

#include <chrono>
#include <thread>

using namespace beebium;
using namespace std::chrono_literals;

// Helper to create time points relative to a base
using steady_clock = std::chrono::steady_clock;
using time_point = steady_clock::time_point;

// =============================================================================
// PassthroughFilter Tests
// =============================================================================

TEST_CASE("PassthroughFilter initial value is zero", "[indicators][filter]") {
    PassthroughFilter filter;
    REQUIRE(filter.sample(steady_clock::now()) == 0);
}

TEST_CASE("PassthroughFilter returns most recent value", "[indicators][filter]") {
    PassthroughFilter filter;
    auto now = steady_clock::now();

    filter.update(255, now);
    REQUIRE(filter.sample(now) == 255);

    filter.update(128, now + 1ms);
    REQUIRE(filter.sample(now + 1ms) == 128);

    filter.update(0, now + 2ms);
    REQUIRE(filter.sample(now + 2ms) == 0);
}

TEST_CASE("PassthroughFilter ignores timestamp", "[indicators][filter]") {
    PassthroughFilter filter;
    auto now = steady_clock::now();

    // Update with one timestamp, sample with different timestamp
    filter.update(200, now);
    REQUIRE(filter.sample(now + 100ms) == 200);
}

// =============================================================================
// DebounceFilter Tests
// =============================================================================

TEST_CASE("DebounceFilter initial value is zero", "[indicators][filter]") {
    DebounceFilter filter(50ms);
    REQUIRE(filter.sample(steady_clock::now()) == 0);
}

TEST_CASE("DebounceFilter does not change immediately", "[indicators][filter]") {
    DebounceFilter filter(50ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    // Value should still be 0 because 50ms hasn't elapsed
    REQUIRE(filter.sample(now + 10ms) == 0);
    REQUIRE(filter.sample(now + 49ms) == 0);
}

TEST_CASE("DebounceFilter changes after stable duration", "[indicators][filter]") {
    DebounceFilter filter(50ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    // After 50ms of stable input, value should change
    REQUIRE(filter.sample(now + 50ms) == 255);
    REQUIRE(filter.sample(now + 100ms) == 255);
}

TEST_CASE("DebounceFilter resets timer on value change", "[indicators][filter]") {
    DebounceFilter filter(50ms);
    auto now = steady_clock::now();

    // Start with 255
    filter.update(255, now);
    // Change to 0 before debounce completes
    filter.update(0, now + 30ms);
    // At 50ms from start, we've only had 0 for 20ms
    REQUIRE(filter.sample(now + 50ms) == 0);  // Still 0 (initial)
    // At 80ms from start (50ms after changing to 0), still original 0
    REQUIRE(filter.sample(now + 80ms) == 0);
}

TEST_CASE("DebounceFilter handles rapid toggling", "[indicators][filter]") {
    DebounceFilter filter(50ms);
    auto now = steady_clock::now();

    // Rapid toggling should never cause output to change
    for (int i = 0; i < 10; ++i) {
        filter.update(255, now + std::chrono::milliseconds(i * 10));
        filter.update(0, now + std::chrono::milliseconds(i * 10 + 5));
    }

    // Output should still be initial value (0)
    REQUIRE(filter.sample(now + 100ms) == 0);
}

TEST_CASE("DebounceFilter with zero duration acts like passthrough", "[indicators][filter]") {
    DebounceFilter filter(0ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    REQUIRE(filter.sample(now) == 255);

    filter.update(0, now + 1ms);
    REQUIRE(filter.sample(now + 1ms) == 0);
}

// =============================================================================
// QuantizedDutyCycleFilter Tests
// =============================================================================

TEST_CASE("QuantizedDutyCycleFilter<2> fully off returns zero", "[indicators][filter]") {
    QuantizedDutyCycleFilter<2> filter(100ms);
    auto now = steady_clock::now();

    REQUIRE(filter.sample(now) == 0);
    REQUIRE(filter.sample(now + 100ms) == 0);
}

TEST_CASE("QuantizedDutyCycleFilter<2> ramps up through quantized on buckets", "[indicators][filter]") {
    QuantizedDutyCycleFilter<2> filter(100ms);
    auto now = steady_clock::now();

    filter.update(255, now);

    // Over the first 100ms wall-clock window the reported brightness ramps as
    // the duty-cycle window fills with ON time.
    REQUIRE(filter.sample(now + 25ms) == 0);
    REQUIRE(filter.sample(now + 50ms) == 64);
    REQUIRE(filter.sample(now + 75ms) == 128);
    REQUIRE(filter.sample(now + 100ms) == 255);
}

TEST_CASE("QuantizedDutyCycleFilter<2> decays back to off through low buckets", "[indicators][filter]") {
    QuantizedDutyCycleFilter<2> filter(100ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    REQUIRE(filter.sample(now + 100ms) == 255);

    filter.update(0, now + 100ms);

    // As the 100ms window slides forward, the published value falls from fully
    // on to the low bucket, then finally to exact off.
    REQUIRE(filter.sample(now + 150ms) == 64);
    REQUIRE(filter.sample(now + 200ms) == 0);
}

TEST_CASE("QuantizedDutyCycleFilter<2> maps 75 percent duty cycle to 128", "[indicators][filter]") {
    QuantizedDutyCycleFilter<2> filter(100ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    filter.update(0, now + 75ms);

    REQUIRE(filter.sample(now + 100ms) == 128);
}

// =============================================================================
// DutyCycleFilter Tests
// =============================================================================

TEST_CASE("DutyCycleFilter initial value is zero", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    REQUIRE(filter.sample(steady_clock::now()) == 0);
}

TEST_CASE("DutyCycleFilter fully on returns 255", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // Turn on at start of window
    filter.update(255, now);
    // Sample at end of window - should be fully on
    REQUIRE(filter.sample(now + 100ms) == 255);
}

TEST_CASE("DutyCycleFilter fully off returns 0", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // Never turn on, sample should be 0
    REQUIRE(filter.sample(now + 100ms) == 0);
}

TEST_CASE("DutyCycleFilter 50% duty cycle returns ~127", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // On for first 50ms, off for next 50ms
    filter.update(255, now);
    filter.update(0, now + 50ms);

    // Sample at 100ms - should be ~50% = 127-128
    auto value = filter.sample(now + 100ms);
    REQUIRE(value >= 126);
    REQUIRE(value <= 128);
}

TEST_CASE("DutyCycleFilter 25% duty cycle returns ~63", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // On for first 25ms, off for rest
    filter.update(255, now);
    filter.update(0, now + 25ms);

    // Sample at 100ms - should be ~25% = 63-64
    auto value = filter.sample(now + 100ms);
    REQUIRE(value >= 62);
    REQUIRE(value <= 65);
}

TEST_CASE("DutyCycleFilter 75% duty cycle returns ~191", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // On for first 75ms, off for rest
    filter.update(255, now);
    filter.update(0, now + 75ms);

    // Sample at 100ms - should be ~75% = 191
    auto value = filter.sample(now + 100ms);
    REQUIRE(value >= 189);
    REQUIRE(value <= 193);
}

TEST_CASE("DutyCycleFilter sliding window discards old transitions", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // Turn on at t=0
    filter.update(255, now);
    // Turn off at t=50ms
    filter.update(0, now + 50ms);

    // At t=100ms, the window is [0, 100ms], on for 50ms = 50%
    auto value1 = filter.sample(now + 100ms);
    REQUIRE(value1 >= 126);
    REQUIRE(value1 <= 128);

    // At t=150ms, the window is [50ms, 150ms], on for 0ms = 0%
    // (the on transition at t=0 is now outside the window)
    auto value2 = filter.sample(now + 150ms);
    REQUIRE(value2 == 0);
}

TEST_CASE("DutyCycleFilter handles multiple transitions in window", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // PWM-like pattern: on 10ms, off 10ms, repeated
    for (int i = 0; i < 5; ++i) {
        filter.update(255, now + std::chrono::milliseconds(i * 20));
        filter.update(0, now + std::chrono::milliseconds(i * 20 + 10));
    }

    // Sample at 100ms - should be ~50%
    auto value = filter.sample(now + 100ms);
    REQUIRE(value >= 120);
    REQUIRE(value <= 135);
}

TEST_CASE("DutyCycleFilter state persists across window", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // Turn on and leave on
    filter.update(255, now);

    // Sample much later - should still be fully on
    REQUIRE(filter.sample(now + 500ms) == 255);
}

TEST_CASE("DutyCycleFilter handles threshold correctly", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // Values > 127 are "on", <= 127 are "off"
    filter.update(128, now);  // Should count as on
    REQUIRE(filter.sample(now + 100ms) == 255);

    DutyCycleFilter filter2(100ms);
    filter2.update(127, now);  // Should count as off
    REQUIRE(filter2.sample(now + 100ms) == 0);
}

TEST_CASE("DutyCycleFilter with very short window", "[indicators][filter]") {
    DutyCycleFilter filter(10ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    filter.update(0, now + 5ms);

    // 50% duty cycle over 10ms window
    auto value = filter.sample(now + 10ms);
    REQUIRE(value >= 126);
    REQUIRE(value <= 128);
}

TEST_CASE("DutyCycleFilter transition at exact window boundary", "[indicators][filter]") {
    DutyCycleFilter filter(100ms);
    auto now = steady_clock::now();

    // Turn on exactly at window start
    filter.update(255, now);

    // Turn off exactly at window end
    filter.update(0, now + 100ms);

    // Sample at window end - entire window was on
    auto value = filter.sample(now + 100ms);
    REQUIRE(value == 255);
}

// =============================================================================
// RetriggerableMonostableFilter Tests
// =============================================================================
//
// Semantics: a trigger (any nonzero update) sets the output high and (re)starts
// a timer. The output remains high for `pulse_width` from the most recent
// trigger, then falls. Subsequent triggers within the active interval extend
// the high duration to `pulse_width` from each new trigger. OFF inputs are
// ignored entirely; only the timer governs the falling edge.
//
// The active interval is half-open: sample(t) is high iff
// t < last_trigger + pulse_width.

TEST_CASE("RetriggerableMonostableFilter initial value is zero", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    REQUIRE(filter.sample(steady_clock::now()) == 0);
}

TEST_CASE("RetriggerableMonostableFilter triggers high immediately", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    REQUIRE(filter.sample(now) == 255);
}

TEST_CASE("RetriggerableMonostableFilter holds high through pulse window", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    REQUIRE(filter.sample(now + 1ms) == 255);
    REQUIRE(filter.sample(now + 40ms) == 255);
    REQUIRE(filter.sample(now + 79ms) == 255);
}

TEST_CASE("RetriggerableMonostableFilter falls at end of pulse window", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    // Exactly at expiration -- half-open interval [trigger, trigger+width).
    REQUIRE(filter.sample(now + 80ms) == 0);
    REQUIRE(filter.sample(now + 200ms) == 0);
}

TEST_CASE("RetriggerableMonostableFilter retrigger extends pulse", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    // Retrigger at t=50ms -- new expiration at t=130ms.
    filter.update(255, now + 50ms);
    REQUIRE(filter.sample(now + 100ms) == 255);
    REQUIRE(filter.sample(now + 129ms) == 255);
    REQUIRE(filter.sample(now + 130ms) == 0);
}

TEST_CASE("RetriggerableMonostableFilter ignores OFF input", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    filter.update(0, now + 10ms);
    // OFF must not interrupt the active pulse.
    REQUIRE(filter.sample(now + 50ms) == 255);
    REQUIRE(filter.sample(now + 79ms) == 255);
    // Timer alone governs the falling edge.
    REQUIRE(filter.sample(now + 80ms) == 0);
}

TEST_CASE("RetriggerableMonostableFilter OFF does not retrigger", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    filter.update(0, now + 60ms);
    // OFF at t=60 must not reset the timer; pulse still ends at t=80.
    REQUIRE(filter.sample(now + 80ms) == 0);
}

TEST_CASE("RetriggerableMonostableFilter re-fires after expiry", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    REQUIRE(filter.sample(now + 80ms) == 0);

    filter.update(255, now + 200ms);
    REQUIRE(filter.sample(now + 200ms) == 255);
    REQUIRE(filter.sample(now + 279ms) == 255);
    REQUIRE(filter.sample(now + 280ms) == 0);
}

TEST_CASE("RetriggerableMonostableFilter zero-width pulse", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(0ms);
    auto now = steady_clock::now();

    filter.update(255, now);
    // With zero width, sample(now) is at the expiration boundary -- already low.
    REQUIRE(filter.sample(now) == 0);
}

TEST_CASE("RetriggerableMonostableFilter rapid retriggers stay high", "[indicators][filter]") {
    RetriggerableMonostableFilter filter(80ms);
    auto now = steady_clock::now();

    // Burst of triggers every 10ms for 200ms; should remain high throughout.
    for (int i = 0; i < 20; ++i) {
        filter.update(255, now + std::chrono::milliseconds(i * 10));
        REQUIRE(filter.sample(now + std::chrono::milliseconds(i * 10)) == 255);
    }
    // Last trigger at t=190ms -- still high until t=270ms.
    REQUIRE(filter.sample(now + 269ms) == 255);
    REQUIRE(filter.sample(now + 270ms) == 0);
}
