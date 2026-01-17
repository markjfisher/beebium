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

TEST_CASE("parse_arguments: --info returns exit code 0", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--info"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 0);
}

TEST_CASE("parse_arguments: --list-fdc returns exit code 0", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--list-fdc"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 0);
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

TEST_CASE("parse_arguments: deprecated --rom with filepath", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--rom", "15:forth.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 15);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Rom);
    REQUIRE(config.rom_slots[15] == "forth.rom");
}

TEST_CASE("parse_arguments: deprecated --rom with empty (clear slot)", "[server_main][parse_arguments]") {
    ArgvHelper args{"beebium", "--rom", "11:"};
    ServerConfig<MachineType> config;

    auto result = parse_arguments<MachineType>(args.argc(), args.data(), config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 11);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Empty);
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
// Helper to set up config to avoid loading non-existent default DFS ROM
// The test environment may not have acorn-dfs_1_00.rom, so we clear the default DFS slot
void setup_test_config(ServerConfig<MachineType>& config) {
    using Memory = MachineType::Memory;
    // Clear the default DFS slot so we don't try to load non-existent ROM
    config.rom_slots[Memory::DEFAULT_DFS_SLOT] = EMPTY_SLOT_MARKER;
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
