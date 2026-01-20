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

// test_cursor_blink_timing.cpp
//
// Integration test for cursor blink timing in bitmap modes.
//
// This test verifies that the cursor blinks at the correct rate by:
// 1. Booting to BASIC in MODE 7
// 2. Switching to a bitmap mode (e.g., MODE 4)
// 3. Capturing frames and detecting cursor on/off state by comparing successive frames
// 4. Counting the number of frames in each on/off period
// 5. Asserting that the periods match expected values
//
// The cursor blink rate is controlled by CRTC R10 bits 5-6:
// - Mode 0: Steady cursor (no blink)
// - Mode 1: No cursor
// - Mode 2: Blink at 1/16 field rate (cursor toggles every 8 frames at 50fps)
// - Mode 3: Blink at 1/32 field rate (cursor toggles every 16 frames at 50fps)
//
// The MOS uses cursor mode 3 by default, so we expect 16 frames on, 16 frames off.
//
// This test should:
// - PASS on commit 4bdb18ba (cursor blinks at correct rate)
// - FAIL on commit 4fcfed60 and later (cursor blinks too fast)

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <numeric>

using namespace beebium;

namespace {

// Load a ROM file into a vector
std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open ROM: " + filepath.string());
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    return data;
}

#ifdef BEEBIUM_ROM_DIR
bool roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom");
}
#endif

// BBC keyboard matrix: ASCII to (row, column) mapping
uint8_t ascii_to_keycode(uint8_t ascii) {
    switch (ascii) {
        case '\r': return 0x49;  // RETURN
        case ' ':  return 0x62;  // SPACE
        case '0':  return 0x27;
        case '1':  return 0x30;
        case '2':  return 0x31;
        case '3':  return 0x11;
        case '4':  return 0x12;
        case '5':  return 0x13;
        case '6':  return 0x34;
        case '7':  return 0x24;
        case '8':  return 0x15;
        case '9':  return 0x26;
        case 'A': case 'a': return 0x41;
        case 'B': case 'b': return 0x64;
        case 'C': case 'c': return 0x52;
        case 'D': case 'd': return 0x32;
        case 'E': case 'e': return 0x22;
        case 'F': case 'f': return 0x43;
        case 'G': case 'g': return 0x53;
        case 'H': case 'h': return 0x54;
        case 'I': case 'i': return 0x25;
        case 'J': case 'j': return 0x45;
        case 'K': case 'k': return 0x46;
        case 'L': case 'l': return 0x56;
        case 'M': case 'm': return 0x65;
        case 'N': case 'n': return 0x55;
        case 'O': case 'o': return 0x36;
        case 'P': case 'p': return 0x37;
        case 'Q': case 'q': return 0x10;
        case 'R': case 'r': return 0x33;
        case 'S': case 's': return 0x51;
        case 'T': case 't': return 0x23;
        case 'U': case 'u': return 0x35;
        case 'V': case 'v': return 0x63;
        case 'W': case 'w': return 0x21;
        case 'X': case 'x': return 0x42;
        case 'Y': case 'y': return 0x44;
        case 'Z': case 'z': return 0x61;
        default: return 0xFF;
    }
}

void press_key(ModelB& machine, uint8_t ascii_code) {
    uint8_t keycode = ascii_to_keycode(ascii_code);
    if (keycode != 0xFF) {
        uint8_t row = (keycode >> 4) & 0x0F;
        uint8_t col = keycode & 0x0F;
        machine.state().memory.system_via_peripheral.key_down(row, col);
    }
}

void release_key(ModelB& machine, uint8_t ascii_code) {
    uint8_t keycode = ascii_to_keycode(ascii_code);
    if (keycode != 0xFF) {
        uint8_t row = (keycode >> 4) & 0x0F;
        uint8_t col = keycode & 0x0F;
        machine.state().memory.system_via_peripheral.key_up(row, col);
    }
}

// Type a string by injecting keypresses
// Note: 200000 cycles per key matches test_keyboard_input.cpp timing
void type_string(ModelB& machine, FrameRenderer& renderer, const char* str, int cycles_per_key = 200000) {
    for (const char* p = str; *p; ++p) {
        uint8_t ch = static_cast<uint8_t>(*p);
        press_key(machine, ch);
        for (int i = 0; i < cycles_per_key / 2; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
        release_key(machine, ch);
        for (int i = 0; i < cycles_per_key / 2; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }
}

// Run cycles while draining video
void run_cycles(ModelB& machine, FrameRenderer& renderer, uint64_t cycles) {
    for (uint64_t i = 0; i < cycles; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
}

// Wait for next frame boundary
void wait_for_frame(ModelB& machine, FrameRenderer& renderer, FrameBuffer& fb) {
    uint64_t start_version = fb.version();
    constexpr uint64_t MAX_CYCLES = 200000;
    uint64_t cycles = 0;
    while (fb.version() == start_version && cycles < MAX_CYCLES) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        ++cycles;
    }
}

// Compute a simple hash of the frame to detect changes
// We use a sum of pixel values - simple but effective for detecting cursor blink
uint64_t frame_hash(const std::span<const uint32_t> frame) {
    uint64_t hash = 0;
    for (auto pixel : frame) {
        hash += pixel;
    }
    return hash;
}

// Detect cursor state by comparing current frame to previous frame
// Returns true if frame changed (cursor toggled), false if same
bool frame_changed(uint64_t prev_hash, uint64_t curr_hash, uint64_t threshold = 1000) {
    uint64_t diff = (curr_hash > prev_hash) ? (curr_hash - prev_hash) : (prev_hash - curr_hash);
    return diff > threshold;
}

// Structure to hold blink timing results
struct BlinkTimingResult {
    std::vector<int> on_periods;   // Number of frames in each ON period
    std::vector<int> off_periods;  // Number of frames in each OFF period
    int total_frames;
    int total_transitions;
};

// Measure cursor blink timing by capturing frames and detecting state changes
BlinkTimingResult measure_blink_timing(ModelB& machine, FrameRenderer& renderer, FrameBuffer& fb, int num_frames) {
    BlinkTimingResult result;
    result.total_frames = num_frames;
    result.total_transitions = 0;

    // Capture initial frame
    wait_for_frame(machine, renderer, fb);
    auto frame = fb.read_frame();
    uint64_t prev_hash = frame_hash(frame);

    // Track state: we don't know initially if cursor is on or off
    // We'll detect transitions and measure period lengths
    int current_period_length = 1;
    bool last_transition_was_increase = false;  // Track direction of first transition

    std::vector<int> period_lengths;

    for (int i = 1; i < num_frames; ++i) {
        wait_for_frame(machine, renderer, fb);
        frame = fb.read_frame();
        uint64_t curr_hash = frame_hash(frame);

        bool changed = frame_changed(prev_hash, curr_hash);

        if (changed) {
            // Transition detected
            period_lengths.push_back(current_period_length);
            current_period_length = 1;
            result.total_transitions++;

            // Track direction for classifying periods
            if (result.total_transitions == 1) {
                last_transition_was_increase = (curr_hash > prev_hash);
            }
        } else {
            current_period_length++;
        }

        prev_hash = curr_hash;
    }

    // Don't include final incomplete period in measurements

    // Classify periods as on/off based on alternation
    // The first complete period after first transition is either on or off
    // We'll alternate from there
    bool is_on = last_transition_was_increase;  // Increasing hash = cursor appeared = ON
    for (size_t i = 0; i < period_lengths.size(); ++i) {
        if (is_on) {
            result.on_periods.push_back(period_lengths[i]);
        } else {
            result.off_periods.push_back(period_lengths[i]);
        }
        is_on = !is_on;
    }

    return result;
}

} // anonymous namespace

#ifdef BEEBIUM_ROM_DIR

TEST_CASE("Cursor blink timing in MODE 4", "[cursor][blink][timing][integration]") {
    REQUIRE(roms_available());

    // Load ROMs
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    // Create machine
    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Create video pipeline
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);
    fb.clear(0);

    // Boot to BASIC prompt
    INFO("Booting to BASIC...");
    run_cycles(machine, renderer, 2'500'000);

    // Type "MODE 4" and press RETURN
    INFO("Switching to MODE 4...");
    type_string(machine, renderer, "MODE 4\r");

    // Wait for mode switch to complete (give it plenty of time to redraw)
    // A mode switch clears and redraws the screen which takes time
    run_cycles(machine, renderer, 2'000'000);

    // Check the ULA control register to see what mode we're in
    uint8_t ula_control = machine.memory().video_ula.control();
    INFO("VideoULA control register = 0x" << std::hex << (int)ula_control);
    INFO("Teletext mode bit = " << machine.memory().video_ula.teletext_mode());

    // Verify we're in a bitmap mode (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Read CRTC R10 to verify cursor blink mode
    machine.memory().crtc.write(0, 10);  // Select R10
    uint8_t r10 = machine.memory().crtc.read(1);
    uint8_t cursor_mode = (r10 >> 5) & 0x03;
    INFO("CRTC R10 = 0x" << std::hex << (int)r10 << ", cursor mode = " << (int)cursor_mode);

    // MOS uses cursor mode 2 (blink at 1/16 field rate) by default
    // Mode 2: cursor toggles when bit 3 of field_count changes
    // At 50fps with field_count incrementing by 1 per frame:
    // - Bit 3 toggles every 8 frames
    // - Expected: 8 frames on, 8 frames off

    // Capture enough frames to see multiple complete blink cycles
    // 64 frames = 4 complete cycles at 8+8=16 frames per cycle
    constexpr int NUM_FRAMES = 80;

    INFO("Measuring cursor blink timing over " << NUM_FRAMES << " frames...");
    auto result = measure_blink_timing(machine, renderer, fb, NUM_FRAMES);

    INFO("Total transitions: " << result.total_transitions);
    INFO("ON periods: " << result.on_periods.size());
    INFO("OFF periods: " << result.off_periods.size());

    // Report individual period lengths
    for (size_t i = 0; i < result.on_periods.size(); ++i) {
        INFO("ON period " << i << ": " << result.on_periods[i] << " frames");
    }
    for (size_t i = 0; i < result.off_periods.size(); ++i) {
        INFO("OFF period " << i << ": " << result.off_periods[i] << " frames");
    }

    // We should have at least 2 complete ON periods and 2 complete OFF periods
    REQUIRE(result.on_periods.size() >= 2);
    REQUIRE(result.off_periods.size() >= 2);

    // Calculate average period lengths (excluding first and last which may be partial)
    auto calc_average = [](const std::vector<int>& periods) -> double {
        if (periods.size() < 2) return 0;
        // Skip first period (may be partial), average the rest
        int sum = std::accumulate(periods.begin() + 1, periods.end(), 0);
        return static_cast<double>(sum) / (periods.size() - 1);
    };

    double avg_on = calc_average(result.on_periods);
    double avg_off = calc_average(result.off_periods);

    INFO("Average ON period: " << avg_on << " frames");
    INFO("Average OFF period: " << avg_off << " frames");

    // The MOS uses cursor mode 3 (1/32 field rate) by default.
    // In this mode, cursor toggles when bit 4 of field_count changes.
    // Expected: 16 frames on, 16 frames off (±2 for measurement noise)
    //
    // If the bug is present (field_count += 2 in non-interlace):
    // Actual: 8 frames on, 8 frames off (cursor blinks twice as fast)
    //
    // We use a tolerance of ±2 frames for measurement noise

    constexpr double EXPECTED_PERIOD = 16.0;
    constexpr double TOLERANCE = 2.0;

    CHECK(avg_on >= EXPECTED_PERIOD - TOLERANCE);
    CHECK(avg_on <= EXPECTED_PERIOD + TOLERANCE);
    CHECK(avg_off >= EXPECTED_PERIOD - TOLERANCE);
    CHECK(avg_off <= EXPECTED_PERIOD + TOLERANCE);

    // Also check that ON and OFF periods are roughly equal
    double period_ratio = (avg_on > avg_off) ? (avg_on / avg_off) : (avg_off / avg_on);
    INFO("Period ratio (should be ~1.0): " << period_ratio);
    CHECK(period_ratio <= 1.5);  // Should be close to 1:1
}

#endif // BEEBIUM_ROM_DIR
