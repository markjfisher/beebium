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

#ifndef BEEBIUM_SERVER_SERVER_MAIN_HPP
#define BEEBIUM_SERVER_SERVER_MAIN_HPP

#include "beebium/Machines.hpp"
#include "beebium/PacingClock.hpp"
#include "beebium/disc/DiscLoader.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/server/RomPaths.hpp"

#include <array>
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <unistd.h>  // for isatty()
#include <vector>

namespace beebium::server {

namespace {

constexpr uint16_t DEFAULT_GRPC_PORT = 0xBEEB;  // 48875

enum class WaitMode {
    None,   // Start immediately
    Cli,    // Wait for RETURN on console
    Api     // Wait for Run() RPC
};

std::atomic<bool> g_running{true};

void signal_handler(int /*signal*/) {
    g_running = false;
}

std::vector<uint8_t> load_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filepath.string());
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Cannot read file: " + filepath.string());
    }

    return data;
}

// Parse "slot:filepath" format, returns (slot, filepath)
// Empty filepath (e.g., "11:") means explicitly leave slot empty
std::pair<uint8_t, std::string> parse_rom_arg(const std::string& arg) {
    auto colon_pos = arg.find(':');
    if (colon_pos == std::string::npos) {
        throw std::runtime_error("Invalid --rom format: " + arg + " (expected slot:filepath or slot: for empty)");
    }
    std::string slot_str = arg.substr(0, colon_pos);
    std::string filepath = arg.substr(colon_pos + 1);

    int slot = std::stoi(slot_str);
    if (slot < 0 || slot > 15) {
        throw std::runtime_error("Invalid slot number: " + slot_str + " (must be 0-15)");
    }

    return {static_cast<uint8_t>(slot), filepath};
}

// Parse "drive:url" format for floppy drives, returns (drive, url_or_filepath)
// Note: URL can contain colons, so we only split on the first colon if followed by a digit
std::pair<uint8_t, std::string> parse_floppy_arg(const std::string& arg) {
    // Check if first character is a digit (drive number)
    if (arg.empty() || !std::isdigit(arg[0])) {
        throw std::runtime_error("Invalid --floppy format: " + arg + " (expected drive:url)");
    }

    auto colon_pos = arg.find(':');
    if (colon_pos == std::string::npos) {
        throw std::runtime_error("Invalid --floppy format: " + arg + " (expected drive:url)");
    }

    std::string drive_str = arg.substr(0, colon_pos);
    std::string url_or_filepath = arg.substr(colon_pos + 1);

    int drive = std::stoi(drive_str);
    if (drive < 0 || drive > 1) {
        throw std::runtime_error("Invalid floppy drive number: " + drive_str + " (must be 0 or 1)");
    }

    if (url_or_filepath.empty()) {
        throw std::runtime_error("Invalid --floppy format: " + arg + " (URL or filepath required)");
    }

    return {static_cast<uint8_t>(drive), url_or_filepath};
}

// Sentinel value to mark a slot as explicitly empty
constexpr const char* EMPTY_SLOT_MARKER = "\x01EMPTY\x01";

// Parse --wait argument value
WaitMode parse_wait_arg(const std::string& value) {
    if (value == "cli") {
        return WaitMode::Cli;
    } else if (value == "api") {
        return WaitMode::Api;
    } else {
        throw std::runtime_error("Invalid --wait value: " + value + " (expected 'cli' or 'api')");
    }
}

// Convert a URL or filepath to a canonical file:// URL.
// If the input is already a URL, returns it unchanged.
// If it's a filepath, resolves to canonical absolute path (no . or ..) and prepends file://
std::string to_absolute_file_url(const std::string& url_or_filepath) {
    // If it's already a URL, return as-is
    if (url_or_filepath.find("://") != std::string::npos) {
        return url_or_filepath;
    }
    // Convert filepath to canonical absolute path (resolves . and .. and symlinks)
    auto canonical_path = std::filesystem::canonical(url_or_filepath);
    return "file://" + canonical_path.string();
}

} // anonymous namespace

template<typename MachineType>
void print_usage(const char* program_name) {
    using Memory = typename MachineType::Memory;
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Machine: " << Memory::MACHINE_DISPLAY_NAME << "\n"
              << "\n"
              << "Optional:\n"
              << "  --mos <filepath>         MOS ROM filepath (default: " << Memory::DEFAULT_MOS_ROM << ")\n"
              << "  --rom <slot>:<filepath>  Load ROM into sideways slot (0-15)\n"
              << "  --rom <slot>:            Leave slot empty (overrides default)\n"
              << "  --rom-dir <dirpath>      ROM directory (auto-detected if not specified)\n"
              << "  --port <port>            gRPC port (default: " << DEFAULT_GRPC_PORT << ")\n"
              << "  --floppy <drive>:<url>   Load disc image into floppy drive (0 or 1)\n"
              << "                           Accepts file:// URLs or bare filepaths\n"
              << "  --screen-mode <0-7>      Startup screen mode (default: 7)\n"
              << "  --auto-boot              Reverse SHIFT-BREAK action (SHIFT-BREAK boots)\n"
              << "  --links <0-255>          Raw startup options byte (mutually exclusive\n"
              << "                           with --screen-mode and --auto-boot)\n"
              << "  --wait[=<mode>]          Wait before starting emulation:\n"
              << "                           cli - wait for RETURN keypress (default if TTY)\n"
              << "                           api - wait for Run() RPC (default if not TTY)\n"
              << "  --info                   Show machine information and exit\n"
              << "  --help                   Show this help message\n"
              << "\n"
              << "Default sideways ROMs:\n"
              << "  Slot " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT) << ": "
              << Memory::DEFAULT_LANGUAGE_ROM << " (language ROM)\n";

    // Show DFS default if machine has it
    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cerr << "  Slot " << static_cast<int>(Memory::DEFAULT_DFS_SLOT) << ": "
                  << Memory::DEFAULT_DFS_ROM << " (disc filing system)\n";
    }

    std::cerr << "\n"
              << "Examples:\n"
              << "  " << program_name << "                           # Use all defaults\n"
              << "  " << program_name << " --rom 15:forth.rom        # Replace BASIC with Forth\n"
              << "  " << program_name << " --floppy 0:game.ssd       # Load disc image in floppy 0\n";

    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cerr << "  " << program_name << " --rom 11:              # No DFS (leave slot 11 empty)\n";
    }
}

template<typename MachineType>
void print_info(const char* program_name) {
    using Memory = typename MachineType::Memory;

    // JSON output for machine discovery
    std::cout << "{\n"
              << "  \"executable\": \"" << program_name << "\",\n"
              << "  \"machine_type\": \"" << Memory::MACHINE_TYPE << "\",\n"
              << "  \"display_name\": \"" << Memory::MACHINE_DISPLAY_NAME << "\",\n"
              << "  \"version\": \"" << BEEBIUM_VERSION << "\",\n"
              << "  \"default_mos_rom\": \"" << Memory::DEFAULT_MOS_ROM << "\",\n"
              << "  \"default_language_rom\": \"" << Memory::DEFAULT_LANGUAGE_ROM << "\",\n"
              << "  \"default_language_slot\": " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT);

    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cout << ",\n"
                  << "  \"default_dfs_rom\": \"" << Memory::DEFAULT_DFS_ROM << "\",\n"
                  << "  \"default_dfs_slot\": " << static_cast<int>(Memory::DEFAULT_DFS_SLOT);
    }

    std::cout << "\n}\n";
}

template<typename MachineType>
int server_main(int argc, char* argv[]) {
    using Memory = typename MachineType::Memory;

    std::string mos_filepath;
    std::string rom_dirpath;
    std::map<uint8_t, std::string> rom_slots;  // slot -> filepath
    uint16_t port = DEFAULT_GRPC_PORT;
    std::array<std::string, 2> floppy_filepaths;  // drive 0 and 1

    // Startup options (keyboard links)
    // -1 means not set; we use int to detect if user specified a value
    int screen_mode = -1;
    bool auto_boot = false;
    int raw_links = -1;  // -1 means not set
    bool screen_mode_set = false;
    bool auto_boot_set = false;

    // Wait mode for controlled startup
    WaitMode wait_mode = WaitMode::None;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage<MachineType>(argv[0]);
            return 0;
        } else if (arg == "--info") {
            print_info<MachineType>(argv[0]);
            return 0;
        } else if (arg == "--mos" && i + 1 < argc) {
            mos_filepath = argv[++i];
        } else if (arg == "--rom" && i + 1 < argc) {
            auto [slot, filepath] = parse_rom_arg(argv[++i]);
            // Empty filepath means explicitly leave slot empty (override default)
            rom_slots[slot] = filepath.empty() ? EMPTY_SLOT_MARKER : filepath;
        } else if (arg == "--rom-dir" && i + 1 < argc) {
            rom_dirpath = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--floppy" && i + 1 < argc) {
            auto [drive, filepath] = parse_floppy_arg(argv[++i]);
            floppy_filepaths[drive] = filepath;
        } else if (arg == "--screen-mode" && i + 1 < argc) {
            screen_mode = std::stoi(argv[++i]);
            if (screen_mode < 0 || screen_mode > 7) {
                std::cerr << "Error: --screen-mode must be 0-7\n";
                return 1;
            }
            screen_mode_set = true;
        } else if (arg == "--auto-boot") {
            auto_boot = true;
            auto_boot_set = true;
        } else if (arg == "--links" && i + 1 < argc) {
            raw_links = std::stoi(argv[++i]);
            if (raw_links < 0 || raw_links > 255) {
                std::cerr << "Error: --links must be 0-255\n";
                return 1;
            }
        } else if (arg == "--wait") {
            // Bare --wait: use TTY detection to choose default
            wait_mode = isatty(STDIN_FILENO) ? WaitMode::Cli : WaitMode::Api;
        } else if (arg.rfind("--wait=", 0) == 0) {
            // --wait=cli or --wait=api
            std::string wait_value = arg.substr(7);  // Skip "--wait="
            wait_mode = parse_wait_arg(wait_value);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage<MachineType>(argv[0]);
            return 1;
        }
    }

    // Validate startup options: --links is mutually exclusive with semantic options
    if (raw_links >= 0 && (screen_mode_set || auto_boot_set)) {
        std::cerr << "Error: --links cannot be combined with --screen-mode or --auto-boot\n";
        return 1;
    }

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // Set ROM directory if specified
        if (!rom_dirpath.empty()) {
            RomPaths::set_rom_directory(rom_dirpath);
        }

        // Use default MOS ROM if not specified
        if (mos_filepath.empty()) {
            mos_filepath = std::string(Memory::DEFAULT_MOS_ROM);
        }

        // Load default language ROM into default slot unless overridden
        if (rom_slots.find(Memory::DEFAULT_LANGUAGE_SLOT) == rom_slots.end()) {
            rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] = std::string(Memory::DEFAULT_LANGUAGE_ROM);
        }

        // Load default DFS ROM if machine has one and slot not overridden
        if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
            if (rom_slots.find(Memory::DEFAULT_DFS_SLOT) == rom_slots.end()) {
                rom_slots[Memory::DEFAULT_DFS_SLOT] = std::string(Memory::DEFAULT_DFS_ROM);
            }
        }

        // Load MOS ROM
        auto mos_path = RomPaths::find_rom(mos_filepath);
        std::cout << "Loading MOS ROM: " << mos_path << "\n";
        auto mos_data = load_file(mos_path);
        if (mos_data.size() != 16384) {
            std::cerr << "Warning: MOS ROM is " << mos_data.size()
                      << " bytes, expected 16384\n";
        }

        // Create and initialize machine
        std::cout << "Initializing " << Memory::MACHINE_DISPLAY_NAME << "...\n";
        MachineType machine;

        // Load MOS ROM into machine
        std::copy(mos_data.begin(), mos_data.end(),
                  machine.state().memory.mos_rom.data());

        // Load sideways ROMs
        for (const auto& [slot, filepath] : rom_slots) {
            // Skip slots explicitly marked as empty
            if (filepath == EMPTY_SLOT_MARKER) {
                std::cout << "Slot " << static_cast<int>(slot) << ": empty (no ROM loaded)\n";
                continue;
            }

            auto rom_path = RomPaths::find_rom(filepath);
            std::cout << "Loading ROM into slot " << static_cast<int>(slot) << ": " << rom_path << "\n";
            auto rom_data = load_file(rom_path);

            if (rom_data.size() != 16384) {
                std::cerr << "Warning: ROM is " << rom_data.size()
                          << " bytes, expected 16384\n";
            }

            // Load ROM into appropriate socket based on machine type
            // Model B+ has 6 ROM sockets, each covering a pair of slots:
            //   IC71 (slots 1/15): BASIC/language ROM
            //   IC68 (slots 10/11): DFS ROM
            //   IC62 (slots 8/9): User ROM
            //   IC57 (slots 6/7): User ROM
            //   IC44 (slots 4/5): User ROM
            //   IC35 (slots 2/3): User ROM
            // Model B has simpler ROM sockets at slots 0, 1, 4, 15
            bool loaded = false;

            // Model B+ specific ROM sockets (IC71 has different slot mapping than Model B)
            if constexpr (requires { machine.state().memory.rom_ic62; }) {
                switch (slot) {
                    case 15: case 14: case 1: case 0:  // IC71 - BASIC (link S13 selects slot pair)
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.basic_rom.data());
                        loaded = true;
                        break;
                    case 11: case 10:  // IC68 - DFS
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.dfs_rom.data());
                        loaded = true;
                        break;
                    case 9: case 8:    // IC62 - User ROM
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.rom_ic62.data());
                        loaded = true;
                        break;
                    case 7: case 6:    // IC57 - User ROM
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.rom_ic57.data());
                        loaded = true;
                        break;
                    case 5: case 4:    // IC44 - User ROM
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.rom_ic44.data());
                        loaded = true;
                        break;
                    case 3: case 2:    // IC35 - User ROM
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.rom_ic35.data());
                        loaded = true;
                        break;
                    default:
                        break;
                }
            } else {
                // Model B ROM sockets
                switch (slot) {
                    case 15: case 0:  // BASIC
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.basic_rom.data());
                        loaded = true;
                        break;
                    case 1:  // DFS
                        std::copy(rom_data.begin(), rom_data.end(),
                                  machine.state().memory.dfs_rom.data());
                        loaded = true;
                        break;
                    default:
                        break;
                }
            }

            if (!loaded) {
                std::cerr << "Warning: Slot " << static_cast<int>(slot)
                          << " is not a valid ROM socket for this machine, ROM ignored\n";
            }
        }

        // Load disc images (Model B+ only)
        if constexpr (requires { machine.state().memory.disc_drive_0; }) {
            if (!floppy_filepaths[0].empty()) {
                // Resolve to absolute file:// URL for consistent API behaviour
                auto source_url = to_absolute_file_url(floppy_filepaths[0]);
                std::cout << "Loading disc into floppy 0: " << source_url << "\n";
                auto result = load_disc_from_url(source_url);
                if (!result) {
                    throw std::runtime_error("Failed to load disc: " + result.error);
                }
                if (result.image->is_write_protected()) {
                    std::cout << "  (write-protected)\n";
                }
                machine.state().memory.disc_drive_0.insert(std::move(result.image), source_url);
            }

            if (!floppy_filepaths[1].empty()) {
                // Resolve to absolute file:// URL for consistent API behaviour
                auto source_url = to_absolute_file_url(floppy_filepaths[1]);
                std::cout << "Loading disc into floppy 1: " << source_url << "\n";
                auto result = load_disc_from_url(source_url);
                if (!result) {
                    throw std::runtime_error("Failed to load disc: " + result.error);
                }
                if (result.image->is_write_protected()) {
                    std::cout << "  (write-protected)\n";
                }
                machine.state().memory.disc_drive_1.insert(std::move(result.image), source_url);
            }
        }

        // Enable video output
        machine.state().memory.enable_video_output();

        // Enable audio output
        machine.state().memory.enable_audio_output();

        // Apply startup options (keyboard links) before reset
        // These must be set before reset() as the MOS reads them during initialization
        if (raw_links >= 0) {
            // Raw byte overrides everything
            machine.state().memory.set_startup_options(static_cast<uint8_t>(raw_links));
            std::cout << "Startup links: 0x" << std::hex << raw_links << std::dec << "\n";
        } else {
            // Apply semantic options (modifies specific bits, preserves others)
            if (screen_mode_set) {
                machine.state().memory.set_screen_mode(static_cast<uint8_t>(screen_mode));
                std::cout << "Startup screen mode: " << screen_mode << "\n";
            }
            if (auto_boot_set) {
                machine.state().memory.set_auto_boot(auto_boot);
                std::cout << "Auto-boot: " << (auto_boot ? "enabled" : "disabled") << "\n";
            }
        }

        // Reset machine
        machine.reset();

        // Start gRPC server
        std::cout << "Starting gRPC server on port " << port << "...\n";
        beebium::service::Server<MachineType> server(machine, "0.0.0.0", port);
        server.start();

        std::cout << Memory::MACHINE_DISPLAY_NAME << " ready. Press Ctrl+C to stop.\n";

        // Handle wait mode for controlled startup
        switch (wait_mode) {
            case WaitMode::Cli:
                // Wait for user to press RETURN before starting emulation
                // "Now press RETURN." is a reference to Roger McGough's electronic poem
                // from the original BBC Micro Welcome cassette.
                std::cout << "Now press RETURN." << std::flush;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "\n";
                break;

            case WaitMode::Api:
                // Complete the reset sequence (7 cycles) so PC contains the
                // actual reset vector value, then pause before first instruction.
                // The 6502 reset sequence reads the reset vector from $FFFC/$FFFD
                // during cycles 4-6, loading PC with the entry point address.
                machine.run(7);
                machine.pause();
                std::cout << "Paused at first instruction (PC=$"
                          << std::hex << std::uppercase
                          << machine.state().cpu.pc.w
                          << std::dec << "). Waiting for Run() RPC...\n";
                break;

            case WaitMode::None:
                // Start immediately
                break;
        }

        // Check for BEEBIUM_NO_PACING environment variable for debugging
        const char* no_pacing_env = std::getenv("BEEBIUM_NO_PACING");
        bool use_pacing = (no_pacing_env == nullptr);

        // Create and start pacing clock with machine-specific configuration
        PacingClock pacing_clock(Memory::default_pacing_config());

        if (use_pacing) {
            pacing_clock.start();
            std::cout << "Pacing: " << Memory::default_pacing_config().pacing_hz << " Hz, "
                      << Memory::default_pacing_config().cycles_per_tick() << " cycles/tick\n";
        } else {
            std::cout << "Pacing: DISABLED (BEEBIUM_NO_PACING set)\n";
        }

        // Main emulation loop
        constexpr uint64_t cycles_per_frame = 40000;  // For non-paced mode
        while (g_running) {
            // Block if debugger has paused execution
            machine.wait_if_paused();

            // Run cycles
            machine.run(use_pacing ? pacing_clock.cycles_per_tick() : cycles_per_frame);

            // Wait for next tick (pacing clock handles timing)
            if (use_pacing) {
                pacing_clock.wait_for_tick();
            }
        }

        if (use_pacing) {
            pacing_clock.stop();
        }

        std::cout << "\nShutting down...\n";
        server.stop();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace beebium::server

#endif // BEEBIUM_SERVER_SERVER_MAIN_HPP
