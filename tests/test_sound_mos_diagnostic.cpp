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

// Diagnostic test to understand why MOS isn't writing to sound chip.
//
// Key finding from BBC Micro manuals: Sound processing happens during Timer 1
// (100 Hz) interrupts, NOT VSYNC events. The MOS performs "one element of
// sound processing" during each Timer 1 interrupt.
//
// This test checks:
// 1. Timer 1 configuration (latch ~10000, continuous mode, interrupt enabled)
// 2. MOS sound flags ($0262 sound disable, $02CE sound updating)
// 3. Timer 1 interrupt frequency (~100 Hz)
// 4. Sound buffer state after SOUND command

#include <catch2/catch_test_macros.hpp>
#include "test_mode7_helpers.hpp"
#include "test_keyboard_helpers.hpp"
#include <beebium/Machines.hpp>

#include <iostream>
#include <iomanip>

using namespace beebium;
using namespace beebium::test;

#ifdef BEEBIUM_ROM_DIR

// System VIA addresses
constexpr uint16_t VIA_PORT_B = 0xFE40;  // Port B (addressable latch control)
constexpr uint16_t VIA_PORT_A = 0xFE41;  // Port A (sound chip data)
constexpr uint16_t VIA_DDRB   = 0xFE42;  // Data Direction Register B
constexpr uint16_t VIA_DDRA   = 0xFE43;  // Data Direction Register A
constexpr uint16_t VIA_T1CL   = 0xFE44;  // Timer 1 Counter Low
constexpr uint16_t VIA_T1CH   = 0xFE45;  // Timer 1 Counter High
constexpr uint16_t VIA_T1LL   = 0xFE46;  // Timer 1 Latch Low
constexpr uint16_t VIA_T1LH   = 0xFE47;  // Timer 1 Latch High
constexpr uint16_t VIA_ACR    = 0xFE4B;  // Auxiliary Control Register
constexpr uint16_t VIA_PCR    = 0xFE4C;  // Peripheral Control Register
constexpr uint16_t VIA_IFR    = 0xFE4D;  // Interrupt Flag Register
constexpr uint16_t VIA_IER    = 0xFE4E;  // Interrupt Enable Register

// MOS workspace addresses (from Tony Lobster's MOS 1.20 disassembly)
constexpr uint16_t SOUND_DISABLE        = 0x0262;  // Non-zero disables sound
constexpr uint16_t SOUND_UPDATING       = 0x02CE;  // $FF if updating, $00 otherwise
constexpr uint16_t BUFFER_EMPTY_FLAGS   = 0x02D3;  // Bit 7 set if buffer empty (ch 0-3)
constexpr uint16_t CHANNEL_OCCUPANCY    = 0x0804;  // Bit 7 set if channel playing (ch 0-3)
constexpr uint16_t CHANNEL_VOLUME       = 0x0808;  // Current amplitude (ch 0-3)
constexpr uint16_t CHANNEL_DURATION     = 0x081C;  // Remaining duration (ch 0-3)
constexpr uint16_t CHANNEL_PITCH        = 0x0830;  // Current pitch (ch 0-3)
constexpr uint16_t SOUND_BUFFER_BASE    = 0x0840;  // Sound buffers (16 bytes each)

TEST_CASE("MOS Timer 1 and sound system diagnostic", "[sound][diagnostic][mos]") {
    Mode7TestContext<ModelB> ctx;  // Use ModelB for MOS 1.20 disassembly reference
    REQUIRE(ctx.booted);

    SECTION("Timer 1 configuration after boot") {
        // Read Timer 1 latch (should be ~10000 for 100 Hz at 1 MHz)
        uint16_t latch = ctx.machine.read(VIA_T1LL) |
                         (ctx.machine.read(VIA_T1LH) << 8);

        // ACR bits 6-7: 01 = continuous mode with PB7 output disabled
        //               00 = one-shot mode
        //               10 = continuous mode with PB7 toggle
        //               11 = one-shot mode with PB7 pulse
        uint8_t acr = ctx.machine.read(VIA_ACR);
        uint8_t t1_mode = (acr >> 6) & 0x03;

        // IER bit 6: Timer 1 interrupt enabled
        uint8_t ier = ctx.machine.read(VIA_IER);
        bool t1_enabled = (ier & 0x40) != 0;

        std::cout << "\n=== Timer 1 Configuration ===\n";
        std::cout << "Timer 1 latch: " << latch << " (expect ~10000 for 100Hz @ 1MHz)\n";
        std::cout << "ACR: 0x" << std::hex << (int)acr << std::dec
                  << ", T1 mode bits: " << (int)t1_mode;
        switch (t1_mode) {
            case 0: std::cout << " (one-shot, no PB7)"; break;
            case 1: std::cout << " (continuous, no PB7)"; break;
            case 2: std::cout << " (continuous, PB7 toggle)"; break;
            case 3: std::cout << " (one-shot, PB7 pulse)"; break;
        }
        std::cout << "\n";
        std::cout << "IER: 0x" << std::hex << (int)ier << std::dec
                  << ", Timer 1 enabled: " << (t1_enabled ? "YES" : "NO") << "\n";

        // Expected values for working MOS configuration
        INFO("Timer 1 latch should be ~10000 for 100Hz at 1MHz");
        REQUIRE(latch >= 9000);
        REQUIRE(latch <= 11000);

        INFO("Timer 1 should be in continuous mode (ACR bits 6-7 = 01)");
        REQUIRE(t1_mode == 1);

        INFO("Timer 1 interrupt should be enabled (IER bit 6)");
        REQUIRE(t1_enabled);
    }

    SECTION("MOS sound flags after boot") {
        uint8_t sound_disable = ctx.machine.read(SOUND_DISABLE);
        uint8_t sound_updating = ctx.machine.read(SOUND_UPDATING);

        std::cout << "\n=== MOS Sound Flags ===\n";
        std::cout << "Sound disable flag ($0262): " << (int)sound_disable
                  << (sound_disable ? " (DISABLED!)" : " (enabled)") << "\n";
        std::cout << "Sound updating flag ($02CE): 0x" << std::hex
                  << (int)sound_updating << std::dec << "\n";

        // Dump buffer empty flags for all 4 channels
        std::cout << "Buffer empty flags:\n";
        for (int ch = 0; ch < 4; ++ch) {
            uint8_t flag = ctx.machine.read(BUFFER_EMPTY_FLAGS + ch);
            std::cout << "  Channel " << ch << " ($" << std::hex << (BUFFER_EMPTY_FLAGS + ch)
                      << "): 0x" << (int)flag << std::dec
                      << (flag & 0x80 ? " (EMPTY)" : " (has data)") << "\n";
        }

        INFO("Sound should be enabled ($0262 = 0)");
        REQUIRE(sound_disable == 0);
    }

    SECTION("Timer 1 interrupt frequency") {
        // Count Timer 1 IFR bit sets over 1 second
        // We detect rising edges of the T1 flag
        int timer1_interrupts = 0;
        uint8_t prev_ifr = 0;

        std::cout << "\n=== Timer 1 Interrupt Frequency Test ===\n";
        std::cout << "Running 2,000,000 cycles (1 second @ 2 MHz)...\n";

        for (int i = 0; i < 2'000'000; ++i) {
            ctx.machine.step();
            if (ctx.machine.memory().video_output.has_value()) {
                ctx.renderer.process(ctx.machine.memory().video_output.value());
            }

            // Check IFR bit 6 (Timer 1) - count rising edges
            uint8_t ifr = ctx.machine.read(VIA_IFR);
            if ((ifr & 0x40) && !(prev_ifr & 0x40)) {
                timer1_interrupts++;
            }
            prev_ifr = ifr;
        }

        std::cout << "Timer 1 IFR rising edges: " << timer1_interrupts
                  << " (expected ~100)\n";

        // Should be approximately 100 Hz (allow tolerance for timing)
        INFO("Timer 1 should fire approximately 100 times per second");
        REQUIRE(timer1_interrupts >= 90);
        REQUIRE(timer1_interrupts <= 110);
    }

    SECTION("Sound buffer state after SOUND command") {
        ctx.machine.memory().enable_audio_output();

        std::cout << "\n=== Sound Buffer State After SOUND Command ===\n";
        std::cout << "Typing: SOUND 1,-1,100,10\n";

        // Type SOUND command
        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 1,-1,100,10\r");

        // Run briefly to let BASIC parse the command (50ms worth)
        for (int i = 0; i < 100'000; ++i) {
            ctx.machine.step();
            if (ctx.machine.memory().video_output.has_value()) {
                ctx.renderer.process(ctx.machine.memory().video_output.value());
            }
        }

        // Check buffer empty flag for channel 1
        uint8_t buf_empty_ch1 = ctx.machine.read(BUFFER_EMPTY_FLAGS + 1);
        std::cout << "Channel 1 buffer empty flag ($02D4): 0x" << std::hex
                  << (int)buf_empty_ch1 << std::dec
                  << (buf_empty_ch1 & 0x80 ? " (EMPTY)" : " (has data)") << "\n";

        // Check channel 1 workspace
        uint8_t occupancy = ctx.machine.read(CHANNEL_OCCUPANCY + 1);
        uint8_t volume = ctx.machine.read(CHANNEL_VOLUME + 1);
        uint8_t duration = ctx.machine.read(CHANNEL_DURATION + 1);
        uint8_t pitch = ctx.machine.read(CHANNEL_PITCH + 1);

        std::cout << "Channel 1 workspace:\n";
        std::cout << "  Occupancy ($0805): 0x" << std::hex << (int)occupancy
                  << std::dec << (occupancy & 0x80 ? " (PLAYING)" : " (idle)") << "\n";
        std::cout << "  Volume ($0809): " << (int)volume << "\n";
        std::cout << "  Duration ($081D): " << (int)duration << "\n";
        std::cout << "  Pitch ($0831): " << (int)pitch << "\n";

        // Dump channel 1 buffer (16 bytes at $0850-$085F)
        std::cout << "Channel 1 buffer ($0850-$085F):\n";
        for (int i = 0; i < 16; ++i) {
            uint8_t val = ctx.machine.read(SOUND_BUFFER_BASE + 0x10 + i);
            std::cout << "  $" << std::hex << std::setfill('0') << std::setw(4)
                      << (SOUND_BUFFER_BASE + 0x10 + i) << ": 0x"
                      << std::setw(2) << (int)val << std::dec << "\n";
        }

        // Check sound chip state
        auto tone1 = ctx.machine.memory().sound_chip.get_tone_channel_state(1);
        std::cout << "\nSound chip channel 1 state:\n";
        std::cout << "  Frequency: " << tone1.frequency << "\n";
        std::cout << "  Volume: " << (int)tone1.volume << "\n";
        std::cout << "  Frequency Hz: " << tone1.frequency_hz << "\n";

        // After SOUND command, we expect:
        // - Buffer should have data OR occupancy should show playing
        // Note: We're not asserting here to keep this diagnostic
        // REQUIRE(!(buf_empty_ch1 & 0x80) || (occupancy & 0x80));
    }

    SECTION("Sound chip state tracking during extended run") {
        ctx.machine.memory().enable_audio_output();

        std::cout << "\n=== Extended Run After SOUND Command ===\n";
        std::cout << "Typing: SOUND 1,-15,100,20\n";  // Use -15 (loudest) and longer duration

        // Check addressable latch state
        std::cout << "Addressable latch value: 0x" << std::hex
                  << (int)ctx.machine.memory().addressable_latch.value << std::dec
                  << ", SOUND_WRITE: " << (ctx.machine.memory().addressable_latch.sound_write_enabled() ? "ENABLED" : "disabled")
                  << "\n";

        // Clear write trace before SOUND command to capture actual sound writes
        ctx.machine.memory().sound_chip.clear_write_trace();

        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 1,-15,100,20\r");

        // Track state changes over time
        // Note: BBC BASIC channels map to SN76489 in REVERSE order!
        //   BASIC Channel 1 → SN76489 Tone 2
        //   BASIC Channel 2 → SN76489 Tone 1
        //   BASIC Channel 3 → SN76489 Tone 0
        auto initial_tone2 = ctx.machine.memory().sound_chip.get_tone_channel_state(2);

        std::cout << "Initial chip state (Tone2/BASIC ch1): freq=" << initial_tone2.frequency
                  << ", vol=" << (int)initial_tone2.volume << "\n";

        // Track minimum volume seen (lower is louder, 15 is silent)
        uint8_t min_volume_seen = 15;
        uint16_t freq_when_audible = 0;

        // Run for 2 seconds, checking every 10,000 cycles (5ms)
        int check_interval = 10'000;
        int total_checks = 400;  // 2 seconds

        for (int check = 0; check < total_checks; ++check) {
            for (int i = 0; i < check_interval; ++i) {
                ctx.machine.step();
                if (ctx.machine.memory().video_output.has_value()) {
                    ctx.renderer.process(ctx.machine.memory().video_output.value());
                }
            }

            auto tone2 = ctx.machine.memory().sound_chip.get_tone_channel_state(2);

            // Track if we ever see a non-silent volume
            if (tone2.volume < min_volume_seen) {
                min_volume_seen = tone2.volume;
                freq_when_audible = tone2.frequency;
                std::cout << "*** Volume changed at check " << (check + 1)
                          << ": vol=" << (int)tone2.volume
                          << ", freq=" << tone2.frequency << " ***\n";
            }

            // Every 40 checks (200ms), dump state
            if ((check + 1) % 40 == 0) {
                uint8_t buf_empty = ctx.machine.read(BUFFER_EMPTY_FLAGS + 1);
                uint8_t occupancy = ctx.machine.read(CHANNEL_OCCUPANCY + 1);
                uint8_t mos_vol = ctx.machine.read(CHANNEL_VOLUME + 1);
                std::cout << "Check " << (check + 1)
                          << ": chip vol=" << (int)tone2.volume
                          << ", mos vol=" << (int)mos_vol
                          << ", occupancy=0x" << std::hex << (int)occupancy
                          << ", buf_empty=0x" << (int)buf_empty
                          << std::dec << "\n";
            }
        }

        std::cout << "\n=== Summary ===\n";
        std::cout << "Minimum volume seen (Tone2/BASIC ch1): " << (int)min_volume_seen;
        if (min_volume_seen == 15) {
            std::cout << " (ALWAYS SILENT - MOS never set audible volume!)";
        } else {
            std::cout << " (AUDIBLE! freq=" << freq_when_audible << ")";
        }
        std::cout << "\n";

        // Dump ALL channels to see which one was written to
        std::cout << "\n=== Final Chip State (All Channels) ===\n";
        for (int ch = 0; ch < 3; ++ch) {
            auto tone = ctx.machine.memory().sound_chip.get_tone_channel_state(ch);
            std::cout << "Tone" << ch << ": freq=" << tone.frequency
                      << ", vol=" << (int)tone.volume
                      << ", Hz=" << tone.frequency_hz << "\n";
        }
        auto noise = ctx.machine.memory().sound_chip.get_noise_channel_state();
        std::cout << "Noise: vol=" << (int)noise.volume
                  << ", white=" << (noise.white_mode ? "yes" : "no")
                  << ", rate=" << (int)noise.rate_select << "\n";

        // Dump write trace to see what bytes were sent to the chip
        std::cout << "\n=== Sound Chip Write Trace ===\n";
        auto& chip = ctx.machine.memory().sound_chip;
        size_t trace_count = chip.get_write_trace_count();
        const auto* trace = chip.get_write_trace();
        std::cout << "Total writes: " << trace_count << "\n";

        // Register type names
        const char* reg_names[] = {
            "Tone0 Freq", "Tone0 Vol", "Tone1 Freq", "Tone1 Vol",
            "Tone2 Freq", "Tone2 Vol", "Noise Ctrl", "Noise Vol"
        };

        for (size_t i = 0; i < trace_count && i < 32; ++i) {
            const auto& entry = trace[i];
            std::cout << "  [" << std::dec << i << "] 0x" << std::hex << std::setfill('0')
                      << std::setw(2) << (int)entry.data << " ";
            if (entry.is_latch) {
                std::cout << "LATCH " << reg_names[entry.reg_type];
            } else {
                std::cout << "DATA  " << reg_names[entry.reg_type];
            }
            std::cout << std::dec << "\n";
        }
        if (trace_count > 32) {
            std::cout << "  ... (" << (trace_count - 32) << " more writes)\n";
        }

        // Expectation: we should have seen volume < 15 at some point
        // This is diagnostic, so we don't fail
    }
}

TEST_CASE("VIA Port A DDR configuration", "[sound][diagnostic][mos]") {
    Mode7TestContext<ModelB> ctx;
    REQUIRE(ctx.booted);

    // The MOS needs to set Port A DDR to output ($FF) before writing to sound chip
    uint8_t ddra = ctx.machine.read(VIA_DDRA);
    uint8_t ddrb = ctx.machine.read(VIA_DDRB);

    std::cout << "\n=== VIA Data Direction Registers ===\n";
    std::cout << "DDRA ($FE43): 0x" << std::hex << (int)ddra << std::dec;
    if (ddra == 0xFF) std::cout << " (all outputs)";
    else if (ddra == 0x00) std::cout << " (all inputs)";
    else std::cout << " (mixed)";
    std::cout << "\n";

    std::cout << "DDRB ($FE42): 0x" << std::hex << (int)ddrb << std::dec;
    if (ddrb == 0xFF) std::cout << " (all outputs)";
    else if (ddrb == 0x00) std::cout << " (all inputs)";
    else std::cout << " (mixed)";
    std::cout << "\n";

    // DDRA should be set for keyboard scanning initially, but MOS will
    // temporarily set it to $FF when writing to sound chip
    // DDRB lower bits should be outputs for addressable latch control
    INFO("DDRB lower nibble should be outputs for addressable latch");
    REQUIRE((ddrb & 0x0F) == 0x0F);
}

#endif
