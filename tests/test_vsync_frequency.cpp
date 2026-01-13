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

// Accurately measure VSYNC frequency by counting direct signal transitions
//
// This test measures VSYNC frequency by monitoring the SystemViaPeripheral's
// vsync() state every cycle to catch all transitions.

#include <catch2/catch_test_macros.hpp>
#include "test_mode7_helpers.hpp"
#include <iostream>

using namespace beebium;
using namespace beebium::test;

#ifdef BEEBIUM_ROM_DIR

TEST_CASE("VSYNC frequency measurement", "[video][vsync]") {
    Mode7TestContext<ModelB> ctx;
    REQUIRE(ctx.booted);

    std::cout << "\n=== Measuring VSYNC Frequency ===\n";

    // Monitor every single cycle to catch all VSYNC transitions
    bool prev_vsync = ctx.machine.memory().system_via_peripheral.vsync();
    int rising_edges = 0;
    int falling_edges = 0;

    constexpr int cycles = 2'000'000;  // 1 second at 2 MHz
    std::cout << "Monitoring VSYNC for " << cycles << " cycles (1 second at 2MHz)...\n";

    for (int i = 0; i < cycles; ++i) {
        ctx.machine.step();

        bool curr_vsync = ctx.machine.memory().system_via_peripheral.vsync();

        if (curr_vsync && !prev_vsync) {
            ++rising_edges;
        } else if (!curr_vsync && prev_vsync) {
            ++falling_edges;
        }

        prev_vsync = curr_vsync;
    }

    std::cout << "\n=== Results ===\n";
    std::cout << "VSYNC rising edges: " << rising_edges << "\n";
    std::cout << "VSYNC falling edges: " << falling_edges << "\n";
    std::cout << "Total transitions: " << (rising_edges + falling_edges) << "\n";
    std::cout << "Measured frequency: " << rising_edges << " Hz\n";
    std::cout << "Expected frequency: 50 Hz (PAL)\n";

    // Check frequency
    if (rising_edges < 45 || rising_edges > 55) {
        std::cout << "\n❌ VSYNC frequency out of range!\n";
        std::cout << "   Expected 50 Hz ± 5 Hz, got " << rising_edges << " Hz\n";
        std::cout << "   Possible causes:\n";
        std::cout << "   - CRTC registers not programmed correctly by MOS\n";
        std::cout << "   - VideoBinding not being ticked at correct rate\n";
        std::cout << "   - CRTC tick() implementation issue\n";
    } else {
        std::cout << "\n✓ VSYNC frequency is correct (" << rising_edges << " Hz)\n";
    }

    // Verify transitions are balanced (rising ~= falling)
    int transition_diff = std::abs(rising_edges - falling_edges);
    if (transition_diff > 1) {
        std::cout << "\n⚠ WARNING: Unbalanced VSYNC transitions!\n";
        std::cout << "   Rising: " << rising_edges << ", Falling: " << falling_edges << "\n";
        std::cout << "   This suggests VSYNC signal may be stuck or unstable\n";
    }

    REQUIRE(rising_edges > 0);  // At least some VSYNCs should occur
}

#endif
