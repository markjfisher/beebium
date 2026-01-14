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

// CRTC timing diagnostic - check register values and calculate VSYNC frequency
//
// This test examines CRTC register values after boot to understand why
// VSYNC frequency is ~8 Hz instead of the expected 50 Hz.

#include <catch2/catch_test_macros.hpp>
#include "test_mode7_helpers.hpp"
#include <iostream>
#include <iomanip>

using namespace beebium;
using namespace beebium::test;

#ifdef BEEBIUM_ROM_DIR

TEST_CASE("CRTC timing diagnostic", "[crtc][vsync][diagnostic]") {
    Mode7TestContext<ModelB> ctx;
    REQUIRE(ctx.booted);

    // CRTC registers (FE00-FE01: address/status, read/write)
    constexpr uint16_t CRTC_ADDR = 0xFE00;
    constexpr uint16_t CRTC_DATA = 0xFE01;

    std::cout << "\n=== CRTC Register Values After Boot ===\n";

    // Read all CRTC registers
    uint8_t regs[18];
    for (int i = 0; i < 18; ++i) {
        ctx.machine.write(CRTC_ADDR, i);  // Select register
        regs[i] = ctx.machine.read(CRTC_DATA);  // Read value
    }

    // Display register values
    std::cout << std::hex << std::setfill('0');
    for (int i = 0; i < 18; ++i) {
        std::cout << "R" << std::dec << std::setw(2) << i << " = 0x"
                  << std::hex << std::setw(2) << (int)regs[i];

        // Add descriptions for key registers
        switch (i) {
            case 0: std::cout << "  (Horizontal Total - 1)"; break;
            case 1: std::cout << "  (Horizontal Displayed)"; break;
            case 2: std::cout << "  (HSYNC Position)"; break;
            case 3: std::cout << "  (Sync Widths)"; break;
            case 4: std::cout << "  (Vertical Total - 1)"; break;
            case 5: std::cout << "  (Vertical Total Adjust)"; break;
            case 6: std::cout << "  (Vertical Displayed)"; break;
            case 7: std::cout << "  (VSYNC Position)"; break;
            case 9: std::cout << "  (Max Scanline Address)"; break;
        }
        std::cout << "\n";
    }

    std::cout << std::dec;

    // Calculate timing
    uint8_t h_total = regs[0] + 1;  // R0 is (total - 1)
    uint8_t v_total = regs[4] + 1;  // R4 is (total - 1)
    uint8_t v_adjust = regs[5];     // R5 is extra scanlines
    uint8_t max_scanline = regs[9] + 1;  // R9 is (max - 1), scanlines per character row

    std::cout << "\n=== Timing Calculation ===\n";
    std::cout << "Horizontal total: " << (int)h_total << " characters\n";
    std::cout << "Vertical total: " << (int)v_total << " character rows\n";
    std::cout << "Vertical adjust: " << (int)v_adjust << " extra scanlines\n";
    std::cout << "Scanlines per character row: " << (int)max_scanline << "\n";

    // Total scanlines per frame
    int total_scanlines = (v_total * max_scanline) + v_adjust;
    std::cout << "Total scanlines per frame: " << total_scanlines << "\n";

    // Character clock frequency
    // MODE 7: 1 MHz character clock (8 pixels per char at 8 MHz pixel clock)
    // Modes 0-6: 2 MHz character clock (4-8 pixels per char at 16 MHz pixel clock)
    //
    // Check video ULA to determine mode
    bool fast_clock = ctx.machine.memory().video_ula.fast_clock();
    int char_clock_mhz = fast_clock ? 2 : 1;
    std::cout << "Character clock: " << char_clock_mhz << " MHz "
              << (fast_clock ? "(Modes 0-6)" : "(Mode 7)") << "\n";

    // Frame frequency calculation
    // Frames per second = char_clock_hz / (h_total * total_scanlines)
    int char_clock_hz = char_clock_mhz * 1000000;
    double frame_hz = (double)char_clock_hz / (h_total * total_scanlines);

    std::cout << "Calculated VSYNC frequency: " << std::fixed << std::setprecision(2)
              << frame_hz << " Hz\n";
    std::cout << "Expected for PAL: 50.00 Hz\n";

    // Check if timing is correct
    if (frame_hz < 40.0 || frame_hz > 60.0) {
        std::cout << "\n❌ WARNING: VSYNC frequency out of range!\n";
        std::cout << "   Expected ~50 Hz, got " << frame_hz << " Hz\n";
        std::cout << "   This would cause:" << "\n";
        std::cout << "   - Incorrect video timing\n";
        std::cout << "   - Too few VSYNC interrupts\n";
        std::cout << "   - MOS sound queue processing too slow\n";
    } else {
        std::cout << "\n✓ VSYNC frequency is correct\n";
    }

    // Check if CRTC is ticking
    std::cout << "\n=== Verifying CRTC is Ticking ===\n";

    // Read light pen register (changes every frame)
    ctx.machine.write(CRTC_ADDR, 16);  // R16 = Light Pen High
    uint8_t lp_high_before = ctx.machine.read(CRTC_DATA);
    ctx.machine.write(CRTC_ADDR, 17);  // R17 = Light Pen Low
    uint8_t lp_low_before = ctx.machine.read(CRTC_DATA);

    uint16_t lp_before = (lp_high_before << 8) | lp_low_before;
    std::cout << "Light pen address before: 0x" << std::hex << lp_before << std::dec << "\n";

    // Run for 0.1 seconds
    for (int i = 0; i < 200000; ++i) {
        ctx.machine.step();
    }

    ctx.machine.write(CRTC_ADDR, 16);
    uint8_t lp_high_after = ctx.machine.read(CRTC_DATA);
    ctx.machine.write(CRTC_ADDR, 17);
    uint8_t lp_low_after = ctx.machine.read(CRTC_DATA);

    uint16_t lp_after = (lp_high_after << 8) | lp_low_after;
    std::cout << "Light pen address after:  0x" << std::hex << lp_after << std::dec << "\n";

    if (lp_before == lp_after) {
        std::cout << "❌ CRTC may not be ticking (light pen address unchanged)\n";
    } else {
        std::cout << "✓ CRTC is ticking\n";
    }

    // This is a diagnostic test - always pass
    REQUIRE(true);
}

#endif
