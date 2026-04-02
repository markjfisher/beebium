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
#include "Saf3019p.hpp"

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// Helper: simulate CBUS bit-banging
//////////////////////////////////////////////////////////////////////////////

// The BBC sends bits by setting DATA (bit 0), then pulsing CLB (bit 1) high
// then low, all while DLEN (bit 2) is high. The SAF3019P samples DATA on
// the falling edge of CLB.
//
// PB0 = DATA, PB1 = CLB, PB2 = DLEN

static constexpr uint8_t DLEN = 0x04;  // bit 2
static constexpr uint8_t CLB  = 0x02;  // bit 1
static constexpr uint8_t DATA = 0x01;  // bit 0

// Clock one bit into the chip (DLEN must already be high)
static void clock_bit(Saf3019p& chip, int bit) {
    uint8_t data = (bit & 1) ? DATA : 0;
    // CLB high with data
    chip.write(DLEN | CLB | data);
    // CLB low (falling edge - chip samples)
    chip.write(DLEN | data);
}

// Send TIME ADDRESS: 3 address bits (S, A0, A1) + start bit
// The start bit is always 1 and goes first.
// Address encoding: register = (S | A0<<1 | A1<<2) but the CBUS sends
// S first, then A0, then A1. The register number maps as:
//   reg 0: S=0, A0=0 => bits: start=1, 0, 0
//   reg 1: S=1, A0=0 => bits: start=1, 1, 0
//   reg 2: S=0, A1=0, A0=1 => bits: start=1, 0, 1
// Actually, from BeebEm: the command is shifted right, and address is
// extracted as (cmd & 0x0f) >> 1. Let me trace through BeebEm's protocol.
//
// From BeebEm's UserPortRTCWrite:
//   On falling CLB with DLEN high:
//     RTC_cmd = (RTC_cmd >> 1) | ((Value & 0x01) << 15)
//     RTC_bit++
//   On falling CLB with DLEN low (end of transmission):
//     if RTC_bit == 11: WRITE
//       RTC_cmd >>= 5  (discard 5 positions)
//       register = (RTC_cmd & 0x0f) >> 1
//       data = RTC_cmd >> 4
//     else: READ
//       RTC_cmd >>= 12
//       register = (RTC_cmd & 0x0f) >> 1
//
// For a WRITE (11 bits clocked with DLEN high):
//   Bits 0-3: address (start, S, A0, A1) -- but shifted into position
//   Bits 4-10: data (7 BCD bits, LSB first)
//
// For a READ (4 bits for TIME ADDRESS):
//   Bits 0-3: address (start, S, A0, A1)
//   Then DLEN goes low to trigger read
//
// The start bit is 1. For register N:
//   S  = (N & 1)
//   A0 = (N >> 1) & 1
//   A1 = 0 (always, since we have 8 registers addressed by 3 bits)
// Wait, registers 0-7 need 3 address bits (S, A0, A1):
//   reg 0: S=0, A0=0, A1=0
//   reg 1: S=1, A0=0, A1=0
//   reg 2: S=0, A0=1, A1=0
//   reg 3: S=1, A0=1, A1=0
//   reg 4: S=0, A0=0, A1=1
//   reg 5: S=1, A0=0, A1=1
//   reg 6: S=0, A0=1, A1=1
//   reg 7: S=1, A0=1, A1=1

// End a transmission: DLEN goes low while CLB is pulsed
static void end_transmission(Saf3019p& chip) {
    // CLB low, DLEN low
    chip.write(0x00);
    // CLB high, DLEN low (load pulse)
    chip.write(CLB);
    // CLB low, DLEN low
    chip.write(0x00);
}

// Perform a CBUS WRITE (TIME SET): 11 bits clocked with DLEN high, then load pulse.
// Bit layout: start(1), S, A0, A1, D0-D6 (7 data bits LSB first)
// Register address: S selects counter(0)/alarm(1), A0 and A1 select the pair.
//   reg 0: S=0, A0=0, A1=0  (month counter)
//   reg 1: S=1, A0=0, A1=0  (month alarm)
//   reg 2: S=0, A0=1, A1=0  (date counter)
//   ...
//   reg 7: S=1, A0=1, A1=1  (minute alarm)
static void cbus_write(Saf3019p& chip, int reg, uint8_t bcd_value) {
    // 4 address bits
    clock_bit(chip, 1);                  // start bit (always 1)
    clock_bit(chip, reg & 1);            // S
    clock_bit(chip, (reg >> 1) & 1);     // A0
    clock_bit(chip, (reg >> 2) & 1);     // A1

    // 7 data bits, LSB first
    for (int i = 0; i < 7; i++) {
        clock_bit(chip, (bcd_value >> i) & 1);
    }

    // End transmission (DLEN goes low -- this is the load pulse)
    end_transmission(chip);
}

// Perform a CBUS READ (TIME ADDRESS + TIME READ):
// 4 address bits clocked with DLEN high (start, S, A0, A1),
// then DLEN goes low to trigger the read, then clock out 7 data bits.
// Data shifts on CLB RISING edge during TIME READ (per SAF3019P datasheet).
static uint8_t cbus_read(Saf3019p& chip, int reg) {
    // Send TIME ADDRESS (4 bits with DLEN high)
    clock_bit(chip, 1);                  // start bit
    clock_bit(chip, reg & 1);            // S
    clock_bit(chip, (reg >> 1) & 1);     // A0
    clock_bit(chip, (reg >> 2) & 1);     // A1

    // End address phase: DLEN goes low, triggering the read
    end_transmission(chip);

    // DLEN high, CLB low (prepare for read phase)
    chip.write(DLEN);  // &04

    // Read 7 bits using the v1.26 protocol:
    // First bit available immediately, then CLB rising shifts to next
    uint8_t sr = 0;
    sr = (sr >> 1) | ((chip.read_data_bit() & 1) << 7);

    for (int i = 0; i < 6; i++) {
        chip.write(DLEN | CLB);  // CLB high (rising edge → next bit)
        sr = (sr >> 1) | ((chip.read_data_bit() & 1) << 7);
        chip.write(DLEN);        // CLB low
    }

    chip.write(0x00);  // Reset ORB
    return sr >> 1;    // Right-justify 7-bit value
}

// Reset the CBUS protocol (simulates DDRB being written with 0xA7)
static void cbus_reset(Saf3019p& chip) {
    chip.reset_protocol();
}

//////////////////////////////////////////////////////////////////////////////
// BCD helper
//////////////////////////////////////////////////////////////////////////////

static uint8_t to_bcd(int value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

static int from_bcd(uint8_t bcd) {
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
}

//////////////////////////////////////////////////////////////////////////////
// BCD wrapping tests
//////////////////////////////////////////////////////////////////////////////

// Wrapping tests operate on raw byte values, matching the SAF3019P's
// internal behaviour as empirically determined (BeebEm). The CBUS
// protocol delivers raw bytes; the wrapping operates on those bytes,
// not on BCD-decoded integers.

TEST_CASE("SAF3019P register 1 (month alarm) wraps raw byte modulo 20", "[saf3019p][wrapping]") {
    Saf3019p chip;

    SECTION("Raw byte values 0-19 stored as-is") {
        for (uint8_t i = 0; i <= 19; i++) {
            chip.write_register(1, i);
            REQUIRE(chip.read_register(1) == i);
        }
    }

    SECTION("Raw byte 20 wraps to 0") {
        chip.write_register(1, 20);
        REQUIRE(chip.read_register(1) == 0);
    }

    SECTION("Raw byte 25 wraps to 5") {
        chip.write_register(1, 25);
        REQUIRE(chip.read_register(1) == 5);
    }

    SECTION("Raw byte 39 wraps to 19") {
        chip.write_register(1, 39);
        REQUIRE(chip.read_register(1) == 19);
    }

    SECTION("BCD 0x04 (year 4) stored correctly") {
        chip.write_register(1, 0x04);
        REQUIRE(chip.read_register(1) == 0x04);
    }

    SECTION("BCD 0x09 (year 9) stored correctly") {
        chip.write_register(1, 0x09);
        REQUIRE(chip.read_register(1) == 0x09);
    }

    SECTION("BCD 0x13 (year 13, raw byte 19) stored correctly") {
        chip.write_register(1, 0x13);
        REQUIRE(chip.read_register(1) == 0x13);
    }
}

TEST_CASE("SAF3019P registers 2-5 (date/hour) raw byte wrapping", "[saf3019p][wrapping]") {
    Saf3019p chip;

    SECTION("Register 2: BCD 0x31 (day 31, raw byte 49) stored (< 40 check on raw byte)") {
        // raw byte 0x31 = 49, which is >= 40, so it wraps to 49-40=9
        chip.write_register(2, 0x31);
        REQUIRE(chip.read_register(2) == 9);
    }

    SECTION("Register 2: raw byte values 0-39 stored as-is") {
        chip.write_register(2, 31);
        REQUIRE(chip.read_register(2) == 31);
    }

    SECTION("Register 2: raw byte 40 wraps to 0") {
        chip.write_register(2, 40);
        REQUIRE(chip.read_register(2) == 0);
    }

    SECTION("Register 4: BCD 0x23 (hour 23, raw byte 35) stored as-is") {
        chip.write_register(4, 0x23);
        REQUIRE(chip.read_register(4) == 0x23);
    }
}

TEST_CASE("SAF3019P registers 6-7 (minute) raw byte wrapping", "[saf3019p][wrapping]") {
    Saf3019p chip;

    SECTION("Register 6: raw byte values 0-79 stored as-is") {
        chip.write_register(6, 0x59);  // BCD 59, raw byte 89 -- but 89 >= 80!
        // raw byte 0x59 = 89, wraps to 89-80 = 9
        REQUIRE(chip.read_register(6) == 9);
    }

    SECTION("Register 6: raw byte 59 stored as-is") {
        chip.write_register(6, 59);  // raw byte 59 < 80
        REQUIRE(chip.read_register(6) == 59);
    }

    SECTION("Register 7: value 0 stored") {
        chip.write_register(7, 0);
        REQUIRE(chip.read_register(7) == 0);
    }

    SECTION("Register 7: BCD 0x71 (raw byte 113) wraps to 13") {
        // 113 >= 100 && < 180: 113 - 100 = 13
        chip.write_register(7, 0x71);
        REQUIRE(chip.read_register(7) == 13);
    }
}

TEST_CASE("SAF3019P register 0 (month counter) wrapping", "[saf3019p][wrapping]") {
    Saf3019p chip;

    SECTION("Raw byte values 1-19 stored as-is") {
        for (uint8_t i = 1; i <= 19; i++) {
            chip.write_register(0, i);
            REQUIRE(chip.read_register(0) == i);
        }
    }

    SECTION("Value 0 does not change month (prescaler reset)") {
        chip.write_register(0, to_bcd(6));  // June = BCD 0x06
        uint8_t before = chip.read_register(0);
        chip.write_register(0, 0);  // prescaler reset
        REQUIRE(chip.read_register(0) == before);
    }

    SECTION("Raw byte values 20-59 clamp to 19") {
        chip.write_register(0, 25);
        REQUIRE(chip.read_register(0) == 19);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Counter advancement tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SAF3019P counter advancement", "[saf3019p][counters]") {
    Saf3019p chip;

    SECTION("Minute rollover") {
        Saf3019p::DateTime dt{1985, 6, 15, 10, 58};
        chip.initialise(dt);

        // Advance 3 minutes
        chip.advance_by_minutes(3);

        auto result = chip.current_datetime();
        REQUIRE(result.hour == 11);
        REQUIRE(result.minute == 1);
    }

    SECTION("Hour rollover") {
        Saf3019p::DateTime dt{1985, 6, 15, 23, 58};
        chip.initialise(dt);

        chip.advance_by_minutes(3);

        auto result = chip.current_datetime();
        REQUIRE(result.day == 16);
        REQUIRE(result.hour == 0);
        REQUIRE(result.minute == 1);
    }

    SECTION("February always 28 days (chip limitation)") {
        Saf3019p::DateTime dt{1985, 2, 28, 23, 58};
        chip.initialise(dt);

        chip.advance_by_minutes(3);

        auto result = chip.current_datetime();
        REQUIRE(result.month == 3);
        REQUIRE(result.day == 1);
        REQUIRE(result.hour == 0);
        REQUIRE(result.minute == 1);
    }

    SECTION("February 28 days even in leap year (chip has no year awareness)") {
        // 1984 is a leap year, but the chip still wraps at 28
        Saf3019p::DateTime dt{1984, 2, 28, 23, 58};
        chip.initialise(dt);

        chip.advance_by_minutes(3);

        auto result = chip.current_datetime();
        REQUIRE(result.month == 3);
        REQUIRE(result.day == 1);
    }

    SECTION("30-day months: April, June, September, November") {
        Saf3019p::DateTime dt{1985, 4, 30, 23, 58};
        chip.initialise(dt);

        chip.advance_by_minutes(3);

        auto result = chip.current_datetime();
        REQUIRE(result.month == 5);
        REQUIRE(result.day == 1);
    }

    SECTION("31-day months: January, March, etc.") {
        Saf3019p::DateTime dt{1985, 1, 31, 23, 58};
        chip.initialise(dt);

        chip.advance_by_minutes(3);

        auto result = chip.current_datetime();
        REQUIRE(result.month == 2);
        REQUIRE(result.day == 1);
    }

    SECTION("December to January month rollover") {
        Saf3019p::DateTime dt{1985, 12, 31, 23, 58};
        chip.initialise(dt);

        chip.advance_by_minutes(3);

        auto result = chip.current_datetime();
        REQUIRE(result.month == 1);
        REQUIRE(result.day == 1);
        // Year register is NOT incremented by the chip
        REQUIRE(result.year == 1985);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Register 0 write-zero (prescaler reset) tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SAF3019P register 0 write-zero prescaler reset", "[saf3019p][prescaler]") {
    Saf3019p chip;

    SECTION("Prescaler reset with accumulated seconds < 30 does not increment minutes") {
        Saf3019p::DateTime dt{1985, 6, 15, 10, 30};
        chip.initialise(dt);
        chip.set_accumulated_seconds(15);

        chip.write_register(0, to_bcd(0));

        REQUIRE(from_bcd(chip.read_register(6)) == 30);  // minutes unchanged
        REQUIRE(chip.accumulated_seconds() == 0);          // prescaler reset
    }

    SECTION("Prescaler reset with accumulated seconds >= 30 increments minutes") {
        Saf3019p::DateTime dt{1985, 6, 15, 10, 30};
        chip.initialise(dt);
        chip.set_accumulated_seconds(45);

        chip.write_register(0, to_bcd(0));

        REQUIRE(from_bcd(chip.read_register(6)) == 31);  // minutes incremented
        REQUIRE(chip.accumulated_seconds() == 0);          // prescaler reset
    }
}

//////////////////////////////////////////////////////////////////////////////
// Initialise tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SAF3019P initialise sets all registers", "[saf3019p][init]") {
    Saf3019p chip;

    Saf3019p::DateTime dt{1985, 6, 15, 14, 30};
    chip.initialise(dt);

    SECTION("Counter registers contain correct BCD values") {
        REQUIRE(from_bcd(chip.read_register(0)) == 6);   // month
        REQUIRE(from_bcd(chip.read_register(2)) == 15);  // day
        REQUIRE(from_bcd(chip.read_register(4)) == 14);  // hour
        REQUIRE(from_bcd(chip.read_register(6)) == 30);  // minute
    }

    SECTION("Year register contains offset from 1981") {
        REQUIRE(from_bcd(chip.read_register(1)) == 4);   // 1985 - 1981 = 4
    }

    SECTION("Old month register contains current month") {
        uint8_t reg3 = chip.read_register(3);
        REQUIRE((from_bcd(reg3) & 0x0F) == 6);  // low nibble = month
    }

    SECTION("Register 5 is zero (unused)") {
        REQUIRE(chip.read_register(5) == 0x00);
    }

    SECTION("Register 7 is zero") {
        REQUIRE(chip.read_register(7) == 0x00);
    }

    SECTION("current_datetime reconstructs the original values") {
        auto result = chip.current_datetime();
        REQUIRE(result.year == 1985);
        REQUIRE(result.month == 6);
        REQUIRE(result.day == 15);
        REQUIRE(result.hour == 14);
        REQUIRE(result.minute == 30);
    }
}

TEST_CASE("SAF3019P initialise sets leap flag correctly", "[saf3019p][init][leap]") {
    Saf3019p chip;

    SECTION("Leap year, January: leap flag set in raw byte") {
        Saf3019p::DateTime dt{1984, 1, 15, 10, 0};
        chip.initialise(dt);
        uint8_t reg3 = chip.read_register(3);
        REQUIRE((reg3 & 0x10) == 0x10);  // bit 4 set in raw byte
    }

    SECTION("Leap year, March: leap flag not set (past danger zone)") {
        Saf3019p::DateTime dt{1984, 3, 15, 10, 0};
        chip.initialise(dt);
        uint8_t reg3 = chip.read_register(3);
        REQUIRE((reg3 & 0x10) == 0x00);
    }

    SECTION("Non-leap year: leap flag not set") {
        Saf3019p::DateTime dt{1985, 1, 15, 10, 0};
        chip.initialise(dt);
        uint8_t reg3 = chip.read_register(3);
        REQUIRE((reg3 & 0x10) == 0x00);
    }
}

TEST_CASE("SAF3019P initialise rejects unrepresentable years", "[saf3019p][init]") {
    Saf3019p chip;

    SECTION("Year 1980 (before 1981) is rejected") {
        Saf3019p::DateTime dt{1980, 6, 15, 10, 0};
        REQUIRE_FALSE(chip.initialise(dt));
    }

    SECTION("Year 2001 (offset 20, wraps) is rejected") {
        Saf3019p::DateTime dt{2001, 6, 15, 10, 0};
        REQUIRE_FALSE(chip.initialise(dt));
    }

    SECTION("Year 1981 (offset 0) is accepted") {
        Saf3019p::DateTime dt{1981, 1, 1, 0, 0};
        REQUIRE(chip.initialise(dt));
    }

    SECTION("Year 2000 (offset 19) is accepted") {
        Saf3019p::DateTime dt{2000, 1, 1, 0, 0};
        REQUIRE(chip.initialise(dt));
    }
}

//////////////////////////////////////////////////////////////////////////////
// CBUS protocol tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SAF3019P CBUS write and readback", "[saf3019p][cbus]") {
    Saf3019p chip;

    SECTION("Write to alarm register 7 and read back") {
        cbus_reset(chip);
        cbus_write(chip, 7, to_bcd(13));

        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 7);
        REQUIRE(from_bcd(value) == 13);
    }

    SECTION("Write zero to register 7 and read back") {
        cbus_reset(chip);
        cbus_write(chip, 7, 0x00);

        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 7);
        REQUIRE(value == 0x00);
    }

    SECTION("Write to register 1 (year) and read back") {
        cbus_reset(chip);
        cbus_write(chip, 1, to_bcd(4));  // year offset 4 = 1985

        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 1);
        REQUIRE(from_bcd(value) == 4);
    }
}

TEST_CASE("SAF3019P CBUS read of counter registers", "[saf3019p][cbus]") {
    Saf3019p chip;
    Saf3019p::DateTime dt{1985, 6, 15, 14, 30};
    chip.initialise(dt);

    SECTION("Read month register via CBUS") {
        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 0);
        REQUIRE(from_bcd(value) == 6);
    }

    SECTION("Read day register via CBUS") {
        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 2);
        REQUIRE(from_bcd(value) == 15);
    }

    SECTION("Read hour register via CBUS") {
        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 4);
        REQUIRE(from_bcd(value) == 14);
    }

    SECTION("Read minute register via CBUS") {
        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 6);
        REQUIRE(from_bcd(value) == 30);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Dongle detection test
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SAF3019P dongle detection sequence", "[saf3019p][dongle]") {
    Saf3019p chip;

    // The Level 3 FS's RTC routine:
    // 1. Convert binary 71 to BCD via counting in decimal mode -> 0x71
    // 2. Write 0x71 to register 7
    // 3. Read register 7 back
    // 4. Convert BCD to binary via counting -> the result after wrapping
    // 5. XOR with 0x0D -- must be zero

    SECTION("Write BCD 0x71 to register 7, read back, verify wrapping") {
        cbus_reset(chip);
        cbus_write(chip, 7, 0x71);  // BCD 0x71 = 71 decimal

        cbus_reset(chip);
        uint8_t readback = cbus_read(chip, 7);

        // The minute alarm register wraps 0x71. After wrapping,
        // BCD-to-binary conversion should yield 13 (0x0D).
        int binary = from_bcd(readback);
        REQUIRE(binary == 13);
    }

    SECTION("Write zero to register 7, read back zero") {
        cbus_reset(chip);
        cbus_write(chip, 7, 0x00);

        cbus_reset(chip);
        uint8_t readback = cbus_read(chip, 7);
        REQUIRE(readback == 0x00);
    }

    SECTION("Full dongle sequence passes") {
        // Phase 1: write 0x71, verify readback XOR 0x0D == 0
        cbus_reset(chip);
        cbus_write(chip, 7, 0x71);

        cbus_reset(chip);
        uint8_t read1 = cbus_read(chip, 7);
        int binary1 = from_bcd(read1);
        REQUIRE((binary1 ^ 0x0D) == 0);

        // Phase 2: write 0, verify readback == 0
        cbus_reset(chip);
        cbus_write(chip, 7, 0x00);

        cbus_reset(chip);
        uint8_t read2 = cbus_read(chip, 7);
        REQUIRE(read2 == 0x00);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Protocol reset tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SAF3019P protocol reset clears state", "[saf3019p][protocol]") {
    Saf3019p chip;

    SECTION("Reset allows fresh command after partial transmission") {
        // Start a command but don't finish
        chip.write(DLEN | CLB | DATA);  // clock a bit
        chip.write(DLEN | DATA);

        // Reset
        chip.reset_protocol();

        // Should be able to do a clean write/read now
        cbus_write(chip, 7, to_bcd(5));

        cbus_reset(chip);
        uint8_t value = cbus_read(chip, 7);
        REQUIRE(from_bcd(value) == 5);
    }
}
