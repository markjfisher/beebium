// Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "beebium/indicators/Indicators.hpp"
#include "beebium/indicators/IndicatorFilter.hpp"

#include <chrono>
#include <thread>
#include <algorithm>

using namespace beebium;
using namespace std::chrono_literals;

// =============================================================================
// Indicator Registration Tests
// =============================================================================

TEST_CASE("Indicators register_indicator returns sequential IDs", "[indicators]") {
    Indicators indicators;

    auto id0 = indicators.register_indicator("led-0", std::make_unique<PassthroughFilter>());
    auto id1 = indicators.register_indicator("led-1", std::make_unique<PassthroughFilter>());
    auto id2 = indicators.register_indicator("led-2", std::make_unique<PassthroughFilter>());

    REQUIRE(id0 == 0);
    REQUIRE(id1 == 1);
    REQUIRE(id2 == 2);
}

TEST_CASE("Indicators names returns registered names", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("caps-lock-led", std::make_unique<PassthroughFilter>());
    indicators.register_indicator("shift-lock-led", std::make_unique<PassthroughFilter>());

    auto names = indicators.names();
    REQUIRE(names.size() == 2);
    REQUIRE(std::find(names.begin(), names.end(), "caps-lock-led") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "shift-lock-led") != names.end());
}

TEST_CASE("Indicators metadata stored and retrieved", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator(
        "test-led",
        std::make_unique<PassthroughFilter>(),
        {{"label", "Test LED"}, {"color", "red"}}
    );

    auto meta = indicators.metadata("test-led");
    REQUIRE(meta.size() == 2);
    REQUIRE(meta["label"] == "Test LED");
    REQUIRE(meta["color"] == "red");
}

TEST_CASE("Indicators metadata for unknown name returns empty map", "[indicators]") {
    Indicators indicators;

    auto meta = indicators.metadata("nonexistent");
    REQUIRE(meta.empty());
}

// =============================================================================
// Indicator Value Tests (without consumer thread)
// =============================================================================

TEST_CASE("Indicators initial values are zero", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    REQUIRE(indicators.get("led") == 0);
}

TEST_CASE("Indicators get unknown name returns zero", "[indicators]") {
    Indicators indicators;

    REQUIRE(indicators.get("nonexistent") == 0);
}

TEST_CASE("Indicators values returns all values", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("led-0", std::make_unique<PassthroughFilter>());
    indicators.register_indicator("led-1", std::make_unique<PassthroughFilter>());

    auto all = indicators.values();
    REQUIRE(all.size() == 2);
    REQUIRE(all["led-0"] == 0);
    REQUIRE(all["led-1"] == 0);
}

TEST_CASE("Indicators set by name for unknown name returns false", "[indicators]") {
    Indicators indicators;

    REQUIRE_FALSE(indicators.set("nonexistent", 255));
}

// =============================================================================
// Indicator Consumer Thread Tests
// =============================================================================

TEST_CASE("Indicators start and stop lifecycle", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    REQUIRE_FALSE(indicators.is_running());

    indicators.start();
    REQUIRE(indicators.is_running());

    indicators.stop();
    REQUIRE_FALSE(indicators.is_running());
}

TEST_CASE("Indicators double start is safe", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    indicators.start();
    indicators.start();  // Should be no-op
    REQUIRE(indicators.is_running());

    indicators.stop();
}

TEST_CASE("Indicators double stop is safe", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    indicators.start();
    indicators.stop();
    indicators.stop();  // Should be no-op
    REQUIRE_FALSE(indicators.is_running());
}

TEST_CASE("Indicators set by ID updates published value via consumer", "[indicators]") {
    Indicators indicators;

    auto id = indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    indicators.start();

    // Set value
    REQUIRE(indicators.set(id, 255));

    // Wait for consumer to process (consumer runs at ~50Hz = 20ms)
    std::this_thread::sleep_for(50ms);

    REQUIRE(indicators.get("led") == 255);

    indicators.stop();
}

TEST_CASE("Indicators set by name updates published value via consumer", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    indicators.start();

    // Set value by name
    REQUIRE(indicators.set("led", 128));

    // Wait for consumer to process
    std::this_thread::sleep_for(50ms);

    REQUIRE(indicators.get("led") == 128);

    indicators.stop();
}

TEST_CASE("Indicators batch set updates multiple values", "[indicators]") {
    Indicators indicators;

    auto id0 = indicators.register_indicator("led-0", std::make_unique<PassthroughFilter>());
    auto id1 = indicators.register_indicator("led-1", std::make_unique<PassthroughFilter>());

    indicators.start();

    // Batch update
    REQUIRE(indicators.set({{id0, 100}, {id1, 200}}));

    // Wait for consumer to process
    std::this_thread::sleep_for(50ms);

    REQUIRE(indicators.get("led-0") == 100);
    REQUIRE(indicators.get("led-1") == 200);

    indicators.stop();
}

TEST_CASE("Indicators sequence increments on value change", "[indicators]") {
    Indicators indicators;

    auto id = indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    indicators.start();

    auto seq1 = indicators.sequence();

    // Set value
    indicators.set(id, 255);

    // Wait for consumer to process
    std::this_thread::sleep_for(50ms);

    auto seq2 = indicators.sequence();
    REQUIRE(seq2 > seq1);

    indicators.stop();
}

TEST_CASE("Indicators sequence does not increment when value unchanged", "[indicators]") {
    Indicators indicators;

    auto id = indicators.register_indicator("led", std::make_unique<PassthroughFilter>());

    indicators.start();

    // Set initial value
    indicators.set(id, 255);
    std::this_thread::sleep_for(50ms);

    auto seq1 = indicators.sequence();

    // Set same value again
    indicators.set(id, 255);
    std::this_thread::sleep_for(50ms);

    auto seq2 = indicators.sequence();

    // Sequence should not have changed (value didn't change)
    REQUIRE(seq2 == seq1);

    indicators.stop();
}

// =============================================================================
// Integration with Filters
// =============================================================================

TEST_CASE("Indicators with DebounceFilter delays value change", "[indicators]") {
    // Use ManualClock for deterministic timing - no reliance on wall-clock
    ManualClock::reset();

    TestIndicators indicators;

    // 100ms debounce
    indicators.register_indicator(
        "led",
        std::make_unique<DebounceFilter>(100ms)
    );

    // Set value at time 0
    indicators.set("led", 255);

    // Process at time 50ms - should still be 0 (debounce not complete)
    ManualClock::advance(50ms);
    indicators.process_pending();
    REQUIRE(indicators.get("led") == 0);

    // Process at time 99ms - should still be 0
    ManualClock::advance(49ms);
    indicators.process_pending();
    REQUIRE(indicators.get("led") == 0);

    // Process at time 101ms - debounce complete, should be 255
    ManualClock::advance(2ms);
    indicators.process_pending();
    REQUIRE(indicators.get("led") == 255);
}

TEST_CASE("Indicators with DutyCycleFilter computes average", "[indicators]") {
    // Use ManualClock for deterministic timing - no reliance on wall-clock
    ManualClock::reset();

    TestIndicators indicators;

    auto id = indicators.register_indicator(
        "led",
        std::make_unique<DutyCycleFilter>(100ms)
    );

    // Simulate 50% PWM: alternate on/off every 10ms over 100ms window
    for (int i = 0; i < 5; ++i) {
        // On for 10ms
        indicators.set(id, 255);
        ManualClock::advance(10ms);
        indicators.process_pending();

        // Off for 10ms
        indicators.set(id, 0);
        ManualClock::advance(10ms);
        indicators.process_pending();
    }

    // After 100ms of 50% duty cycle, value should be close to 127
    auto value = indicators.get("led");
    // With deterministic timing, we can use a tighter tolerance
    REQUIRE(value >= 100);
    REQUIRE(value <= 155);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("Indicators destructor stops consumer thread", "[indicators]") {
    {
        Indicators indicators;
        indicators.register_indicator("led", std::make_unique<PassthroughFilter>());
        indicators.start();
        REQUIRE(indicators.is_running());
        // Destructor should stop the thread
    }
    // If we get here without hanging, the destructor worked
    REQUIRE(true);
}

TEST_CASE("Indicators set with invalid ID is handled gracefully", "[indicators]") {
    Indicators indicators;

    indicators.register_indicator("led", std::make_unique<PassthroughFilter>());
    indicators.start();

    // Set with invalid ID - should not crash, event goes to queue but is ignored
    indicators.set(999, 255);

    std::this_thread::sleep_for(50ms);

    // Valid indicator should still be at initial value
    REQUIRE(indicators.get("led") == 0);

    indicators.stop();
}

TEST_CASE("Indicators handles rapid updates without blocking", "[indicators]") {
    Indicators indicators;

    auto id = indicators.register_indicator("led", std::make_unique<PassthroughFilter>());
    indicators.start();

    // Rapid-fire updates - should not block
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) {
        indicators.set(id, static_cast<uint8_t>(i & 0xFF));
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should complete quickly (non-blocking)
    REQUIRE(elapsed < 100ms);

    indicators.stop();
}
