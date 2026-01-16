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

#include <catch2/catch_test_macros.hpp>
#include <beebium/Via6522.hpp>

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// Initialization and reset tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 initialization", "[via][init]") {
    Via6522 via;

    SECTION("Registers default to expected values") {
        REQUIRE(via.read(Via6522::REG_DDRA) == 0x00);  // All inputs
        REQUIRE(via.read(Via6522::REG_DDRB) == 0x00);
        REQUIRE(via.read(Via6522::REG_ACR) == 0x00);
        REQUIRE(via.read(Via6522::REG_PCR) == 0x00);
    }

    SECTION("IER bit 7 always reads as 1") {
        REQUIRE((via.read(Via6522::REG_IER) & 0x80) == 0x80);
    }

    SECTION("IFR is clear initially") {
        REQUIRE((via.read(Via6522::REG_IFR) & 0x7F) == 0x00);
    }

    SECTION("Port pins read high by default (inputs pulled up)") {
        REQUIRE(via.read(Via6522::REG_ORA_NH) == 0xFF);
    }

    SECTION("No IRQ pending initially") {
        REQUIRE_FALSE(via.irq_pending());
    }
}

TEST_CASE("Via6522 reset", "[via][reset]") {
    Via6522 via;

    // Modify some state
    via.write(Via6522::REG_DDRA, 0xFF);
    via.write(Via6522::REG_ORA, 0x42);
    via.write(Via6522::REG_IER, 0xFF);  // Enable all interrupts

    // Reset
    via.reset();

    SECTION("DDR is cleared") {
        REQUIRE(via.read(Via6522::REG_DDRA) == 0x00);
    }

    SECTION("IER is cleared (except bit 7)") {
        REQUIRE(via.read(Via6522::REG_IER) == 0x80);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Port tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 data direction register", "[via][ports]") {
    Via6522 via;

    SECTION("DDR 0 means input, 1 means output") {
        via.write(Via6522::REG_DDRA, 0xF0);  // High nibble output
        REQUIRE(via.read(Via6522::REG_DDRA) == 0xF0);

        via.write(Via6522::REG_DDRB, 0x0F);  // Low nibble output
        REQUIRE(via.read(Via6522::REG_DDRB) == 0x0F);
    }
}

TEST_CASE("Via6522 port output", "[via][ports]") {
    Via6522 via;

    SECTION("Output bits come from OR when DDR is output") {
        via.write(Via6522::REG_DDRA, 0xFF);  // All outputs
        via.write(Via6522::REG_ORA, 0xAA);
        // Need to run a clock cycle for port value to update
        via.update_phi2_trailing_edge();
        REQUIRE(via.port_a().p == 0xAA);
    }

    SECTION("Input bits are pulled high") {
        via.write(Via6522::REG_DDRA, 0x00);  // All inputs
        via.update_phi2_trailing_edge();
        REQUIRE(via.port_a().p == 0xFF);
    }

    SECTION("Mixed input/output") {
        via.write(Via6522::REG_DDRA, 0xF0);  // High nibble output
        via.write(Via6522::REG_ORA, 0xA5);
        via.update_phi2_trailing_edge();
        // High nibble from OR (0xA0), low nibble pulled high (0x0F)
        REQUIRE(via.port_a().p == 0xAF);
    }
}

TEST_CASE("Via6522 port read", "[via][ports]") {
    Via6522 via;

    SECTION("Reading ORB returns output bits from OR, input bits from port") {
        via.write(Via6522::REG_DDRB, 0xF0);  // High nibble output
        via.write(Via6522::REG_ORB, 0xA0);
        via.update_phi2_trailing_edge();
        uint8_t value = via.read(Via6522::REG_ORB);
        // Output bits: 0xA0, input bits: 0x0F (pulled high)
        REQUIRE(value == 0xAF);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Timer 1 tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 timer 1 one-shot mode", "[via][timer][t1]") {
    Via6522 via;

    // Ensure one-shot mode (ACR bit 6 = 0)
    via.write(Via6522::REG_ACR, 0x00);

    // Enable T1 interrupt
    via.write(Via6522::REG_IER, 0xC0);  // Set bit 6 (T1)

    SECTION("Timer 1 fires after N+1.5 cycles") {
        // Program T1 for 5 cycle timeout
        via.write(Via6522::REG_T1LL, 5);
        via.write(Via6522::REG_T1CH, 0);  // Start timer

        // T1 should not have fired yet
        REQUIRE_FALSE(via.irq_pending());

        // Run 5 trailing edge + 5 leading edge cycles
        // Timer counts down on trailing edge
        // Initial timeout is N+1.5 cycles due to phase offset
        for (int i = 0; i < 6; i++) {
            via.update_phi2_trailing_edge();
            REQUIRE_FALSE(via.irq_pending());  // Not yet
            via.update_phi2_leading_edge();
        }

        // Run one more trailing edge (timeout happens)
        via.update_phi2_trailing_edge();
        // IRQ fires on leading edge
        via.update_phi2_leading_edge();

        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) != 0);  // T1 flag set
        REQUIRE(via.irq_pending());
    }

    SECTION("Reading T1C-L clears T1 interrupt") {
        via.write(Via6522::REG_T1LL, 0);
        via.write(Via6522::REG_T1CH, 0);

        // Run until timeout
        for (int i = 0; i < 3; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) != 0);

        // Read T1C-L to clear
        via.read(Via6522::REG_T1CL);
        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) == 0);
    }

    SECTION("One-shot mode does not re-fire") {
        via.write(Via6522::REG_T1LL, 2);
        via.write(Via6522::REG_T1CH, 0);

        // Run until first timeout
        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE(via.irq_pending());

        // Clear the interrupt
        via.read(Via6522::REG_T1CL);
        REQUIRE_FALSE(via.irq_pending());

        // Run more cycles - should not fire again
        for (int i = 0; i < 10; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE_FALSE(via.irq_pending());
    }
}

TEST_CASE("Via6522 timer 1 continuous mode", "[via][timer][t1]") {
    Via6522 via;

    // Enable continuous mode (ACR bit 6 = 1)
    via.write(Via6522::REG_ACR, 0x40);

    // Enable T1 interrupt
    via.write(Via6522::REG_IER, 0xC0);

    SECTION("Timer 1 re-fires in continuous mode") {
        via.write(Via6522::REG_T1LL, 3);
        via.write(Via6522::REG_T1CH, 0);

        // Run until first timeout
        int cycles = 0;
        while (!via.irq_pending() && cycles < 20) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
            cycles++;
        }
        REQUIRE(via.irq_pending());

        // Need to run a trailing edge first - reading T1C-L won't clear the
        // interrupt if it just timed out this cycle (matching B2 behavior)
        via.update_phi2_trailing_edge();

        // Now clear interrupt
        via.read(Via6522::REG_T1CL);
        REQUIRE_FALSE(via.irq_pending());

        // Run until second timeout
        cycles = 0;
        while (!via.irq_pending() && cycles < 20) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
            cycles++;
        }
        REQUIRE(via.irq_pending());  // Should fire again
    }
}

TEST_CASE("Via6522 timer 1 PB7 toggle", "[via][timer][t1][pb7]") {
    Via6522 via;

    // Enable T1 output to PB7 (ACR bit 7 = 1) and continuous mode
    via.write(Via6522::REG_ACR, 0xC0);  // bits 7 and 6

    SECTION("PB7 goes low when timer starts") {
        via.write(Via6522::REG_T1LL, 5);
        via.write(Via6522::REG_T1CH, 0);  // Start timer - PB7 should go low
        REQUIRE(via.state().t1_pb7 == 0);
    }

    SECTION("PB7 toggles on timeout") {
        via.write(Via6522::REG_T1LL, 2);
        via.write(Via6522::REG_T1CH, 0);

        REQUIRE(via.state().t1_pb7 == 0);  // Initially low

        // Run until timeout
        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE(via.state().t1_pb7 == 0x80);  // Toggled to high
    }
}

TEST_CASE("Via6522 T1 PB7 output appears on port_b.p with DDRB bit 7 = 0", "[via][timer][t1][pb7]") {
    Via6522 via;

    // Enable T1 output to PB7 (ACR bit 7 = 1) and continuous mode
    via.write(Via6522::REG_ACR, 0xC0);

    // DDRB bit 7 = 0 (all inputs) - T1 should still output to PB7
    via.write(Via6522::REG_DDRB, 0x00);

    SECTION("PB7 on port_b.p is low when timer starts") {
        via.write(Via6522::REG_T1LL, 5);
        via.write(Via6522::REG_T1CH, 0);  // Start timer - PB7 should go low
        via.update_phi2_trailing_edge();

        REQUIRE(via.state().t1_pb7 == 0);
        // Key test: port_b.p bit 7 should be low even though DDRB bit 7 = 0
        REQUIRE((via.port_b().p & 0x80) == 0x00);
    }

    SECTION("PB7 on port_b.p toggles to high on timeout") {
        via.write(Via6522::REG_T1LL, 2);
        via.write(Via6522::REG_T1CH, 0);

        // Run until timeout
        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE(via.state().t1_pb7 == 0x80);
        // Key test: port_b.p bit 7 should be high even though DDRB bit 7 = 0
        REQUIRE((via.port_b().p & 0x80) == 0x80);
    }
}

TEST_CASE("Via6522 T1 PB7 output overrides DDRB when ACR bit 7 set", "[via][timer][t1][pb7]") {
    Via6522 via;

    // DDRB bit 7 = 0 (input)
    via.write(Via6522::REG_DDRB, 0x00);

    SECTION("Without T1 output mode, PB7 is input (pulled high)") {
        via.write(Via6522::REG_ACR, 0x00);  // T1 output disabled
        via.update_phi2_trailing_edge();

        // PB7 should be pulled high (input)
        REQUIRE((via.port_b().p & 0x80) == 0x80);
    }

    SECTION("With T1 output mode, PB7 is driven by T1") {
        via.write(Via6522::REG_ACR, 0xC0);  // T1 output enabled + continuous
        via.write(Via6522::REG_T1LL, 5);
        via.write(Via6522::REG_T1CH, 0);  // Start timer - PB7 goes low
        via.update_phi2_trailing_edge();

        // PB7 should be low (T1 output overrides DDR)
        REQUIRE((via.port_b().p & 0x80) == 0x00);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Timer 2 tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 timer 2 one-shot", "[via][timer][t2]") {
    Via6522 via;

    // Enable T2 interrupt
    via.write(Via6522::REG_IER, 0xA0);  // Set bit 5 (T2)

    SECTION("Timer 2 fires once") {
        via.write(Via6522::REG_T2CL, 3);  // Low latch
        via.write(Via6522::REG_T2CH, 0);  // High counter, starts timer

        REQUIRE_FALSE(via.irq_pending());

        // Run until timeout
        int cycles = 0;
        while (!via.irq_pending() && cycles < 20) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
            cycles++;
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) != 0);  // T2 flag set
        REQUIRE(via.irq_pending());
    }

    SECTION("Reading T2C-L clears T2 interrupt") {
        via.write(Via6522::REG_T2CL, 0);
        via.write(Via6522::REG_T2CH, 0);

        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) != 0);

        via.read(Via6522::REG_T2CL);
        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) == 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// IFR/IER tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 IFR operations", "[via][ifr]") {
    Via6522 via;

    SECTION("Writing 1s to IFR clears flags") {
        // Force T1 interrupt
        via.write(Via6522::REG_IER, 0xC0);
        via.write(Via6522::REG_T1LL, 0);
        via.write(Via6522::REG_T1CH, 0);

        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) != 0);

        // Clear T1 flag by writing 0x40 to IFR
        via.write(Via6522::REG_IFR, 0x40);
        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) == 0);
    }

    SECTION("IFR bit 7 reflects enabled interrupt state") {
        // No enabled interrupts = bit 7 clear
        REQUIRE((via.read(Via6522::REG_IFR) & 0x80) == 0);

        // Enable T1 and trigger it
        via.write(Via6522::REG_IER, 0xC0);
        via.write(Via6522::REG_T1LL, 0);
        via.write(Via6522::REG_T1CH, 0);

        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        // Enabled interrupt pending = bit 7 set
        REQUIRE((via.read(Via6522::REG_IFR) & 0x80) != 0);
    }
}

TEST_CASE("Via6522 IER operations", "[via][ier]") {
    Via6522 via;

    SECTION("IER bit 7 = 1 sets bits") {
        via.write(Via6522::REG_IER, 0x82);  // Set CA1 enable
        REQUIRE((via.read(Via6522::REG_IER) & 0x02) != 0);

        via.write(Via6522::REG_IER, 0xC0);  // Set T1 enable
        REQUIRE((via.read(Via6522::REG_IER) & 0x40) != 0);
        REQUIRE((via.read(Via6522::REG_IER) & 0x02) != 0);  // CA1 still set
    }

    SECTION("IER bit 7 = 0 clears bits") {
        via.write(Via6522::REG_IER, 0xFF);  // Enable all
        REQUIRE((via.read(Via6522::REG_IER) & 0x7F) == 0x7F);

        via.write(Via6522::REG_IER, 0x02);  // Clear CA1 enable
        REQUIRE((via.read(Via6522::REG_IER) & 0x02) == 0);
        REQUIRE((via.read(Via6522::REG_IER) & 0x40) != 0);  // T1 still set
    }

    SECTION("IER bit 7 always reads as 1") {
        via.write(Via6522::REG_IER, 0x00);  // Clear all
        REQUIRE((via.read(Via6522::REG_IER) & 0x80) == 0x80);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Shift register tests (basic)
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 shift register", "[via][sr]") {
    Via6522 via;

    SECTION("SR can be written and read") {
        via.write(Via6522::REG_SR, 0xA5);
        REQUIRE(via.read(Via6522::REG_SR) == 0xA5);
    }
}

//////////////////////////////////////////////////////////////////////////////
// ACR/PCR tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 ACR", "[via][acr]") {
    Via6522 via;

    SECTION("ACR can be written and read") {
        via.write(Via6522::REG_ACR, 0xC0);  // T1 continuous + PB7 output
        REQUIRE(via.read(Via6522::REG_ACR) == 0xC0);
    }
}

TEST_CASE("Via6522 PCR", "[via][pcr]") {
    Via6522 via;

    SECTION("PCR can be written and read") {
        via.write(Via6522::REG_PCR, 0xEE);  // CA2/CB2 high
        REQUIRE(via.read(Via6522::REG_PCR) == 0xEE);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Timer latch tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 timer latches", "[via][timer]") {
    Via6522 via;

    SECTION("T1 latches can be written without starting timer") {
        via.write(Via6522::REG_T1LL, 0x12);
        via.write(Via6522::REG_T1LH, 0x34);

        REQUIRE(via.read(Via6522::REG_T1LL) == 0x12);
        REQUIRE(via.read(Via6522::REG_T1LH) == 0x34);

        // Timer should not have started (t1_pending should be false)
        REQUIRE_FALSE(via.state().t1_pending);
    }

    SECTION("Writing T1C-H starts timer") {
        via.write(Via6522::REG_T1LL, 0x56);
        via.write(Via6522::REG_T1CH, 0x78);  // This starts the timer

        REQUIRE(via.state().t1_pending);
        REQUIRE(via.state().t1_reload);
    }
}

//////////////////////////////////////////////////////////////////////////////
// peek() side-effect-free read tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 peek does not clear interrupt flags", "[via][peek]") {
    Via6522 via;

    SECTION("peek T1CL does not clear T1 interrupt flag") {
        // Set up timer to fire immediately
        via.write(Via6522::REG_T1LL, 0x01);
        via.write(Via6522::REG_T1LH, 0x00);
        via.write(Via6522::REG_T1CH, 0x00);  // Start timer with counter = 0x0001

        // Tick until timer fires
        for (int i = 0; i < 10; ++i) {
            via.tick_rising();
            via.tick_falling();
        }

        // Enable T1 interrupt
        via.write(Via6522::REG_IER, 0xC0);  // Set bit 6 (T1) with bit 7 high

        // Verify T1 interrupt is set
        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) == 0x40);

        // peek should return same value as read but NOT clear the flag
        uint8_t peek_value = via.peek(Via6522::REG_T1CL);
        (void)peek_value;  // Suppress unused warning

        // IFR should still have T1 flag set
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

        // Now read() should clear it
        (void)via.read(Via6522::REG_T1CL);
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x00);
    }

    SECTION("peek T2CL does not clear T2 interrupt flag") {
        // Set up T2 timer to fire immediately
        via.write(Via6522::REG_T2CL, 0x01);
        via.write(Via6522::REG_T2CH, 0x00);  // Start timer with counter = 0x0001

        // Tick until timer fires
        for (int i = 0; i < 10; ++i) {
            via.tick_rising();
            via.tick_falling();
        }

        // Enable T2 interrupt
        via.write(Via6522::REG_IER, 0xA0);  // Set bit 5 (T2) with bit 7 high

        // Verify T2 interrupt is set
        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) == 0x20);

        // peek should NOT clear the flag
        uint8_t peek_value = via.peek(Via6522::REG_T2CL);
        (void)peek_value;
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x20);

        // read() should clear it
        (void)via.read(Via6522::REG_T2CL);
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x00);
    }

    SECTION("peek returns same register values as read") {
        // Write some values
        via.write(Via6522::REG_DDRA, 0x55);
        via.write(Via6522::REG_DDRB, 0xAA);
        via.write(Via6522::REG_ACR, 0x12);
        via.write(Via6522::REG_PCR, 0x34);

        // peek should return same values
        REQUIRE(via.peek(Via6522::REG_DDRA) == 0x55);
        REQUIRE(via.peek(Via6522::REG_DDRB) == 0xAA);
        REQUIRE(via.peek(Via6522::REG_ACR) == 0x12);
        REQUIRE(via.peek(Via6522::REG_PCR) == 0x34);

        // IER bit 7 always reads as 1
        REQUIRE((via.peek(Via6522::REG_IER) & 0x80) == 0x80);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Category A: Timer Counter Value Sequence Tests
// Based on real hardware tests from stardot.org.uk/forums/viewtopic.php?t=16138
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 T1 counter sequence one-shot", "[via][timer][t1][timing]") {
    Via6522 via;

    // Enable one-shot mode (ACR bit 6 = 0)
    via.write(Via6522::REG_ACR, 0x00);

    // Program T1 for latch value 4
    via.write(Via6522::REG_T1LL, 4);
    via.write(Via6522::REG_T1LH, 0);
    via.write(Via6522::REG_T1CH, 0);  // Start timer

    // After write to T1CH, t1_reload should be true
    REQUIRE(via.state().t1_reload);

    // First trailing edge: load counter from latch
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 4);

    // Count down: 4, 3, 2, 1, 0, FFFF
    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 3);

    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 2);

    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 1);

    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 0);

    // Next count wraps to 0xFFFF - this is when timeout is detected
    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 0xFFFF);
    REQUIRE(via.state().t1_reload);  // Reload scheduled

    // Leading edge: IRQ fires
    via.update_phi2_leading_edge();
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

    // Next trailing edge: reload from latch (even in one-shot mode, counter reloads)
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 4);

    // But no more IRQs in one-shot mode - t1_pending should be false
    REQUIRE_FALSE(via.state().t1_pending);
}

TEST_CASE("Via6522 T1 counter sequence continuous", "[via][timer][t1][timing]") {
    Via6522 via;

    // Enable continuous mode (ACR bit 6 = 1)
    via.write(Via6522::REG_ACR, 0x40);
    via.write(Via6522::REG_IER, 0xC0);  // Enable T1 interrupt

    // Program T1 for latch value 3
    via.write(Via6522::REG_T1LL, 3);
    via.write(Via6522::REG_T1LH, 0);
    via.write(Via6522::REG_T1CH, 0);

    // Load counter
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t1 == 3);

    // Count to first timeout
    for (int i = 0; i < 4; i++) {
        via.update_phi2_leading_edge();
        via.update_phi2_trailing_edge();
    }
    REQUIRE(via.state().t1 == 0xFFFF);

    // IRQ fires on leading edge
    via.update_phi2_leading_edge();
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

    // Clear interrupt and verify t1_pending is still true (continuous mode)
    via.update_phi2_trailing_edge();  // Clear timeout flag first
    via.read(Via6522::REG_T1CL);
    REQUIRE(via.state().t1_pending);

    // Counter should have reloaded
    REQUIRE(via.state().t1 == 3);

    // Verify it fires again
    for (int i = 0; i < 4; i++) {
        via.update_phi2_leading_edge();
        via.update_phi2_trailing_edge();
    }
    via.update_phi2_leading_edge();
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);
}

TEST_CASE("Via6522 T2 counter sequence", "[via][timer][t2][timing]") {
    Via6522 via;

    // T2 is always one-shot, counting 1MHz clock by default
    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xA0);  // Enable T2 interrupt

    // Program T2 for latch value 4
    via.write(Via6522::REG_T2CL, 4);
    via.write(Via6522::REG_T2CH, 0);

    // Load counter
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 4);

    // Count down: 4, 3, 2, 1, 0, FFFF, FFFE...
    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 3);

    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 2);

    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 1);

    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 0);

    // Wrap to 0xFFFF - timeout detected
    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 0xFFFF);

    // IRQ fires on leading edge
    via.update_phi2_leading_edge();
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x20);

    // T2 does NOT reload - continues free-running
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 0xFFFE);

    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 0xFFFD);
}

TEST_CASE("Via6522 counter value at IRQ assertion", "[via][timer][timing]") {
    Via6522 via;

    SECTION("T1 counter is 0xFFFF when IRQ fires") {
        via.write(Via6522::REG_ACR, 0x00);
        via.write(Via6522::REG_IER, 0xC0);
        via.write(Via6522::REG_T1LL, 2);
        via.write(Via6522::REG_T1CH, 0);

        // Run to timeout
        for (int i = 0; i < 4; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        // At the moment IFR.T1 gets set, counter should be 0xFFFF
        REQUIRE(via.state().t1 == 0xFFFF);
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);
    }

    SECTION("T2 counter is 0xFFFF when IRQ fires") {
        via.write(Via6522::REG_ACR, 0x00);
        via.write(Via6522::REG_IER, 0xA0);
        via.write(Via6522::REG_T2CL, 2);
        via.write(Via6522::REG_T2CH, 0);

        // Run to timeout
        for (int i = 0; i < 4; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE(via.state().t2 == 0xFFFF);
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x20);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Category B: Same-Cycle IRQ Acknowledgment Tests
// Tests for the 500ns delay where reading T1CL/T2CL during timeout cycle
// should NOT clear the flag
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 same-cycle T1CL read does not clear IRQ", "[via][timer][t1][timing][ack]") {
    Via6522 via;

    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xC0);
    via.write(Via6522::REG_T1LL, 2);
    via.write(Via6522::REG_T1CH, 0);

    // Run to the trailing edge where timeout is detected
    // Counter goes: load(2), 1, 0, FFFF (timeout)
    for (int i = 0; i < 3; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }
    via.update_phi2_trailing_edge();  // timeout detected here

    // t1_timeout should be true
    REQUIRE(via.state().t1_timeout);

    // Read T1CL BEFORE leading edge - should NOT clear flag because t1_timeout is true
    via.read(Via6522::REG_T1CL);

    // Now run leading edge - this is when IRQ flag gets set
    via.update_phi2_leading_edge();

    // Flag should be SET (timer firing wins over acknowledgment attempt)
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);
}

TEST_CASE("Via6522 T1CL read after timeout cycle clears IRQ", "[via][timer][t1][timing][ack]") {
    Via6522 via;

    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xC0);
    via.write(Via6522::REG_T1LL, 2);
    via.write(Via6522::REG_T1CH, 0);

    // Run to timeout and beyond
    for (int i = 0; i < 4; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }

    // IRQ should be set
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

    // Run another trailing edge to clear the timeout flag
    via.update_phi2_trailing_edge();
    REQUIRE_FALSE(via.state().t1_timeout);

    // Now reading T1CL should clear the flag
    via.read(Via6522::REG_T1CL);
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x00);
}

TEST_CASE("Via6522 same-cycle T2CL read does not clear IRQ", "[via][timer][t2][timing][ack]") {
    Via6522 via;

    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xA0);
    via.write(Via6522::REG_T2CL, 2);
    via.write(Via6522::REG_T2CH, 0);

    // Run to timeout trailing edge
    for (int i = 0; i < 3; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }
    via.update_phi2_trailing_edge();

    REQUIRE(via.state().t2_timeout);

    // Read T2CL during timeout cycle
    via.read(Via6522::REG_T2CL);

    // Leading edge: flag gets set
    via.update_phi2_leading_edge();

    // Flag should be SET
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x20);
}

TEST_CASE("Via6522 write T1CH same cycle as IRQ restarts timer but IRQ fires", "[via][timer][t1][timing][ack]") {
    Via6522 via;

    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xC0);
    via.write(Via6522::REG_T1LL, 2);
    via.write(Via6522::REG_T1CH, 0);

    // Run to timeout trailing edge
    for (int i = 0; i < 3; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }
    via.update_phi2_trailing_edge();

    REQUIRE(via.state().t1_timeout);

    // Write T1CH during timeout cycle - this starts a new timer
    via.write(Via6522::REG_T1LL, 10);  // New latch value
    via.write(Via6522::REG_T1CH, 0);

    // Leading edge: IRQ from the OLD timeout should still fire
    via.update_phi2_leading_edge();

    // Flag should be SET (the old IRQ fires before new timer takes over)
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

    // New timer should be pending
    REQUIRE(via.state().t1_pending);
    REQUIRE(via.state().t1_reload);
}

TEST_CASE("Via6522 write IFR same cycle as IRQ - timer firing wins", "[via][timer][t1][timing][ack]") {
    Via6522 via;

    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xC0);
    via.write(Via6522::REG_T1LL, 2);
    via.write(Via6522::REG_T1CH, 0);

    // Run to timeout trailing edge
    for (int i = 0; i < 3; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }
    via.update_phi2_trailing_edge();

    REQUIRE(via.state().t1_timeout);

    // Write to IFR to try to clear T1 flag BEFORE leading edge
    via.write(Via6522::REG_IFR, 0x40);

    // Leading edge: timer firing should WIN
    via.update_phi2_leading_edge();

    // Flag should be SET because timer firing takes precedence
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);
}

//////////////////////////////////////////////////////////////////////////////
// Category C: T1LH Write Behavior Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 T1LH write clears T1 flag in one-shot mode", "[via][timer][t1][t1lh]") {
    Via6522 via;

    // One-shot mode
    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xC0);
    via.write(Via6522::REG_T1LL, 2);
    via.write(Via6522::REG_T1CH, 0);

    // Run to IRQ
    for (int i = 0; i < 5; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }

    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

    // Clear timeout flag first
    via.update_phi2_trailing_edge();

    // Writing T1LH should clear the flag (regardless of mode)
    via.write(Via6522::REG_T1LH, 0x10);
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x00);
}

TEST_CASE("Via6522 T1LH write clears T1 flag in continuous mode", "[via][timer][t1][t1lh]") {
    Via6522 via;

    // Continuous mode
    via.write(Via6522::REG_ACR, 0x40);
    via.write(Via6522::REG_IER, 0xC0);
    via.write(Via6522::REG_T1LL, 2);
    via.write(Via6522::REG_T1CH, 0);

    // Run to IRQ
    for (int i = 0; i < 5; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }

    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

    // Clear timeout flag
    via.update_phi2_trailing_edge();

    // Writing T1LH should clear the flag
    via.write(Via6522::REG_T1LH, 0x10);
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x00);
}

TEST_CASE("Via6522 T1LH write does NOT clear flag during timeout cycle", "[via][timer][t1][t1lh][timing]") {
    Via6522 via;

    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_IER, 0xC0);
    via.write(Via6522::REG_T1LL, 2);
    via.write(Via6522::REG_T1CH, 0);

    // Run to timeout trailing edge
    for (int i = 0; i < 3; i++) {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    }
    via.update_phi2_trailing_edge();

    REQUIRE(via.state().t1_timeout);

    // Write T1LH during timeout cycle - should NOT clear because timeout active
    via.write(Via6522::REG_T1LH, 0x10);

    // Leading edge fires the IRQ
    via.update_phi2_leading_edge();

    // Flag should be SET because timer firing wins
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);
}

//////////////////////////////////////////////////////////////////////////////
// Category D: T2 Pulse Counting Mode Tests
// ACR bit 5 controls T2: 0 = count 1MHz clock, 1 = count PB6 falling edges
//////////////////////////////////////////////////////////////////////////////

// Mock peripheral to control PB6 for pulse counting tests
class MockPulsePeripheral : public ViaPeripheral {
public:
    uint8_t pb6_value = 0x40;  // PB6 high initially

    uint8_t update_port_a(uint8_t out, uint8_t ddr) override {
        (void)out; (void)ddr;
        return 0xFF;
    }

    uint8_t update_port_b(uint8_t out, uint8_t ddr) override {
        (void)out; (void)ddr;
        return pb6_value;  // Return current PB6 state
    }

    void update_control_lines(uint8_t& ca1, uint8_t& ca2,
                             uint8_t& cb1, uint8_t& cb2) override {
        (void)ca1; (void)ca2; (void)cb1; (void)cb2;
    }

    void set_pb6_high() { pb6_value = 0x40; }
    void set_pb6_low() { pb6_value = 0x00; }
};

TEST_CASE("Via6522 T2 pulse counting mode does not count 1MHz clock", "[via][timer][t2][pulse]") {
    MockPulsePeripheral peripheral;
    Via6522 via(peripheral);

    // Enable T2 pulse counting mode (ACR bit 5 = 1)
    via.write(Via6522::REG_ACR, 0x20);
    via.write(Via6522::REG_IER, 0xA0);

    // Set DDRB to make PB6 an input
    via.write(Via6522::REG_DDRB, 0x00);

    // Program T2
    via.write(Via6522::REG_T2CL, 10);
    via.write(Via6522::REG_T2CH, 0);

    // Load counter
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 10);

    // Run several 1MHz cycles with PB6 held HIGH - counter should NOT decrement
    peripheral.set_pb6_high();
    for (int i = 0; i < 5; i++) {
        via.update_phi2_leading_edge();
        via.update_phi2_trailing_edge();
    }

    // Counter should still be 10 (no decrement because PB6 didn't transition)
    REQUIRE(via.state().t2 == 10);
}

TEST_CASE("Via6522 T2 counts PB6 falling edges in pulse counting mode", "[via][timer][t2][pulse]") {
    MockPulsePeripheral peripheral;
    Via6522 via(peripheral);

    // Enable T2 pulse counting mode
    via.write(Via6522::REG_ACR, 0x20);
    via.write(Via6522::REG_IER, 0xA0);
    via.write(Via6522::REG_DDRB, 0x00);

    // Program T2
    via.write(Via6522::REG_T2CL, 5);
    via.write(Via6522::REG_T2CH, 0);

    // Load counter
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 5);
    REQUIRE(via.state().t2_count == false);  // Should be false in pulse mode

    // Verify peripheral is connected
    REQUIRE(via.peripheral() != nullptr);
    REQUIRE(via.peripheral() == &peripheral);

    // Verify peripheral directly returns correct value
    uint8_t direct_call = peripheral.update_port_b(0, 0);
    INFO("direct peripheral call: " << (int)direct_call);
    REQUIRE(direct_call == 0x40);

    // Verify via peripheral returns correct value
    uint8_t via_peripheral_call = via.peripheral()->update_port_b(0, 0);
    INFO("via peripheral call: " << (int)via_peripheral_call);
    REQUIRE(via_peripheral_call == 0x40);

    // Verify peripheral is connected and returning correct value
    // After trailing_edge, port_b.p should be updated from peripheral
    INFO("port_b.p after load: " << (int)via.state().port_b.p);
    INFO("peripheral pb6_value: " << (int)peripheral.pb6_value);
    REQUIRE(via.state().port_b.p == 0x40);  // Should be from peripheral

    // Step-by-step falling edge detection with assertions
    // Initial: t2=5, old_pb=0, port_b.p=0x40 (from peripheral), t2_count=false

    // Step 1: Set peripheral high (no-op, already 0x40)
    peripheral.set_pb6_high();

    // Step 2: Trailing edge - port_b.p = 0x40
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().port_b.p == 0x40);
    REQUIRE(via.state().t2 == 5);  // No decrement (t2_count was false)

    // Step 3: Leading edge - set old_pb = 0x40, t2_count = false (no edge)
    via.update_phi2_leading_edge();
    REQUIRE(via.state().old_pb == 0x40);
    REQUIRE(via.state().t2_count == false);  // No falling edge yet

    // Step 4: Set peripheral low
    peripheral.set_pb6_low();

    // Step 5: Trailing edge - port_b.p = 0x00
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().port_b.p == 0x00);
    REQUIRE(via.state().t2 == 5);  // No decrement (t2_count was false)

    // Step 6: Leading edge - detect falling edge, t2_count = true
    via.update_phi2_leading_edge();
    REQUIRE(via.state().old_pb == 0x00);  // Updated to current port_b.p
    REQUIRE(via.state().t2_count == true);  // Falling edge detected!

    // Step 7: Trailing edge - counter decrements
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 4);  // Decremented!

    // Step 8: Leading edge - clear t2_count
    via.update_phi2_leading_edge();
    REQUIRE(via.state().t2_count == false);  // No edge (low -> low)

    // Generate another falling edge using helper
    auto generate_falling_edge = [&]() {
        peripheral.set_pb6_high();
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();

        peripheral.set_pb6_low();
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    };

    // Second falling edge: 4 -> 3
    generate_falling_edge();
    REQUIRE(via.state().t2 == 3);
}

TEST_CASE("Via6522 T2 pulse counting fires IRQ on underflow", "[via][timer][t2][pulse]") {
    MockPulsePeripheral peripheral;
    Via6522 via(peripheral);

    // Enable T2 pulse counting mode
    via.write(Via6522::REG_ACR, 0x20);
    via.write(Via6522::REG_IER, 0xA0);
    via.write(Via6522::REG_DDRB, 0x00);

    // Program T2 for quick expiry
    via.write(Via6522::REG_T2CL, 2);
    via.write(Via6522::REG_T2CH, 0);

    // Load counter
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 2);

    // Helper lambda for generating a falling edge and leaving VIA in clean state
    auto generate_falling_edge = [&]() {
        peripheral.set_pb6_high();
        via.update_phi2_trailing_edge();  // port_b.p = high
        via.update_phi2_leading_edge();   // old_pb = high

        peripheral.set_pb6_low();
        via.update_phi2_trailing_edge();  // port_b.p = low
        via.update_phi2_leading_edge();   // detect edge, t2_count = true
        via.update_phi2_trailing_edge();  // counter decrements
        via.update_phi2_leading_edge();   // clear t2_count
    };

    // Generate 3 falling edges: 2 -> 1 -> 0 -> FFFF
    generate_falling_edge();  // 2 -> 1
    REQUIRE(via.state().t2 == 1);

    generate_falling_edge();  // 1 -> 0
    REQUIRE(via.state().t2 == 0);

    generate_falling_edge();  // 0 -> FFFF (timeout)

    // Counter should have wrapped to 0xFFFF and IRQ fired
    REQUIRE(via.state().t2 == 0xFFFF);
    REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x20);
}

TEST_CASE("Via6522 T2 continues free-running after timeout in pulse mode", "[via][timer][t2][pulse]") {
    MockPulsePeripheral peripheral;
    Via6522 via(peripheral);

    // Enable T2 pulse counting mode
    via.write(Via6522::REG_ACR, 0x20);
    via.write(Via6522::REG_IER, 0xA0);
    via.write(Via6522::REG_DDRB, 0x00);

    // Program T2 for quick expiry
    via.write(Via6522::REG_T2CL, 1);
    via.write(Via6522::REG_T2CH, 0);

    // Load counter
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 1);

    // Helper lambda for generating a falling edge and leaving VIA in clean state
    auto generate_falling_edge = [&]() {
        peripheral.set_pb6_high();
        via.update_phi2_trailing_edge();  // port_b.p = high
        via.update_phi2_leading_edge();   // old_pb = high

        peripheral.set_pb6_low();
        via.update_phi2_trailing_edge();  // port_b.p = low
        via.update_phi2_leading_edge();   // detect edge, t2_count = true
        via.update_phi2_trailing_edge();  // counter decrements
        via.update_phi2_leading_edge();   // clear t2_count
    };

    // First edge: 1 -> 0
    generate_falling_edge();
    REQUIRE(via.state().t2 == 0);

    // Second edge: 0 -> FFFF (timeout)
    generate_falling_edge();
    REQUIRE(via.state().t2 == 0xFFFF);

    // Third edge: FFFF -> FFFE (continues counting after timeout)
    generate_falling_edge();
    REQUIRE(via.state().t2 == 0xFFFE);
}

TEST_CASE("Via6522 T2 switching between clock and pulse modes", "[via][timer][t2][pulse]") {
    MockPulsePeripheral peripheral;
    Via6522 via(peripheral);

    via.write(Via6522::REG_DDRB, 0x00);

    // Start in clock mode
    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_T2CL, 10);
    via.write(Via6522::REG_T2CH, 0);

    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 10);

    // Run a few clock cycles - counter should decrement
    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 8);

    // Switch to pulse mode
    via.write(Via6522::REG_ACR, 0x20);

    // Run several clock cycles with PB6 high - no decrement
    peripheral.set_pb6_high();
    for (int i = 0; i < 3; i++) {
        via.update_phi2_leading_edge();
        via.update_phi2_trailing_edge();
    }
    REQUIRE(via.state().t2 == 8);

    // Switch back to clock mode - counting resumes
    via.write(Via6522::REG_ACR, 0x00);
    via.update_phi2_leading_edge();
    via.update_phi2_trailing_edge();
    REQUIRE(via.state().t2 == 7);
}
