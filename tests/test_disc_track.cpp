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

#include <beebium/disc/DiscTrack.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

// =============================================================================
// Construction
// =============================================================================

TEST_CASE("DiscTrack default construction", "[disc][track]") {
    DiscTrack track;

    CHECK(track.length() == ibm_disc_format::k_ibm_disc_bytes_per_track);
    CHECK_FALSE(track.is_dirty());
}

TEST_CASE("DiscTrack initial pulses are zero", "[disc][track]") {
    DiscTrack track;

    for (uint32_t i = 0; i < track.length(); ++i) {
        CHECK(track.read_pulses(i) == 0);
    }
}

// =============================================================================
// Read/Write
// =============================================================================

TEST_CASE("DiscTrack write and read back", "[disc][track]") {
    DiscTrack track;

    track.write_pulses(0, 0xDEADBEEF);
    CHECK(track.read_pulses(0) == 0xDEADBEEF);

    track.write_pulses(100, 0x12345678);
    CHECK(track.read_pulses(100) == 0x12345678);

    // Other positions remain zero
    CHECK(track.read_pulses(1) == 0);
    CHECK(track.read_pulses(99) == 0);
}

TEST_CASE("DiscTrack write at last valid position", "[disc][track]") {
    DiscTrack track;

    uint32_t last = track.length() - 1;
    track.write_pulses(last, 0xCAFEBABE);
    CHECK(track.read_pulses(last) == 0xCAFEBABE);
}

// =============================================================================
// Dirty Tracking
// =============================================================================

TEST_CASE("DiscTrack is not dirty after construction", "[disc][track][dirty]") {
    DiscTrack track;
    CHECK_FALSE(track.is_dirty());
}

TEST_CASE("DiscTrack becomes dirty after write", "[disc][track][dirty]") {
    DiscTrack track;

    track.write_pulses(0, 0x01);
    CHECK(track.is_dirty());
}

TEST_CASE("DiscTrack dirty flag can be cleared", "[disc][track][dirty]") {
    DiscTrack track;

    track.write_pulses(0, 0x01);
    REQUIRE(track.is_dirty());

    track.set_dirty(false);
    CHECK_FALSE(track.is_dirty());
}

TEST_CASE("DiscTrack dirty flag can be set explicitly", "[disc][track][dirty]") {
    DiscTrack track;

    track.set_dirty(true);
    CHECK(track.is_dirty());
}

// =============================================================================
// Length Management
// =============================================================================

TEST_CASE("DiscTrack custom length", "[disc][track]") {
    DiscTrack track;

    track.set_length(2000);
    CHECK(track.length() == 2000);
}

TEST_CASE("DiscTrack length clamped to maximum", "[disc][track]") {
    DiscTrack track;

    track.set_length(ibm_disc_format::k_max_pulse_positions_per_track + 100);
    CHECK(track.length() == ibm_disc_format::k_max_pulse_positions_per_track);
}

TEST_CASE("DiscTrack length of zero is valid", "[disc][track]") {
    DiscTrack track;

    track.set_length(0);
    CHECK(track.length() == 0);
}

// =============================================================================
// Clear
// =============================================================================

TEST_CASE("DiscTrack clear resets pulses and dirty flag", "[disc][track]") {
    DiscTrack track;

    track.write_pulses(0, 0xFFFFFFFF);
    track.write_pulses(100, 0x12345678);
    REQUIRE(track.is_dirty());

    track.clear();

    CHECK_FALSE(track.is_dirty());
    CHECK(track.read_pulses(0) == 0);
    CHECK(track.read_pulses(100) == 0);
    // Length preserved after clear
    CHECK(track.length() == ibm_disc_format::k_ibm_disc_bytes_per_track);
}

// =============================================================================
// Span Access
// =============================================================================

TEST_CASE("DiscTrack pulse span provides direct access", "[disc][track]") {
    DiscTrack track;

    // Write via span
    auto span = track.pulses();
    span[50] = 0xABCD1234;

    // Read back via normal accessor
    CHECK(track.read_pulses(50) == 0xABCD1234);
}

TEST_CASE("DiscTrack const pulse span for reading", "[disc][track]") {
    DiscTrack track;

    track.write_pulses(42, 0x99887766);

    const auto& const_track = track;
    auto span = const_track.pulses();
    CHECK(span[42] == 0x99887766);
}
