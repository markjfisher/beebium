// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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
#include "TimeParser.hpp"

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// parse_absolute_time tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("parse_absolute_time valid inputs", "[time-parser][absolute]") {
    SECTION("Standard format YYYY-MM-DDThh:mm") {
        auto dt = parse_absolute_time("1985-03-15T10:30");
        REQUIRE(dt.year == 1985);
        REQUIRE(dt.month == 3);
        REQUIRE(dt.day == 15);
        REQUIRE(dt.hour == 10);
        REQUIRE(dt.minute == 30);
    }

    SECTION("With seconds (ignored)") {
        auto dt = parse_absolute_time("1985-03-15T10:30:45");
        REQUIRE(dt.year == 1985);
        REQUIRE(dt.month == 3);
        REQUIRE(dt.day == 15);
        REQUIRE(dt.hour == 10);
        REQUIRE(dt.minute == 30);
    }

    SECTION("Midnight") {
        auto dt = parse_absolute_time("2000-01-01T00:00");
        REQUIRE(dt.year == 2000);
        REQUIRE(dt.hour == 0);
        REQUIRE(dt.minute == 0);
    }

    SECTION("End of day") {
        auto dt = parse_absolute_time("1999-12-31T23:59");
        REQUIRE(dt.year == 1999);
        REQUIRE(dt.month == 12);
        REQUIRE(dt.day == 31);
        REQUIRE(dt.hour == 23);
        REQUIRE(dt.minute == 59);
    }

    SECTION("Lowercase t separator") {
        auto dt = parse_absolute_time("1985-03-15t10:30");
        REQUIRE(dt.year == 1985);
    }

    SECTION("Compact format (no colon in time)") {
        auto dt = parse_absolute_time("1985-06-15T1430");
        REQUIRE(dt.year == 1985);
        REQUIRE(dt.month == 6);
        REQUIRE(dt.day == 15);
        REQUIRE(dt.hour == 14);
        REQUIRE(dt.minute == 30);
    }
}

TEST_CASE("parse_absolute_time invalid inputs", "[time-parser][absolute]") {
    SECTION("Too short") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985"), std::invalid_argument);
    }

    SECTION("Bad separator") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985/03/15T10:30"), std::invalid_argument);
    }

    SECTION("Month out of range") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985-13-15T10:30"), std::invalid_argument);
    }

    SECTION("Hour out of range") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985-03-15T25:30"), std::invalid_argument);
    }

    SECTION("Minute out of range") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985-03-15T10:61"), std::invalid_argument);
    }
}

//////////////////////////////////////////////////////////////////////////////
// apply_relative_offset tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("apply_relative_offset", "[time-parser][relative]") {
    // These tests verify the offset is applied relative to current time.
    // We can't test exact values since current time varies, but we can
    // verify the offset arithmetic.

    SECTION("Negative year offset") {
        auto now = current_local_datetime();
        auto dt = apply_relative_offset("-10y");
        REQUIRE(dt.year == now.year - 10);
        REQUIRE(dt.month == now.month);
    }

    SECTION("Positive hour offset") {
        auto now = current_local_datetime();
        auto dt = apply_relative_offset("+5h");
        // May roll over to next day, so just check it's different
        int expected_total_minutes = now.hour * 60 + now.minute + 5 * 60;
        int actual_total_minutes = dt.hour * 60 + dt.minute;
        // Account for day rollover
        if (expected_total_minutes >= 24 * 60) {
            expected_total_minutes -= 24 * 60;
        }
        REQUIRE(actual_total_minutes == expected_total_minutes);
    }

    SECTION("Combined offset") {
        auto now = current_local_datetime();
        auto dt = apply_relative_offset("-1y6M");
        // -1y6M means subtract 1 year and 6 months
        // Month may go negative; normalisation pushes into previous year
        int expected_month = now.month - 6;
        int expected_year = now.year - 1;
        if (expected_month <= 0) {
            expected_month += 12;
            expected_year--;
        }
        REQUIRE(dt.year == expected_year);
        REQUIRE(dt.month == expected_month);
    }
}

TEST_CASE("apply_relative_offset invalid inputs", "[time-parser][relative]") {
    SECTION("Empty string") {
        REQUIRE_THROWS_AS(apply_relative_offset(""), std::invalid_argument);
    }

    SECTION("Missing unit") {
        REQUIRE_THROWS_AS(apply_relative_offset("-10"), std::invalid_argument);
    }

    SECTION("Unknown unit") {
        REQUIRE_THROWS_AS(apply_relative_offset("-10x"), std::invalid_argument);
    }
}

//////////////////////////////////////////////////////////////////////////////
// current_local_datetime tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("current_local_datetime returns plausible values", "[time-parser]") {
    auto dt = current_local_datetime();
    REQUIRE(dt.year >= 2020);
    REQUIRE(dt.month >= 1);
    REQUIRE(dt.month <= 12);
    REQUIRE(dt.day >= 1);
    REQUIRE(dt.day <= 31);
    REQUIRE(dt.hour >= 0);
    REQUIRE(dt.hour <= 23);
    REQUIRE(dt.minute >= 0);
    REQUIRE(dt.minute <= 59);
}
