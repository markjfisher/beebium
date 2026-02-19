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

// test_boot_econet.cpp
// Integration tests for BBC Model B boot with Econet hardware fitted.
// Verifies that the MOS + NFS ROM produce correct startup messages
// and that the machine doesn't hang during boot.

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/Mc6854.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_keyboard_helpers.hpp"

using namespace beebium;

namespace {

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool base_roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom");
}

bool nfs_rom_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-nfs_3_34.rom");
}

// Search Mode 7 screen memory ($7C00-$7FFF) for a string.
bool screen_contains(ModelB& machine, const std::string& text) {
    for (uint16_t addr = 0x7C00; addr <= 0x7FFF - text.size(); ++addr) {
        bool match = true;
        for (size_t i = 0; i < text.size(); ++i) {
            if (machine.state().memory.read(addr + static_cast<uint16_t>(i)) !=
                static_cast<uint8_t>(text[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Dump first N rows of Mode 7 screen memory for debugging.
std::string dump_screen(ModelB& machine, int rows = 6) {
    std::string result;
    for (int row = 0; row < rows; ++row) {
        result += "Row " + std::to_string(row) + ": [";
        for (int col = 0; col < 40; ++col) {
            uint8_t ch = machine.state().memory.read(
                0x7C00 + static_cast<uint16_t>(row * 40 + col));
            if (ch >= 0x20 && ch < 0x7F) {
                result += static_cast<char>(ch);
            } else {
                result += '.';
            }
        }
        result += "]\n";
    }
    return result;
}

// Set up a Model B with MOS + BASIC + optional NFS ROM in slot 10 + Econet socket.
void setup_econet_machine(ModelB& machine, uint8_t station_id, bool connected,
                          bool load_nfs = true) {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    if (load_nfs) {
        auto nfs = load_rom(rom_dirpath / "acorn-nfs_3_34.rom");
        machine.memory().load_sideways_rom(10, nfs.data(), nfs.size());
    }

    // Create backend and enable Econet socket
    auto backend = std::make_unique<TestBackend>();
    backend->set_connected(connected);
    machine.state().memory.econet_socket.enable(station_id, std::move(backend), true);
}

// Boot the machine for a fixed number of instructions.
void boot_machine(ModelB& machine, uint64_t instructions = 3'500'000) {
    machine.reset();
    for (uint64_t i = 0; i < instructions; ++i) {
        machine.step_instruction();
    }
}

} // namespace

// =============================================================================
// Econet Boot Tests
// =============================================================================

TEST_CASE("Model B with Econet (clock present) boots to BASIC prompt",
          "[boot][econet]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    ModelB machine;
    setup_econet_machine(machine, 1, true);  // station 1, connected
    boot_machine(machine);

    INFO("Screen:\n" << dump_screen(machine));

    // Should be in Mode 7
    REQUIRE(machine.state().memory.read(0x0355) == 7);

    // Boot message present
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "BASIC"));
}

TEST_CASE("Boot message includes station number when Econet connected",
          "[boot][econet]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    ModelB machine;
    setup_econet_machine(machine, 1, true);  // station 1, connected
    boot_machine(machine);

    INFO("Screen:\n" << dump_screen(machine));

    REQUIRE(screen_contains(machine, "Econet Station"));
    REQUIRE(screen_contains(machine, "001"));
}

TEST_CASE("Model B with Econet (no network) boots to BASIC prompt with No Clock",
          "[boot][econet]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    ModelB machine;
    setup_econet_machine(machine, 1, false);  // station 1, disconnected
    boot_machine(machine);

    INFO("Screen:\n" << dump_screen(machine));

    // Should be in Mode 7
    REQUIRE(machine.state().memory.read(0x0355) == 7);

    // Boot message present
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "BASIC"));

    // Econet station shown with No Clock
    REQUIRE(screen_contains(machine, "Econet Station"));
    REQUIRE(screen_contains(machine, "No Clock"));
}

TEST_CASE("Econet boot without NFS ROM shows no Econet message",
          "[boot][econet]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");

    ModelB machine;
    setup_econet_machine(machine, 1, false, false);  // station 1, no NFS ROM
    boot_machine(machine);

    INFO("Screen:\n" << dump_screen(machine));

    // Should be in Mode 7
    REQUIRE(machine.state().memory.read(0x0355) == 7);

    // Standard boot message present
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "BASIC"));

    // No Econet message (NFS ROM not loaded)
    REQUIRE_FALSE(screen_contains(machine, "Econet Station"));
}

// =============================================================================
// NFS command timeout test — exercises the NFS ROM's full TX/RX path through
// the ADLC emulation without needing an external file server.
//
// With the TestBackend connected but no file server responding, the NFS ROM
// should complete the four-way handshake (using FourWayHandshake's fake acks),
// wait for a reply that never comes, and eventually produce an error message
// like "No reply", "Not listening", or "Net error" on screen.
//
// If this test hangs (exceeds the cycle budget without producing output),
// it means the NFS ROM is stuck in a loop — likely due to the ADLC not
// delivering frames correctly or NMI gating issues.
// =============================================================================

TEST_CASE("NFS *. with no server triggers network activity",
          "[boot][econet][nfs]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    using namespace beebium::test;

    ModelB machine;
    // Manual setup (not setup_econet_machine) to keep a raw pointer to the backend
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    auto nfs = load_rom(rom_dirpath / "acorn-nfs_3_34.rom");
    machine.memory().load_sideways_rom(10, nfs.data(), nfs.size());

    auto backend_ptr = std::make_unique<TestBackend>();
    backend_ptr->set_connected(true);
    auto* backend = backend_ptr.get();
    machine.state().memory.econet_socket.enable(101, std::move(backend_ptr), true);

    machine.memory().enable_video_output();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot the machine (using step() for video processing)
    machine.reset();
    for (uint64_t i = 0; i < 8'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    INFO("Screen after boot:\n" << dump_screen(machine));
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "Econet Station"));

    // Type *NET to select NFS as the active filing system
    type_string_with_shift(machine, renderer, "*NET\r", 100000);

    // Give time for *NET to complete
    for (uint64_t i = 0; i < 2'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    INFO("Screen after *NET:\n" << dump_screen(machine, 12));

    // Record sent frame count before *.
    size_t frames_before = backend->sent_frame_count();

    // Type *. and RETURN
    type_string_with_shift(machine, renderer, "*.\r", 100000);

    // Run for a short time — enough for the NFS ROM to attempt at least
    // one TX cycle (scout + data). With no server responding, the NFS ROM
    // retries indefinitely (the fake scout/final acks always succeed in
    // AUN mode, so the retry counter never decrements).
    // 2M cycles (~1ms BBC time) is enough for several retry attempts.
    for (uint64_t i = 0; i < 2'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    size_t frames_after = backend->sent_frame_count();

    INFO("Screen after *. command:\n" << dump_screen(machine, 12));
    INFO("Frames sent: before=" << frames_before << " after=" << frames_after);

    // Verify the NFS ROM attempted network activity (sent at least one
    // AUN Unicast to the file server). This confirms the full TX path:
    // INACTIVE poll → scout → fake scout ack → data → AUN Unicast.
    REQUIRE(frames_after > frames_before);

    // With a TestBackend (no real server), the NFS ROM retries forever
    // because the scout and final ack phases always succeed in AUN mode.
    // For "Who are you?" output, a responding file server is needed.
    // See the separate file server integration tests for that.
}
