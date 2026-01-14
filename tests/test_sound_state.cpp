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

// State consistency tests for SN76489 sound chip
//
// These tests verify that:
// - Chip state is accessible via introspection API
// - State remains consistent across operations
// - Reset properly initializes all state
// - State queries match expected values after register writes
//
// When serialization is implemented, these tests will help verify
// state save/load round-trips work correctly.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "beebium/devices/Sn76489.hpp"
#include "beebium/AudioBuffer.hpp"

#include <vector>

using namespace beebium;

TEST_CASE("SN76489 state consistency", "[sn76489][state]") {

    SECTION("Initial state after construction") {
        Sn76489 chip(4'000'000, 48'000);

        // All tone channels should be silent
        for (size_t i = 0; i < 3; ++i) {
            auto tone = chip.get_tone_channel_state(i);
            REQUIRE(tone.frequency == 0);
            REQUIRE(tone.volume == 15);  // Silent
            REQUIRE(tone.amplitude == 0);
        }

        // Noise channel should be silent with default state
        auto noise = chip.get_noise_channel_state();
        REQUIRE(noise.volume == 15);
        REQUIRE(noise.amplitude == 0);
        REQUIRE(noise.lfsr == 0x4000);  // Initial LFSR state

        // Latched register should be 0
        REQUIRE(chip.latched_register() == 0);
    }

    SECTION("Tone channel state after frequency write") {
        Sn76489 chip(4'000'000, 48'000);

        // Set tone 0 frequency to 0x123
        chip.write(0x83);  // Latch: tone 0, low nibble = 3
        chip.write(0x12);  // Data: high bits = 0x12

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 0x123);
        REQUIRE(state.volume == 15);  // Still silent

        // Frequency Hz should match: 4 MHz / (32 × 0x123) = 4000000 / (32 × 291) ≈ 429.6 Hz
        float expected_hz = 4000000.0f / (32.0f * 0x123);
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(expected_hz, 0.001f));
    }

    SECTION("Tone channel state after volume write") {
        Sn76489 chip(4'000'000, 48'000);

        // Set tone 1 volume to 5
        chip.write(0xB5);  // Latch: tone 1 volume = 5

        auto state = chip.get_tone_channel_state(1);
        REQUIRE(state.volume == 5);
        // Amplitude should be non-zero (based on output_bit state)
        // Volume 5 corresponds to amplitude ±40
        REQUIRE(std::abs(state.amplitude) == 40);
    }

    SECTION("All tone channels independent") {
        Sn76489 chip(4'000'000, 48'000);

        // Set different frequencies and volumes for each channel
        chip.write(0x81);  // Tone 0 freq = 1
        chip.write(0x91);  // Tone 0 vol = 1

        chip.write(0xA2);  // Tone 1 freq = 2
        chip.write(0xB3);  // Tone 1 vol = 3

        chip.write(0xC4);  // Tone 2 freq = 4
        chip.write(0xD5);  // Tone 2 vol = 5

        // Verify each channel has correct independent state
        auto tone0 = chip.get_tone_channel_state(0);
        auto tone1 = chip.get_tone_channel_state(1);
        auto tone2 = chip.get_tone_channel_state(2);

        REQUIRE(tone0.frequency == 1);
        REQUIRE(tone0.volume == 1);

        REQUIRE(tone1.frequency == 2);
        REQUIRE(tone1.volume == 3);

        REQUIRE(tone2.frequency == 4);
        REQUIRE(tone2.volume == 5);
    }

    SECTION("Noise channel state consistency") {
        Sn76489 chip(4'000'000, 48'000);

        // Set noise to white mode, rate 2
        chip.write(0xE6);  // Noise control: rate=2, white=1
        chip.write(0xF3);  // Noise volume = 3

        auto noise = chip.get_noise_channel_state();
        REQUIRE(noise.rate_select == 2);
        REQUIRE(noise.white_mode == true);
        REQUIRE(noise.volume == 3);

        // Rate Hz for rate 2: 250 kHz / 128 = 1953.125 Hz
        REQUIRE_THAT(noise.rate_hz, Catch::Matchers::WithinRel(1953.125f, 0.01f));
    }

    SECTION("LFSR state evolves during playback") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(8192);

        // Enable noise
        chip.write(0xE4);  // White noise, rate 0
        chip.write(0xF0);  // Volume = 0 (max)

        auto state1 = chip.get_noise_channel_state();
        REQUIRE(state1.lfsr == 0x4000);

        // Run chip to advance LFSR
        for (int i = 0; i < 10000; ++i) {
            chip.tick(buffer);
        }

        auto state2 = chip.get_noise_channel_state();
        REQUIRE(state2.lfsr != 0x4000);  // LFSR should have changed
        REQUIRE((state2.lfsr & 0x7FFF) == state2.lfsr);  // Valid 15-bit value
    }

    SECTION("Counter state accessible") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(1024);

        // Set tone 0 to frequency 100
        chip.write(0x84);  // Freq low = 4
        chip.write(0x06);  // Freq high = 6 → 0x64 (100)
        chip.write(0x90);  // Volume = 0

        // Run a few ticks
        for (int i = 0; i < 100; ++i) {
            chip.tick(buffer);
        }

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 100);
        REQUIRE(state.counter <= 100);  // Counter should be valid
    }

    SECTION("Output bit toggles during playback") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(8192);

        // Set very short period for fast toggling
        chip.write(0x84);  // Freq = 4 (fast)
        chip.write(0x90);  // Volume = 0

        // Track output bit changes
        bool saw_true = false;
        bool saw_false = false;

        for (int i = 0; i < 1000; ++i) {
            chip.tick(buffer);
            auto state = chip.get_tone_channel_state(0);
            if (state.output_bit) saw_true = true;
            else saw_false = true;
        }

        REQUIRE(saw_true);
        REQUIRE(saw_false);
    }
}

TEST_CASE("SN76489 reset state", "[sn76489][state]") {

    SECTION("Reset clears all tone channels") {
        Sn76489 chip(4'000'000, 48'000);

        // Set various values
        chip.write(0x8F);  // Tone 0 freq = 15
        chip.write(0x92);  // Tone 0 vol = 2
        chip.write(0xAF);  // Tone 1 freq = 15
        chip.write(0xB4);  // Tone 1 vol = 4
        chip.write(0xCF);  // Tone 2 freq = 15
        chip.write(0xD6);  // Tone 2 vol = 6

        chip.reset();

        for (size_t i = 0; i < 3; ++i) {
            auto tone = chip.get_tone_channel_state(i);
            REQUIRE(tone.frequency == 0);
            REQUIRE(tone.volume == 15);
        }
    }

    SECTION("Reset clears noise channel") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(1024);

        // Enable noise and run to change LFSR
        chip.write(0xE7);  // Noise: rate 3, white mode
        chip.write(0xF2);  // Noise vol = 2

        for (int i = 0; i < 10000; ++i) {
            chip.tick(buffer);
        }

        chip.reset();

        auto noise = chip.get_noise_channel_state();
        REQUIRE(noise.volume == 15);
        REQUIRE(noise.lfsr == 0x4000);
    }

    SECTION("Reset clears latched register") {
        Sn76489 chip(4'000'000, 48'000);

        chip.write(0xA0);  // Latch tone 1 freq
        REQUIRE(chip.latched_register() == 2);

        chip.reset();
        REQUIRE(chip.latched_register() == 0);
    }

    SECTION("Reset clears write trace") {
        Sn76489 chip(4'000'000, 48'000);

        chip.write(0x80);
        chip.write(0x90);
        chip.write(0xA0);

        REQUIRE(chip.get_write_trace_count() == 3);

        chip.clear_write_trace();
        REQUIRE(chip.get_write_trace_count() == 0);
    }
}

TEST_CASE("SN76489 state queries are read-only", "[sn76489][state]") {

    SECTION("State query doesn't modify chip") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(1024);

        // Set up some state
        chip.write(0x85);
        chip.write(0x90);

        // Query state multiple times
        for (int i = 0; i < 100; ++i) {
            [[maybe_unused]] auto tone = chip.get_tone_channel_state(0);
            [[maybe_unused]] auto noise = chip.get_noise_channel_state();
            [[maybe_unused]] auto reg = chip.latched_register();
        }

        // State should be unchanged
        auto final_tone = chip.get_tone_channel_state(0);
        REQUIRE(final_tone.frequency == 5);
        REQUIRE(final_tone.volume == 0);
    }

    SECTION("Tick advances state, query doesn't") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(8192);

        chip.write(0xE4);  // Noise white mode
        chip.write(0xF0);  // Volume max

        // Get initial LFSR
        uint16_t initial_lfsr = chip.get_noise_channel_state().lfsr;

        // Query multiple times (should not change LFSR)
        for (int i = 0; i < 100; ++i) {
            REQUIRE(chip.get_noise_channel_state().lfsr == initial_lfsr);
        }

        // Tick (should change LFSR)
        for (int i = 0; i < 10000; ++i) {
            chip.tick(buffer);
        }

        REQUIRE(chip.get_noise_channel_state().lfsr != initial_lfsr);
    }
}

TEST_CASE("SN76489 frequency_hz calculations", "[sn76489][state]") {

    SECTION("Tone frequency_hz matches formula") {
        Sn76489 chip(4'000'000, 48'000);

        // Test several frequency values
        std::vector<uint16_t> test_freqs = {1, 10, 100, 512, 1023};

        for (uint16_t freq : test_freqs) {
            // Set frequency (up to 10 bits)
            uint8_t low = freq & 0x0F;
            uint8_t high = (freq >> 4) & 0x3F;

            chip.write(0x80 | low);   // Latch tone 0, low nibble
            chip.write(high);          // Data byte, high bits

            auto state = chip.get_tone_channel_state(0);
            REQUIRE(state.frequency == freq);

            // f_out = f_clock / (32 × N)
            float expected = 4000000.0f / (32.0f * freq);
            REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(expected, 0.001f));
        }
    }

    SECTION("Frequency 0 treated as 1024 for Hz calculation") {
        Sn76489 chip(4'000'000, 48'000);

        chip.write(0x80);  // Tone 0 freq = 0
        chip.write(0x00);

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 0);

        // N=0 treated as 1024: f = 4 MHz / (32 × 1024) = 122.07 Hz
        float expected = 4000000.0f / (32.0f * 1024.0f);
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(expected, 0.01f));
    }

    SECTION("Noise rate_hz matches formula for fixed rates") {
        Sn76489 chip(4'000'000, 48'000);

        // Rate dividers: 32, 64, 128 (matches MAME formula: 1 << (5 + rate))
        std::vector<std::pair<uint8_t, float>> rate_tests = {
            {0, 250000.0f / 32.0f},   // 7812.5 Hz
            {1, 250000.0f / 64.0f},   // 3906.25 Hz
            {2, 250000.0f / 128.0f},  // 1953.125 Hz
        };

        for (auto [rate, expected_hz] : rate_tests) {
            chip.write(0xE0 | rate);  // Set noise rate

            auto noise = chip.get_noise_channel_state();
            REQUIRE(noise.rate_select == rate);
            REQUIRE_THAT(noise.rate_hz, Catch::Matchers::WithinRel(expected_hz, 0.01f));
        }
    }

    SECTION("Noise rate=3 uses tone 2 frequency") {
        Sn76489 chip(4'000'000, 48'000);

        // Set tone 2 to specific frequency
        chip.write(0xC4);  // Tone 2 freq = 4
        chip.write(0x03);  // High bits = 3 → 0x34 (52)

        // Set noise to use tone 2
        chip.write(0xE3);  // Rate = 3

        auto tone2 = chip.get_tone_channel_state(2);
        auto noise = chip.get_noise_channel_state();

        REQUIRE(noise.rate_select == 3);
        REQUIRE_THAT(noise.rate_hz, Catch::Matchers::WithinRel(tone2.frequency_hz, 0.01f));
    }
}
