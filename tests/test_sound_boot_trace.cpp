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

// Trace sound chip writes during boot to understand MOS initialization

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>

#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>

using namespace beebium;

#ifdef BEEBIUM_ROM_DIR

// Simple tracer for sound chip writes
struct SoundWriteTracer {
    struct Write {
        uint64_t cycle;
        uint8_t data;
        bool latch_enabled;
    };
    std::vector<Write> writes;

    void record(uint64_t cycle, uint8_t data, bool latch_enabled) {
        writes.push_back({cycle, data, latch_enabled});
    }

    void dump() {
        std::cout << "\n=== Sound chip writes during boot ===\n";
        std::cout << "Total writes to VIA Port A: " << writes.size() << "\n";

        size_t enabled_writes = 0;
        for (const auto& w : writes) {
            if (w.latch_enabled) {
                enabled_writes++;
                std::cout << "Cycle " << w.cycle << ": 0x" << std::hex << (int)w.data << std::dec;

                // Decode if latch byte
                if (w.data & 0x80) {
                    uint8_t channel = (w.data >> 5) & 0x03;
                    uint8_t type = (w.data >> 4) & 0x01;
                    uint8_t data_low = w.data & 0x0F;
                    std::cout << " [LATCH: ch=" << (int)channel << " "
                              << (type ? "VOL" : "FREQ") << " data=" << (int)data_low << "]";
                } else {
                    std::cout << " [DATA: " << (int)(w.data & 0x3F) << "]";
                }
                std::cout << "\n";
            }
        }
        std::cout << "Enabled writes: " << enabled_writes << " / " << writes.size() << "\n";
    }
};

TEST_CASE("Sound chip writes during boot", "[sound][boot][trace]") {
    // Load ROMs
    auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom_path = rom_dirpath / "acorn-mos_1_20.rom";
    auto basic_rom_path = rom_dirpath / "bbc-basic_2.rom";

    std::ifstream mos_file(mos_rom_path, std::ios::binary);
    std::ifstream basic_file(basic_rom_path, std::ios::binary);
    REQUIRE(mos_file);
    REQUIRE(basic_file);

    mos_file.seekg(0, std::ios::end);
    auto mos_size = mos_file.tellg();
    mos_file.seekg(0);

    basic_file.seekg(0, std::ios::end);
    auto basic_size = basic_file.tellg();
    basic_file.seekg(0);

    std::vector<uint8_t> mos_rom(mos_size);
    std::vector<uint8_t> basic_rom(basic_size);

    mos_file.read(reinterpret_cast<char*>(mos_rom.data()), mos_size);
    basic_file.read(reinterpret_cast<char*>(basic_rom.data()), basic_size);

    // Create machine
    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().enable_audio_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    SoundWriteTracer tracer;

    std::cout << "\n=== Starting boot sequence ===\n";
    machine.reset();

    // Boot until MODE 7 detected
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        // Before step: check if we're about to write to VIA Port A
        // We can't hook directly, so we'll check state changes

        auto old_tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        auto old_tone1 = machine.memory().sound_chip.get_tone_channel_state(1);
        auto old_tone2 = machine.memory().sound_chip.get_tone_channel_state(2);
        auto old_noise = machine.memory().sound_chip.get_noise_channel_state();
        bool old_latch_enabled = machine.memory().addressable_latch.sound_write_enabled();

        machine.step();

        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check if chip state changed (indicates a write happened)
        auto new_tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        auto new_tone1 = machine.memory().sound_chip.get_tone_channel_state(1);
        auto new_tone2 = machine.memory().sound_chip.get_tone_channel_state(2);
        auto new_noise = machine.memory().sound_chip.get_noise_channel_state();

        bool changed = (new_tone0.frequency != old_tone0.frequency ||
                       new_tone0.volume != old_tone0.volume ||
                       new_tone1.frequency != old_tone1.frequency ||
                       new_tone1.volume != old_tone1.volume ||
                       new_tone2.frequency != old_tone2.frequency ||
                       new_tone2.volume != old_tone2.volume ||
                       new_noise.rate_select != old_noise.rate_select ||
                       new_noise.white_mode != old_noise.white_mode ||
                       new_noise.volume != old_noise.volume);

        if (changed && old_latch_enabled) {
            std::cout << "Cycle " << i << ": Sound chip state changed!\n";
            std::cout << "  Tone0: freq " << old_tone0.frequency << "->" << new_tone0.frequency
                      << " vol " << (int)old_tone0.volume << "->" << (int)new_tone0.volume << "\n";
            std::cout << "  Tone1: freq " << old_tone1.frequency << "->" << new_tone1.frequency
                      << " vol " << (int)old_tone1.volume << "->" << (int)new_tone1.volume << "\n";
        }

        // Check boot completion
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 7) {
                boot_complete = true;
                std::cout << "Boot complete at cycle " << i << "\n";
            }
        }
    }

    // Final chip state
    auto final_tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
    auto final_tone1 = machine.memory().sound_chip.get_tone_channel_state(1);
    auto final_tone2 = machine.memory().sound_chip.get_tone_channel_state(2);
    auto final_noise = machine.memory().sound_chip.get_noise_channel_state();

    std::cout << "\n=== Final chip state after boot ===\n";
    std::cout << "Tone 0: freq=" << final_tone0.frequency << " vol=" << (int)final_tone0.volume << "\n";
    std::cout << "Tone 1: freq=" << final_tone1.frequency << " vol=" << (int)final_tone1.volume << "\n";
    std::cout << "Tone 2: freq=" << final_tone2.frequency << " vol=" << (int)final_tone2.volume << "\n";
    std::cout << "Noise: rate=" << (int)final_noise.rate_select << " white=" << final_noise.white_mode
              << " vol=" << (int)final_noise.volume << "\n";
    std::cout << "Latch: 0x" << std::hex << (int)machine.memory().addressable_latch.value << std::dec << "\n";

    REQUIRE(boot_complete);
}

#endif
