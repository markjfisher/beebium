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

// test_server_main.cpp
// Tests for server_main helper functions (parse_arguments, validate_config, etc.)

#include <beebium/server/ServerMain.hpp>
#include <beebium/Machines.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace beebium::server;
using MachineType = beebium::ModelB;

// Helper to convert string vector to argc/argv format
struct ArgvHelper {
    std::vector<std::string> args;
    std::vector<char*> argv;

    explicit ArgvHelper(std::initializer_list<const char*> init) {
        for (const char* arg : init) {
            args.emplace_back(arg);
        }
        for (auto& s : args) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);
    }

    int argc() const { return static_cast<int>(args.size()); }
    char** data() { return argv.data(); }
};

// ============================================================================
// parse_arguments() tests
// ============================================================================

TEST_CASE("parse_arguments: --help returns exit code 0", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--help"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 0);
}

TEST_CASE("parse_arguments: -h returns exit code 0", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "-h"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 0);
}

TEST_CASE("parse_arguments: --info is now unknown (use describe-machine subcommand)", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--info"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    // --info was replaced by describe-machine subcommand
    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

TEST_CASE("parse_arguments: --list-fdc is now unknown (use list-fdcs subcommand)", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--list-fdc"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    // --list-fdc was replaced by list-fdcs subcommand
    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

TEST_CASE("parse_arguments: unknown argument returns exit code 1", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--unknown-option"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

TEST_CASE("parse_arguments: no arguments returns nullopt (continue)", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parse_arguments: --mos sets mos_filepath", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--mos", "custom-mos.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.mos_filepath == "custom-mos.rom");
}

TEST_CASE("parse_arguments: --rom-dir sets rom_dirpath", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--rom-dir", "/path/to/roms"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.rom_dirpath == "/path/to/roms");
}

TEST_CASE("parse_arguments: --port sets port", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--port", "12345"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.port == 12345);
}

TEST_CASE("parse_arguments: --sideways rom populates sideways_configs and rom_slots", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--sideways", "15:rom:test.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 15);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Rom);
    REQUIRE(config.sideways_configs[0].image_filepath == "test.rom");
    REQUIRE(config.rom_slots.count(15) == 1);
    REQUIRE(config.rom_slots[15] == "test.rom");
}

TEST_CASE("parse_arguments: --sideways empty populates config", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--sideways", "11:empty"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 11);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_arguments: --sideways ram populates config", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--sideways", "4:ram"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 4);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Ram);
}

TEST_CASE("parse_arguments: --floppy sets floppy_filepaths", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--floppy", "0:game.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0] == "game.ssd");
    REQUIRE(config.floppy_filepaths[1].empty());
}

TEST_CASE("parse_arguments: --floppy for drive 1", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--floppy", "1:backup.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0].empty());
    REQUIRE(config.floppy_filepaths[1] == "backup.ssd");
}

// Tests for colon completion (shell tab completion support)

TEST_CASE("parse_arguments: --floppy with split filepath (colon completion)", "[server_main][parse_arguments]") {
    // Simulates: --floppy 0: game.ssd (space after colon from tab completion)
    ArgvHelper args{"beebium", "--floppy", "0:", "game.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0] == "game.ssd");
}

TEST_CASE("parse_arguments: --sideways with split filepath (colon completion)", "[server_main][parse_arguments]") {
    // Simulates: --sideways 15:rom: forth.rom (space after colon from tab completion)
    ArgvHelper args{"beebium", "--sideways", "15:rom:", "forth.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 15);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Rom);
    REQUIRE(config.sideways_configs[0].image_filepath == "forth.rom");
}

TEST_CASE("parse_arguments: --floppy colon completion doesn't consume options", "[server_main][parse_arguments]") {
    // --floppy 0: followed by --port should NOT consume --port
    // This results in an error because "0:" has no filepath
    ArgvHelper args{"beebium", "--floppy", "0:", "--port", "8080"};
    ServerConfig<MachineType> config;

    // Should throw because "0:" has no filepath and next arg starts with -
    REQUIRE_THROWS_WITH(
        parse_arguments<MachineType>(args.argc(), args.data(), config),
        Catch::Matchers::ContainsSubstring("filepath required")
    );
}

TEST_CASE("parse_arguments: --sideways colon completion with following option", "[server_main][parse_arguments]") {
    // --sideways 15:empty followed by --port should work (empty doesn't need completion)
    ArgvHelper args{"beebium", "--sideways", "15:empty", "--port", "8080"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Empty);
    REQUIRE(config.port == 8080);
}

TEST_CASE("parse_arguments: --screen-mode sets screen_mode", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--screen-mode", "4"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.screen_mode == 4);
    REQUIRE(config.screen_mode_set == true);
}

TEST_CASE("parse_arguments: --screen-mode out of range returns error", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--screen-mode", "8"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

TEST_CASE("parse_arguments: --auto-boot sets auto_boot", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--auto-boot"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.auto_boot == true);
    REQUIRE(config.auto_boot_set == true);
}

TEST_CASE("parse_arguments: --links sets raw_links", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--links", "128"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.raw_links == 128);
}

TEST_CASE("parse_arguments: --links out of range returns error", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--links", "256"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

TEST_CASE("parse_arguments: --wait=cli sets wait_mode", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--wait=cli"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.wait_mode == WaitMode::Cli);
}

TEST_CASE("parse_arguments: --wait=api sets wait_mode", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--wait=api"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.wait_mode == WaitMode::Api);
}

TEST_CASE("parse_arguments: --fdc sets fdc_type", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--fdc", "acorn-1770"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type == "acorn-1770");
}

TEST_CASE("parse_arguments: --rom is now unknown argument", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--rom", "15:forth.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    // --rom was removed; should be treated as unknown argument
    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

TEST_CASE("parse_arguments: multiple options combined", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--mos", "mos.rom", "--port", "8000", "--screen-mode", "0"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.mos_filepath == "mos.rom");
    REQUIRE(config.port == 8000);
    REQUIRE(config.screen_mode == 0);
    REQUIRE(config.screen_mode_set == true);
}

// ============================================================================
// validate_config() tests
// ============================================================================

TEST_CASE("validate_config: valid empty config returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

TEST_CASE("validate_config: --links with --screen-mode returns error", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.raw_links = 128;
    config.screen_mode_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE(error.has_value());
    REQUIRE(error->find("--links") != std::string::npos);
    REQUIRE(error->find("--screen-mode") != std::string::npos);
}

TEST_CASE("validate_config: --links with --auto-boot returns error", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.raw_links = 128;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE(error.has_value());
    REQUIRE(error->find("--links") != std::string::npos);
    REQUIRE(error->find("--auto-boot") != std::string::npos);
}

TEST_CASE("validate_config: --links with both --screen-mode and --auto-boot returns error", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.raw_links = 128;
    config.screen_mode_set = true;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE(error.has_value());
}

TEST_CASE("validate_config: --screen-mode without --links returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.screen_mode = 4;
    config.screen_mode_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

TEST_CASE("validate_config: --auto-boot without --links returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.auto_boot = true;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

TEST_CASE("validate_config: both --screen-mode and --auto-boot without --links returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.screen_mode = 4;
    config.screen_mode_set = true;
    config.auto_boot = true;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

// ============================================================================
// load_roms() tests
// ============================================================================

#ifdef BEEBIUM_ROM_DIR
// Helper to set up config for testing
// For machines with a default DFS (like Model B+), this would clear the DFS slot
// to avoid loading a ROM that might not exist in the test environment.
// Model B has no default DFS since it has no FDC by default.
void setup_test_config(ServerConfig<MachineType>& /*config*/) {
    // Model B has no default DFS ROM, so nothing to clear
}

TEST_CASE("load_roms: applies default MOS ROM when not specified", "[server_main][load_roms]") {
    using Memory = MachineType::Memory;
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    // config.mos_filepath is empty, should use default

    load_roms(machine, config);

    REQUIRE(config.mos_filepath == Memory::DEFAULT_MOS_ROM);
}

TEST_CASE("load_roms: uses specified MOS ROM filepath", "[server_main][load_roms]") {
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    config.mos_filepath = "acorn-mos_1_20.rom";  // Explicit MOS

    load_roms(machine, config);

    REQUIRE(config.mos_filepath == "acorn-mos_1_20.rom");
}

TEST_CASE("load_roms: applies default language ROM when slot not overridden", "[server_main][load_roms]") {
    using Memory = MachineType::Memory;
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);

    load_roms(machine, config);

    REQUIRE(config.rom_slots.count(Memory::DEFAULT_LANGUAGE_SLOT) == 1);
    REQUIRE(config.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] == Memory::DEFAULT_LANGUAGE_ROM);
}

TEST_CASE("load_roms: does not override user-specified language slot", "[server_main][load_roms]") {
    using Memory = MachineType::Memory;
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    config.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] = "acorn-mos_1_20.rom";  // Use MOS as test ROM

    load_roms(machine, config);

    // Should not have been overwritten with default
    REQUIRE(config.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] == "acorn-mos_1_20.rom");
}

TEST_CASE("load_roms: handles empty slot marker", "[server_main][load_roms]") {
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    config.rom_slots[10] = EMPTY_SLOT_MARKER;

    // Should not throw
    REQUIRE_NOTHROW(load_roms(machine, config));
}

TEST_CASE("load_roms: loads MOS ROM into machine memory", "[server_main][load_roms]") {
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);

    // Should not throw
    REQUIRE_NOTHROW(load_roms(machine, config));

    // After load_roms, can read from the MOS ROM area (0xC000-0xFFFF)
    // The reset vector at 0xFFFC/0xFFFD should contain the entry point
    // (MOS 1.20 reset vector is $D9CD)
    uint8_t reset_low = machine.state().memory.read(0xFFFC);
    uint8_t reset_high = machine.state().memory.read(0xFFFD);
    uint16_t reset_vector = reset_low | (reset_high << 8);

    // MOS 1.20 reset vector should be in the range 0xC000-0xFFFF
    REQUIRE(reset_vector >= 0xC000);
}

// ============================================================================
// install_disc_controller() tests
// ============================================================================

TEST_CASE("install_disc_controller: returns nullopt when no FDC specified", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    // config.fdc_type is empty

    auto result = install_disc_controller(machine, config);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("install_disc_controller: returns nullopt for 'none' type", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.fdc_type = "none";

    auto result = install_disc_controller(machine, config);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("install_disc_controller: installs valid FDC type", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.fdc_type = "acorn-1770";

    auto result = install_disc_controller(machine, config);

    REQUIRE_FALSE(result.has_value());
    // Machine should now have a disc controller installed
    REQUIRE(machine.state().memory.has_disc_controller());
}

TEST_CASE("install_disc_controller: returns error for unknown FDC type", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.fdc_type = "unknown-controller";

    auto result = install_disc_controller(machine, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

// ============================================================================
// load_disc_images() tests
// ============================================================================

TEST_CASE("load_disc_images: handles empty floppy paths (no-op)", "[server_main][load_disc_images]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    // config.floppy_filepaths are empty

    // Should not throw
    REQUIRE_NOTHROW(load_disc_images(machine, config));
}

// ============================================================================
// apply_startup_options() tests
// ============================================================================

TEST_CASE("apply_startup_options: does nothing when no options set", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    // All startup options are at default values

    // Should not throw
    REQUIRE_NOTHROW(apply_startup_options(machine, config));
}

TEST_CASE("apply_startup_options: sets raw links when specified", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.raw_links = 0xAB;

    apply_startup_options(machine, config);

    REQUIRE(machine.state().memory.startup_options() == 0xAB);
}

TEST_CASE("apply_startup_options: sets screen mode when specified", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.screen_mode = 4;
    config.screen_mode_set = true;

    apply_startup_options(machine, config);

    // Screen mode is encoded in bits 0-2 of startup options
    uint8_t startup = machine.state().memory.startup_options();
    REQUIRE((startup & 0x07) == 4);
}

TEST_CASE("apply_startup_options: sets auto-boot when specified", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.auto_boot = true;
    config.auto_boot_set = true;

    // Should not throw
    REQUIRE_NOTHROW(apply_startup_options(machine, config));
}
#endif

// ============================================================================
// Signal handler tests
// ============================================================================

#include <beebium/server/Platform.hpp>
#include <csignal>

TEST_CASE("signal handler: sets atomic flag without calling callback directly",
          "[server_main][signal]") {
    // Reset state
    beebium::server::platform::detail::g_signal_received.store(false);

    bool callback_called = false;
    beebium::server::platform::install_shutdown_handler([&callback_called] {
        callback_called = true;
    });

    // Simulate what the kernel does: call the signal handler function
    beebium::server::platform::detail::signal_handler(SIGTERM);

    // The handler should have set the atomic flag but NOT called the callback
    REQUIRE(beebium::server::platform::detail::g_signal_received.load());
    REQUIRE_FALSE(callback_called);

    // Now dispatch the pending signal (as the main loop would)
    beebium::server::platform::dispatch_pending_signal();

    // Now the callback should have been called
    REQUIRE(callback_called);
    // And the flag should be cleared
    REQUIRE_FALSE(beebium::server::platform::detail::g_signal_received.load());

    // Clean up
    beebium::server::platform::remove_shutdown_handler();
}

TEST_CASE("signal handler: dispatch_pending_signal is a no-op when no signal received",
          "[server_main][signal]") {
    beebium::server::platform::detail::g_signal_received.store(false);

    bool callback_called = false;
    beebium::server::platform::install_shutdown_handler([&callback_called] {
        callback_called = true;
    });

    // Dispatch without any signal having been received
    beebium::server::platform::dispatch_pending_signal();

    REQUIRE_FALSE(callback_called);

    beebium::server::platform::remove_shutdown_handler();
}

TEST_CASE("signal handler: invoke_shutdown sets g_running false and interrupts waits",
          "[server_main][signal]") {
    // Reset g_running
    g_running = true;

    MachineType machine;
    bool pacing_stopped = false;

    g_request_machine_shutdown = [&machine]() { machine.request_shutdown(); };
    g_request_pacing_stop = [&pacing_stopped]() { pacing_stopped = true; };
    g_notify_clients_shutdown = nullptr;

    invoke_shutdown();

    REQUIRE_FALSE(g_running);
    REQUIRE(machine.shutdown_requested());
    REQUIRE(pacing_stopped);

    // Clean up
    g_request_machine_shutdown = nullptr;
    g_request_pacing_stop = nullptr;
    g_running = true;
}

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>

TEST_CASE("SIGTERM: terminates server process within 2 seconds",
          "[server_main][signal][integration]") {
    // Fork a child that installs the shutdown handler and blocks in a loop
    pid_t child = fork();
    REQUIRE(child >= 0);

    if (child == 0) {
        // Child process: install handler and block
        std::atomic<bool> running{true};
        beebium::server::platform::install_shutdown_handler([&running] {
            running = false;
        });

        // Simulate the main loop: poll for pending signals with bounded waits
        while (running) {
            beebium::server::platform::dispatch_pending_signal();
            usleep(10000);  // 10ms
        }

        _exit(0);
    }

    // Parent: give child time to start, then send SIGTERM
    usleep(100000);  // 100ms
    kill(child, SIGTERM);

    // Wait for child to exit (timeout 2 seconds)
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 40; ++i) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            exited = true;
            break;
        }
        usleep(50000);  // 50ms
    }

    if (!exited) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }

    REQUIRE(exited);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}
#endif
