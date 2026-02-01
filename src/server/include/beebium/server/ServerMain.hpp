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
#include "beebium/disc/DiscControllerRegistry.hpp"
#include "beebium/disc/DiscConcepts.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/server/PresetLoader.hpp"
#include "beebium/server/RomPaths.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include "beebium/server/Platform.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// Types used by ServerConfig (must be outside anonymous namespace to avoid -Wsubobject-linkage)
constexpr uint16_t DEFAULT_GRPC_PORT = 0xBEEB;  // 48875

enum class WaitMode {
    None,   // Start immediately
    Cli,    // Wait for RETURN on console
    Api     // Wait for Run() RPC
};

enum class OutputFormat {
    Auto,    // Detect based on platform::is_stdout_tty()
    Pretty,  // Human-friendly formatted output
    Tsv,     // Tab-separated values with header
    Jsonl    // JSON Lines (one object per line)
};

// Exit codes following sysexits.h conventions
namespace ExitCode {
    constexpr int OK = 0;           // Success
    constexpr int USAGE = 64;       // Command line usage error (EX_USAGE)
    constexpr int DATAERR = 65;     // Data format error (EX_DATAERR)
    constexpr int NOINPUT = 66;     // Cannot open input file (EX_NOINPUT)
    constexpr int SOFTWARE = 70;    // Internal software error (EX_SOFTWARE)
    constexpr int IOERR = 74;       // I/O error (EX_IOERR)
    constexpr int CONFIG = 78;      // Configuration error (EX_CONFIG)
}

// Configuration parsed from global (pre-subcommand) arguments
struct GlobalConfig {
    std::string subcommand_name;    // Empty = default to "start"
    int subcommand_argv_start = 1;  // Index where subcommand args begin
    bool help_requested = false;    // --help before subcommand
    OutputFormat output_format = OutputFormat::Auto;  // --format option
};

// Forward declaration for subcommand base class
template<typename MachineType> class Subcommand;

// Abstract base class for subcommands
template<typename MachineType>
class Subcommand {
public:
    virtual ~Subcommand() = default;

    // Subcommand identifier (e.g., "start", "list-fdcs")
    virtual std::string_view name() const = 0;

    // Short description for help listing
    virtual std::string_view description() const = 0;

    // Print subcommand-specific help
    virtual void help(const char* program_name) const = 0;

    // Execute the subcommand
    // Returns exit code
    virtual int invoke(int argc, char* argv[], const GlobalConfig& global) const = 0;
};

// Anonymous namespace for internal implementation details
namespace {

std::atomic<bool> g_running{true};

// Function pointers for shutdown handler to interrupt blocking waits.
// These are set before the main loop starts and cleared on shutdown.
std::function<void()> g_request_machine_shutdown;
std::function<void()> g_request_pacing_stop;
std::function<void()> g_notify_clients_shutdown;

// Shutdown handler logic invoked by platform::install_shutdown_handler()
inline void invoke_shutdown() {
    g_running = false;
    // Notify connected clients that shutdown is starting
    if (g_notify_clients_shutdown) {
        g_notify_clients_shutdown();
    }
    // Interrupt any blocked wait_if_paused() or wait_for_tick()
    if (g_request_machine_shutdown) {
        g_request_machine_shutdown();
    }
    if (g_request_pacing_stop) {
        g_request_pacing_stop();
    }
}

inline std::vector<uint8_t> load_file(const std::filesystem::path& filepath) {
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

// Validate UUID string format (RFC 4122: 8-4-4-4-12 hex digits with hyphens)
// e.g., "550e8400-e29b-41d4-a716-446655440000"
inline bool is_valid_uuid(const std::string& s) {
    if (s.length() != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
        }
    }
    return true;
}

// Generate a random UUID v4
inline std::string generate_uuid_v4() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    uint64_t a = dis(gen);
    uint64_t b = dis(gen);

    // Set version 4 (0100) in bits 12-15 of time_hi_and_version
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant (10) in bits 6-7 of clock_seq_hi_and_reserved
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
             static_cast<uint32_t>(a >> 32),
             static_cast<uint16_t>(a >> 16),
             static_cast<uint16_t>(a),
             static_cast<uint16_t>(b >> 48),
             static_cast<unsigned long long>(b & 0x0000FFFFFFFFFFFFULL));
    return std::string(buf);
}

// Parse an integer from a string with support for multiple bases.
// Supported formats:
//   - Decimal (no prefix): "123", "0", "255"
//   - Binary (0b prefix): "0b1010", "0B1111"
//   - Octal (0o prefix): "0o17", "0O377"
//   - Hexadecimal (0x prefix): "0xff", "0XBE", "0xBEEB"
// Throws std::runtime_error on invalid input.
int parse_int(const std::string& str, const std::string& context = "") {
    if (str.empty()) {
        throw std::runtime_error("Empty integer value" + (context.empty() ? "" : " for " + context));
    }

    std::string_view sv = str;
    int base = 10;
    size_t start = 0;

    // Check for base prefix
    if (sv.length() >= 2 && sv[0] == '0') {
        char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(sv[1])));
        if (prefix == 'b') {
            base = 2;
            start = 2;
        } else if (prefix == 'o') {
            base = 8;
            start = 2;
        } else if (prefix == 'x') {
            base = 16;
            start = 2;
        }
    }

    if (start >= sv.length()) {
        throw std::runtime_error("Invalid integer: " + str + (context.empty() ? "" : " for " + context));
    }

    // Parse the number using long long for consistent 64-bit range on all platforms
    // (Windows has 32-bit long, while Unix has 64-bit long)
    std::string digits(sv.substr(start));
    char* end = nullptr;
    errno = 0;
    long long value = std::strtoll(digits.c_str(), &end, base);

    // Check for parsing errors
    if (end == digits.c_str() || *end != '\0') {
        throw std::runtime_error("Invalid integer: " + str + (context.empty() ? "" : " for " + context));
    }

    // Check for overflow (either strtoll overflow or int range overflow)
    if (errno == ERANGE || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Integer overflow: " + str + (context.empty() ? "" : " for " + context));
    }

    return static_cast<int>(value);
}

// Complete a colon-terminated argument value with the next argv element.
// This enables shell tab completion for arguments like "--floppy 0: game.ssd"
// where the shell inserts a space after tab-completing the filepath.
//
// If value ends with ':' and next argv exists and doesn't look like an option,
// appends the next argv to value and advances i to consume it.
//
// Example: "--floppy 0:" followed by "game.ssd" becomes "0:game.ssd"
// Example: "--sideways 15:rom:" followed by "forth.rom" becomes "15:rom:forth.rom"
inline void complete_colon_arg(std::string& value, int& i, int argc, char* argv[]) {
    if (value.empty() || value.back() != ':') {
        return;  // Doesn't end with colon, no completion needed
    }

    int next_i = i + 1;
    if (next_i >= argc) {
        return;  // No next argument available
    }

    std::string_view next_arg = argv[next_i];
    if (next_arg.empty() || next_arg[0] == '-') {
        return;  // Next arg looks like an option, not a continuation
    }

    // Append next arg to value and consume it
    value += std::string(next_arg);
    i = next_i;
}

// Parse "drive:filepath" or "drive:url" format for floppy drives
// Returns (drive, filepath_or_url)
inline std::pair<uint8_t, std::string> parse_floppy_arg(const std::string& arg) {
    auto colon_pos = arg.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0) {
        throw std::runtime_error("Invalid --floppy format: " + arg + " (expected drive:filepath)");
    }

    std::string drive_str = arg.substr(0, colon_pos);
    std::string filepath_or_url = arg.substr(colon_pos + 1);

    int drive = parse_int(drive_str, "--floppy drive");
    if (drive < 0 || drive > 1) {
        throw std::runtime_error("Invalid floppy drive number: " + drive_str + " (must be 0 or 1)");
    }

    if (filepath_or_url.empty()) {
        throw std::runtime_error("Invalid --floppy format: " + arg + " (filepath required)");
    }

    return {static_cast<uint8_t>(drive), filepath_or_url};
}

// Sentinel values to mark slots that should not have default ROMs loaded
constexpr const char* EMPTY_SLOT_MARKER = "\x01EMPTY\x01";
constexpr const char* RAM_SLOT_MARKER = "\x01RAM\x01";

} // anonymous namespace

// Sideways slot configuration (for --sideways argument)
// Outside anonymous namespace because used by ServerConfig
enum class SidewaysSlotType { Empty, Rom, Ram };

struct SidewaysConfig {
    uint8_t slot;
    SidewaysSlotType type;
    std::string image_filepath;  // Optional: filepath for ROM or pre-loaded RAM
};

namespace {

// Parse --sideways argument: SLOT:TYPE[:IMAGE]
// Examples:
//   15:rom:bbc-basic_2.rom  - ROM with image
//   4:ram                    - Empty RAM
//   4:ram:preload.bin        - RAM with pre-loaded image
//   2:empty                  - Empty slot
inline SidewaysConfig parse_sideways_arg(const std::string& arg) {
    SidewaysConfig config{};

    // Find first colon (slot:type...)
    auto first_colon = arg.find(':');
    if (first_colon == std::string::npos) {
        throw std::runtime_error("Invalid --sideways format: " + arg +
            " (expected SLOT:TYPE[:IMAGE])");
    }

    // Parse slot number
    std::string slot_str = arg.substr(0, first_colon);
    int slot = parse_int(slot_str, "--sideways slot");
    if (slot < 0 || slot > 15) {
        throw std::runtime_error("Invalid slot number: " + slot_str + " (must be 0-15)");
    }
    config.slot = static_cast<uint8_t>(slot);

    // Find second colon (type:image...) or end
    std::string remainder = arg.substr(first_colon + 1);
    auto second_colon = remainder.find(':');

    std::string type_str;
    if (second_colon == std::string::npos) {
        type_str = remainder;
        // No image path
    } else {
        type_str = remainder.substr(0, second_colon);
        config.image_filepath = remainder.substr(second_colon + 1);
    }

    // Parse type
    if (type_str == "empty") {
        config.type = SidewaysSlotType::Empty;
        if (!config.image_filepath.empty()) {
            throw std::runtime_error("--sideways: 'empty' type cannot have image path");
        }
    } else if (type_str == "rom") {
        config.type = SidewaysSlotType::Rom;
        if (config.image_filepath.empty()) {
            throw std::runtime_error("--sideways: 'rom' type requires image path");
        }
    } else if (type_str == "ram") {
        config.type = SidewaysSlotType::Ram;
        // image_filepath is optional for RAM (pre-loaded battery-backed RAM)
    } else {
        throw std::runtime_error("Invalid --sideways type: " + type_str +
            " (expected 'empty', 'rom', or 'ram')");
    }

    return config;
}

// Parse --wait argument value
inline WaitMode parse_wait_arg(const std::string& value) {
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
inline std::string to_absolute_file_url(const std::string& url_or_filepath) {
    // If it's already a URL, return as-is
    if (url_or_filepath.find("://") != std::string::npos) {
        return url_or_filepath;
    }
    // Convert filepath to canonical absolute path (resolves . and .. and symlinks)
    auto canonical_path = std::filesystem::canonical(url_or_filepath);
    return "file://" + canonical_path.string();
}

// Resolve Auto format to actual format based on TTY detection
inline OutputFormat resolve_output_format(OutputFormat format) {
    if (format == OutputFormat::Auto) {
        return platform::is_stdout_tty() ? OutputFormat::Pretty : OutputFormat::Tsv;
    }
    return format;
}

// Parse --format argument value
OutputFormat parse_format_arg(const std::string& value) {
    if (value == "pretty") return OutputFormat::Pretty;
    if (value == "tsv") return OutputFormat::Tsv;
    if (value == "jsonl") return OutputFormat::Jsonl;
    throw std::runtime_error("Invalid --format value: " + value +
                             " (expected 'pretty', 'tsv', or 'jsonl')");
}

// Parse global arguments that appear before the subcommand.
// Returns:
//   - std::nullopt on success (continue with subcommand dispatch)
//   - ExitCode::USAGE for errors
inline std::optional<int> parse_global_arguments(int argc, char* argv[], GlobalConfig& global) {
    // Scan arguments looking for global options or subcommand
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        // Check for help flag before subcommand
        if (arg == "--help" || arg == "-h") {
            global.help_requested = true;
            // Continue scanning to see if there's a subcommand after --help
            continue;
        }

        // Check for --format option
        if (arg == "--format" && i + 1 < argc) {
            try {
                global.output_format = parse_format_arg(argv[++i]);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            continue;
        }

        // Check for --format=value form
        if (arg.rfind("--format=", 0) == 0) {
            std::string format_value(arg.substr(9));  // Skip "--format="
            try {
                global.output_format = parse_format_arg(format_value);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            continue;
        }

        // First non-option argument is the subcommand
        if (!arg.empty() && arg[0] != '-') {
            global.subcommand_name = std::string(arg);
            global.subcommand_argv_start = i + 1;
            return std::nullopt;  // Success
        }

        // Argument starting with '-' but not a recognized global option: this is a subcommand option.
        // Stop parsing global args and let the default subcommand handle it.
        // (This enables "beebium --port 8080" to work as "beebium start --port 8080")
        global.subcommand_argv_start = i;  // Subcommand-specific args start here
        return std::nullopt;
    }

    // No subcommand found, will default to "start"
    global.subcommand_argv_start = argc;  // No subcommand-specific args
    return std::nullopt;
}

} // anonymous namespace

// Configuration struct for server initialization
// Aggregates all parsed command-line arguments
template<typename MachineType>
struct ServerConfig {
    // ROM configuration
    std::string mos_filepath;
    std::string rom_dirpath;
    std::map<uint8_t, std::string> rom_slots;
    std::vector<SidewaysConfig> sideways_configs;

    // Network
    uint16_t port = DEFAULT_GRPC_PORT;

    // Disc configuration
    std::array<std::string, 2> floppy_filepaths;
    std::string fdc_type;

    // Startup options (keyboard links)
    // -1 means not set; we use int to detect if user specified a value
    int screen_mode = -1;
    bool auto_boot = false;
    int raw_links = -1;
    bool screen_mode_set = false;
    bool auto_boot_set = false;

    // Wait mode for controlled startup
    WaitMode wait_mode = WaitMode::None;

    // Provenance
    std::string provenance_type;
    std::string provenance_uuid;
    std::string provenance_version;

    // Machine identity
    std::string machine_uuid;
    std::string machine_name;

    // Shutdown policy
    bool allow_shutdown = false;

    // mDNS advertisement
    bool advertise = false;

    // Preset file path
    std::optional<std::filesystem::path> preset_filepath;
};

template<typename MachineType>
void print_usage(const char* program_name) {
    using Memory = typename MachineType::Memory;
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Machine: " << Memory::MACHINE_DISPLAY_NAME << "\n"
              << "\n"
              << "Optional:\n"
              << "  --preset <filepath>      Load configuration from preset file\n"
              << "                           (CLI options override preset values)\n"
              << "  --mos <filepath>         MOS ROM filepath (default: " << Memory::DEFAULT_MOS_ROM << ")\n"
              << "  --sideways SLOT:TYPE[:IMAGE]\n"
              << "                           Configure sideways slot (0-15):\n"
              << "                           SLOT:rom:IMAGE - ROM with image file\n"
              << "                           SLOT:ram[:IMAGE] - RAM (optional pre-load)\n"
              << "                           SLOT:empty - Empty slot\n"
              << "  --rom-dir <dirpath>      ROM directory (auto-detected if not specified)\n"
              << "  --port <port>            gRPC port (default: " << DEFAULT_GRPC_PORT << ")\n"
              << "  --floppy <drive>:<filepath|url>\n"
              << "                           Load disc image into floppy drive (0 or 1)\n";

    // Show --fdc option only for machines with disc controller sockets
    if constexpr (HasDiscControllerSocket<Memory>) {
        std::cerr << "  --fdc <type>             Disc controller to install in socket:\n"
                  << "                           acorn-1770 - Acorn WD1770 controller\n"
                  << "                           none - leave socket empty (no disc)\n";
    }

    std::cerr << "  --screen-mode <0-7>      Startup screen mode (default: 7)\n"
              << "  --auto-boot              Reverse SHIFT-BREAK action (SHIFT-BREAK boots)\n"
              << "  --links <0-255>          Raw startup options byte (mutually exclusive\n"
              << "                           with --screen-mode and --auto-boot)\n"
              << "  --wait[=<mode>]          Wait before starting emulation:\n"
              << "                           cli - wait for RETURN keypress (default if TTY)\n"
              << "                           api - wait for Run() RPC (default if not TTY)\n"
              << "  --provenance-type <type> Provenance type (e.g., python-client, macos-gui)\n"
              << "  --provenance-uuid <uuid> Provenance instance UUID (RFC 4122)\n"
              << "  --provenance-version <v> Provenance version string\n"
              << "  --machine-uuid <uuid>    Machine identity UUID (RFC 4122, default: auto-generated)\n"
              << "  --machine-name <name>    Machine name/label (default: from model)\n"
              << "  --allow-shutdown         Allow any client to shut down the server\n"
              << "  --advertise              Enable mDNS service advertisement\n"
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
              << "  " << program_name << "                                  # Use all defaults\n"
              << "  " << program_name << " --sideways 15:rom:forth.rom      # Replace BASIC with Forth\n"
              << "  " << program_name << " --floppy 0:game.ssd              # Load disc image in floppy 0\n";

    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cerr << "  " << program_name << " --sideways 11:empty             # No DFS (leave slot 11 empty)\n";
    }

    if constexpr (HasDiscControllerSocket<Memory>) {
        std::cerr << "  " << program_name << " --fdc acorn-1770               # Install disc controller\n";
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

// Apply preset configuration to ServerConfig.
// Preset values provide defaults that can be overridden by CLI arguments.
template<typename MachineType>
void apply_preset(ServerConfig<MachineType>& config, const PresetConfig& preset) {
    if (preset.storage) {
        const auto& storage = *preset.storage;

        // FDC socket
        if (storage.fdc_socket_id && config.fdc_type.empty()) {
            config.fdc_type = *storage.fdc_socket_id;
        }
        // Note: fdc_socket_mode stored for future use

        // Floppy drives
        for (const auto& [drive, uri_opt] : storage.floppy_drives) {
            if (drive < config.floppy_filepaths.size() && config.floppy_filepaths[drive].empty()) {
                if (uri_opt) {
                    config.floppy_filepaths[drive] = *uri_opt;
                }
                // nullopt means empty drive - leave as empty string (default)
            }
        }

        // Cassette - future enhancement
    }
}

// Parse command-line arguments for the 'start' subcommand into a ServerConfig struct.
// Returns:
//   - std::nullopt on success (continue execution)
//   - ExitCode::OK for --help (exit successfully)
//   - ExitCode::USAGE or ExitCode::CONFIG for errors
//
// Preset merge semantics: If --preset is specified, the preset file is loaded first
// and its values populate the config. Then CLI arguments are parsed and can override
// any preset value. This allows presets to provide defaults while CLI provides overrides.
template<typename MachineType>
std::optional<int> parse_start_arguments(int argc, char* argv[], int start_index,
                                          ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    // First pass: find --preset and load it before other options
    // This allows CLI options to override preset values
    for (int i = start_index; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--preset" && i + 1 < argc) {
            config.preset_filepath = argv[i + 1];
            auto result = load_preset(*config.preset_filepath);
            if (!result) {
                std::cerr << "Error: " << result.error << "\n";
                return ExitCode::NOINPUT;
            }

            // Validate model compatibility if preset specifies a model
            if (result.config->model) {
                std::string_view preset_model = *result.config->model;
                std::string_view machine_type = Memory::MACHINE_TYPE;
                if (preset_model != machine_type) {
                    std::cerr << "Error: Preset is for model '" << preset_model
                              << "' but this executable is '" << machine_type << "'\n";
                    return ExitCode::CONFIG;
                }
            }

            // Apply preset as baseline configuration
            apply_preset(config, *result.config);
            break;
        }
    }

    // Second pass: parse all CLI arguments (including --preset which we skip now)
    for (int i = start_index; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--preset" && i + 1 < argc) {
            // Already handled in first pass, skip
            ++i;
            continue;
        } else if (arg == "--help" || arg == "-h") {
            print_usage<MachineType>(argv[0]);
            return ExitCode::OK;
        } else if (arg == "--mos" && i + 1 < argc) {
            config.mos_filepath = argv[++i];
        } else if (arg == "--sideways" && i + 1 < argc) {
            std::string value = argv[++i];
            complete_colon_arg(value, i, argc, argv);
            auto sideways_config = parse_sideways_arg(value);
            config.sideways_configs.push_back(sideways_config);
            // Add ALL slot types to rom_slots to prevent default ROM loading
            // This ensures --sideways 15:empty or --sideways 15:ram prevents BASIC from loading
            if (sideways_config.type == SidewaysSlotType::Rom) {
                config.rom_slots[sideways_config.slot] = sideways_config.image_filepath;
            } else if (sideways_config.type == SidewaysSlotType::Empty) {
                config.rom_slots[sideways_config.slot] = EMPTY_SLOT_MARKER;
            } else if (sideways_config.type == SidewaysSlotType::Ram) {
                config.rom_slots[sideways_config.slot] = RAM_SLOT_MARKER;
            }
        } else if (arg == "--rom-dir" && i + 1 < argc) {
            config.rom_dirpath = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                config.port = static_cast<uint16_t>(parse_int(argv[++i], "--port"));
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--floppy" && i + 1 < argc) {
            std::string value = argv[++i];
            complete_colon_arg(value, i, argc, argv);
            auto [drive, filepath] = parse_floppy_arg(value);
            config.floppy_filepaths[drive] = filepath;
        } else if (arg == "--screen-mode" && i + 1 < argc) {
            try {
                config.screen_mode = parse_int(argv[++i], "--screen-mode");
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            if (config.screen_mode < 0 || config.screen_mode > 7) {
                std::cerr << "Error: --screen-mode must be 0-7\n";
                return ExitCode::USAGE;
            }
            config.screen_mode_set = true;
        } else if (arg == "--auto-boot") {
            config.auto_boot = true;
            config.auto_boot_set = true;
        } else if (arg == "--links" && i + 1 < argc) {
            try {
                config.raw_links = parse_int(argv[++i], "--links");
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            if (config.raw_links < 0 || config.raw_links > 255) {
                std::cerr << "Error: --links must be 0-255\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--wait") {
            // Bare --wait: use TTY detection to choose default
            config.wait_mode = platform::is_stdin_tty() ? WaitMode::Cli : WaitMode::Api;
        } else if (arg.rfind("--wait=", 0) == 0) {
            // --wait=cli or --wait=api
            std::string wait_value = arg.substr(7);  // Skip "--wait="
            config.wait_mode = parse_wait_arg(wait_value);
        } else if (arg == "--fdc" && i + 1 < argc) {
            config.fdc_type = argv[++i];
        } else if (arg == "--provenance-type" && i + 1 < argc) {
            config.provenance_type = argv[++i];
        } else if (arg == "--provenance-uuid" && i + 1 < argc) {
            config.provenance_uuid = argv[++i];
        } else if (arg == "--provenance-version" && i + 1 < argc) {
            config.provenance_version = argv[++i];
        } else if (arg == "--machine-uuid" && i + 1 < argc) {
            config.machine_uuid = argv[++i];
            if (!is_valid_uuid(config.machine_uuid)) {
                std::cerr << "Error: --machine-uuid must be a valid UUID\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--machine-name" && i + 1 < argc) {
            config.machine_name = argv[++i];
        } else if (arg == "--allow-shutdown") {
            config.allow_shutdown = true;
        } else if (arg == "--advertise") {
            config.advertise = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage<MachineType>(argv[0]);
            return ExitCode::USAGE;
        }
    }

    return std::nullopt;  // Success, continue execution
}

// Validate configuration for consistency.
// Returns error message if invalid, nullopt if valid.
template<typename MachineType>
std::optional<std::string> validate_config(const ServerConfig<MachineType>& config) {
    // --links is mutually exclusive with semantic options
    if (config.raw_links >= 0 && (config.screen_mode_set || config.auto_boot_set)) {
        return "--links cannot be combined with --screen-mode or --auto-boot";
    }

    return std::nullopt;  // Valid
}

// Load MOS ROM and sideways ROMs into machine memory.
// Applies defaults to config where not specified.
// Throws on file I/O errors.
template<typename MachineType>
void load_roms(MachineType& machine, ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    // Apply default MOS ROM if not specified
    if (config.mos_filepath.empty()) {
        config.mos_filepath = std::string(Memory::DEFAULT_MOS_ROM);
    }

    // Load default language ROM into default slot unless overridden
    if (config.rom_slots.find(Memory::DEFAULT_LANGUAGE_SLOT) == config.rom_slots.end()) {
        config.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] = std::string(Memory::DEFAULT_LANGUAGE_ROM);
    }

    // Load default DFS ROM if machine has one and slot not overridden
    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        if (config.rom_slots.find(Memory::DEFAULT_DFS_SLOT) == config.rom_slots.end()) {
            config.rom_slots[Memory::DEFAULT_DFS_SLOT] = std::string(Memory::DEFAULT_DFS_ROM);
        }
    }

    // Load MOS ROM
    auto mos_path = RomPaths::find_rom(config.mos_filepath);
    std::cout << "Loading MOS ROM: " << mos_path << "\n";
    auto mos_data = load_file(mos_path);
    if (mos_data.size() != 16384) {
        std::cerr << "Warning: MOS ROM is " << mos_data.size()
                  << " bytes, expected 16384\n";
    }

    // Load MOS ROM into machine
    std::copy(mos_data.begin(), mos_data.end(),
              machine.state().memory.mos_rom.data());

    // Process all slot configurations using the unified API
    // This single loop handles ROM, RAM, and Empty slots
    for (const auto& [slot, marker_or_filepath] : config.rom_slots) {
        // Handle empty slots
        if (marker_or_filepath == EMPTY_SLOT_MARKER) {
            std::cout << "Slot " << static_cast<int>(slot) << ": empty\n";
            if constexpr (Memory::supports_slot_configuration()) {
                machine.state().memory.configure_slot_as_empty(slot);
            }
            continue;
        }

        // Handle RAM slots
        if (marker_or_filepath == RAM_SLOT_MARKER) {
            std::cout << "Slot " << static_cast<int>(slot) << ": RAM";
            if constexpr (Memory::supports_slot_configuration()) {
                machine.state().memory.configure_slot_as_ram(slot);
            } else {
                std::cerr << "\nWarning: RAM slots not supported on this machine type\n";
            }
            std::cout << "\n";
            continue;
        }

        // Regular ROM loading - use unified API
        auto rom_path = RomPaths::find_rom(marker_or_filepath);
        std::cout << "Loading ROM into slot " << static_cast<int>(slot) << ": " << rom_path << "\n";
        auto rom_data = load_file(rom_path);

        if (rom_data.size() != 16384) {
            std::cerr << "Warning: ROM is " << rom_data.size()
                      << " bytes, expected 16384\n";
        }

        // Unified API handles slot type configuration and loading together
        machine.state().memory.load_sideways_rom(slot, rom_data.data(), rom_data.size());
    }

    // Handle RAM slots with pre-loaded images
    // These are processed after rom_slots to ensure the slot is already configured as RAM
    for (const auto& sideways_config : config.sideways_configs) {
        if (sideways_config.type == SidewaysSlotType::Ram && !sideways_config.image_filepath.empty()) {
            auto ram_path = RomPaths::find_rom(sideways_config.image_filepath);
            auto ram_data = load_file(ram_path);
            std::cout << "Pre-loading RAM slot " << static_cast<int>(sideways_config.slot)
                      << " from " << ram_path << "\n";
            // Use load_sideways_data to load without changing slot type
            machine.state().memory.load_sideways_data(sideways_config.slot, ram_data.data(), ram_data.size());
        }
    }
}

// Install disc controller for machines with sockets.
// Returns exit code on error, nullopt on success.
template<typename MachineType>
std::optional<int> install_disc_controller(MachineType& machine, const ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    if constexpr (HasDiscControllerSocket<Memory>) {
        if (!config.fdc_type.empty()) {
            if (config.fdc_type == "none") {
                std::cout << "Disc controller: none (socket empty)\n";
                // Do nothing - leave socket empty
            } else if (auto controller = DiscControllerRegistry::create(config.fdc_type)) {
                auto* info = DiscControllerRegistry::find(config.fdc_type);
                std::cout << "Installing disc controller: " << info->display_name
                          << " (" << info->fdc_chip << ")\n";
                machine.state().memory.install_disc_controller(std::move(controller), config.fdc_type);
            } else {
                std::cerr << "Error: Unknown disc controller type: " << config.fdc_type << "\n"
                          << "Use --list-fdc to see available controllers.\n";
                return 1;
            }
        }
        // If --fdc not specified, socket remains empty (no disc controller)
    } else if (!config.fdc_type.empty()) {
        // Model B+ has built-in controller, --fdc option is not applicable
        std::cerr << "Warning: --fdc option ignored (machine has built-in disc controller)\n";
    }

    return std::nullopt;
}

// Load disc images into floppy drives.
// Throws on error.
template<typename MachineType>
void load_disc_images(MachineType& machine, const ServerConfig<MachineType>& config) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        if (!config.floppy_filepaths[0].empty()) {
            // Resolve to absolute file:// URL for consistent API behaviour
            auto source_url = to_absolute_file_url(config.floppy_filepaths[0]);
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

        if (!config.floppy_filepaths[1].empty()) {
            // Resolve to absolute file:// URL for consistent API behaviour
            auto source_url = to_absolute_file_url(config.floppy_filepaths[1]);
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
}

// Apply startup options (keyboard links) to machine.
// Must be called before machine.reset().
template<typename MachineType>
void apply_startup_options(MachineType& machine, const ServerConfig<MachineType>& config) {
    if (config.raw_links >= 0) {
        // Raw byte overrides everything
        machine.state().memory.set_startup_options(static_cast<uint8_t>(config.raw_links));
        std::cout << "Startup links: 0x" << std::hex << config.raw_links << std::dec << "\n";
    } else {
        // Apply semantic options (modifies specific bits, preserves others)
        if (config.screen_mode_set) {
            machine.state().memory.set_screen_mode(static_cast<uint8_t>(config.screen_mode));
            std::cout << "Startup screen mode: " << config.screen_mode << "\n";
        }
        if (config.auto_boot_set) {
            machine.state().memory.set_auto_boot(config.auto_boot);
            std::cout << "Auto-boot: " << (config.auto_boot ? "enabled" : "disabled") << "\n";
        }
    }
}

// Handle wait mode for controlled startup.
// Must be called after machine.reset() and before the main emulation loop.
template<typename MachineType>
void handle_wait_mode(MachineType& machine, WaitMode wait_mode) {
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
}

// Run the main emulation loop with pacing.
// This function blocks until g_running becomes false (signal handler sets it).
// Sets up shutdown callbacks for clean signal handling.
template<typename MachineType>
void run_emulation_loop(MachineType& machine, beebium::service::Server<MachineType>& server) {
    using Memory = typename MachineType::Memory;

    // Check for BEEBIUM_NO_PACING environment variable for debugging
    bool use_pacing = !platform::get_env("BEEBIUM_NO_PACING").has_value();

    // Create and start pacing clock with machine-specific configuration
    PacingClock pacing_clock(Memory::default_pacing_config());

    if (use_pacing) {
        pacing_clock.start();
        std::cout << "Pacing: " << Memory::default_pacing_config().pacing_hz << " Hz, "
                  << Memory::default_pacing_config().cycles_per_tick() << " cycles/tick\n";
    } else {
        std::cout << "Pacing: DISABLED (BEEBIUM_NO_PACING set)\n";
    }

    // Set up shutdown callbacks so signal handler can interrupt blocked waits
    // and notify clients of impending shutdown
    g_request_machine_shutdown = [&machine]() { machine.request_shutdown(); };
    g_request_pacing_stop = [&pacing_clock]() { pacing_clock.request_stop(); };
    g_notify_clients_shutdown = [&server]() { server.notify_shutdown(5000); };

    // Main emulation loop
    constexpr uint64_t cycles_per_frame = 40000;  // For non-paced mode
    while (g_running) {
        // Block if debugger has paused execution
        machine.wait_if_paused();

        // Check if shutdown was requested during wait
        if (machine.shutdown_requested()) {
            break;
        }

        // Run cycles
        machine.run(use_pacing ? pacing_clock.cycles_per_tick() : cycles_per_frame);

        // Wait for next tick (pacing clock handles timing)
        if (use_pacing) {
            pacing_clock.wait_for_tick();
        }
    }

    // Clean up shutdown callbacks
    g_request_machine_shutdown = nullptr;
    g_request_pacing_stop = nullptr;
    g_notify_clients_shutdown = nullptr;

    if (use_pacing) {
        pacing_clock.stop();
    }
}

// ============================================================================
// Concrete subcommand implementations
// ============================================================================

// Forward declarations for subcommand dispatch
template<typename MachineType>
const std::vector<std::unique_ptr<Subcommand<MachineType>>>& get_subcommands();

template<typename MachineType>
void print_global_help(const char* program_name);

// Start subcommand - starts the emulator server (default)
template<typename MachineType>
class StartSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "start"; }
    std::string_view description() const override { return "Start the emulator server (default)"; }

    void help(const char* program_name) const override {
        print_usage<MachineType>(program_name);
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help for this subcommand
        if (global.help_requested) {
            print_usage<MachineType>(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand-specific arguments
        ServerConfig<MachineType> config;
        if (auto exit_code = parse_start_arguments<MachineType>(argc, argv, global.subcommand_argv_start, config)) {
            return *exit_code;
        }

        // Validate configuration
        if (auto error = validate_config<MachineType>(config)) {
            std::cerr << "Error: " << *error << "\n";
            return ExitCode::CONFIG;
        }

        // Apply provenance defaults (protocol specification)
        if (config.provenance_type.empty()) {
            config.provenance_type = platform::is_stdin_tty() ? "terminal" : "unknown";
        }
        if (config.provenance_uuid.empty()) {
            config.provenance_uuid = generate_uuid_v4();
        } else if (!is_valid_uuid(config.provenance_uuid)) {
            std::cerr << "Error: --provenance-uuid must be a valid UUID\n";
            return ExitCode::USAGE;
        }
        auto timestamp = std::chrono::system_clock::now();

        // Set up shutdown handler (handles SIGINT/SIGTERM on POSIX, console events on Windows)
        platform::install_shutdown_handler(invoke_shutdown);

        try {
            // Set ROM directory if specified
            if (!config.rom_dirpath.empty()) {
                RomPaths::set_rom_directory(config.rom_dirpath);
            }

            // Create and initialize machine
            std::cout << "Initializing " << Memory::MACHINE_DISPLAY_NAME << "...\n";
            MachineType machine;

            // Load ROMs
            load_roms(machine, config);

            // Install disc controller
            if (auto exit_code = install_disc_controller(machine, config)) {
                return *exit_code;
            }

            // Load disc images
            load_disc_images(machine, config);

            // Enable video output
            machine.state().memory.enable_video_output();

            // Enable audio output
            machine.state().memory.enable_audio_output();

            // Apply startup options before reset
            apply_startup_options(machine, config);

            // Reset machine
            machine.reset();

            // Start gRPC server
            std::cout << "Starting gRPC server...\n";
            beebium::service::Server<MachineType> server(machine, "0.0.0.0", config.port);
            beebium::service::Provenance provenance{
                config.provenance_type,
                config.provenance_uuid,
                config.provenance_version,
                timestamp
            };

            // Apply identity defaults
            if (config.machine_uuid.empty()) {
                config.machine_uuid = generate_uuid_v4();
            }
            if (config.machine_name.empty()) {
                config.machine_name = std::string(Memory::MACHINE_DISPLAY_NAME);
            }

            beebium::service::MachineIdentity identity{
                config.machine_uuid,
                config.machine_name,
                std::string(Memory::MACHINE_TYPE),
                std::string(Memory::MACHINE_DISPLAY_NAME)
            };

            // Print provenance details for debugging (before move)
            auto timestamp_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                provenance.timestamp.time_since_epoch()).count();
            std::cout << "Provenance: type='" << provenance.type
                      << "', uuid=" << provenance.instance_uuid
                      << ", version=" << (provenance.version.empty() ? "(none)" : "'" + provenance.version + "'")
                      << ", timestamp=" << timestamp_seconds
                      << std::endl;

            // Print identity details
            std::cout << "Identity: uuid=" << identity.uuid
                      << ", name='" << identity.name
                      << "', model_type='" << identity.model_type
                      << "', model_name='" << identity.model_name
                      << "'" << std::endl;

            // Create shutdown callback that stops the emulation loop when RPC shutdown is requested
            beebium::service::ShutdownPolicyConfig shutdown_policy_config{config.allow_shutdown};
            auto shutdown_callback = [&machine]() {
                g_running = false;
                machine.request_shutdown();
                // Also interrupt pacing clock if it's waiting (may be nullptr during startup)
                if (g_request_pacing_stop) {
                    g_request_pacing_stop();
                }
            };
            server.start(std::move(provenance), std::move(identity),
                        config.advertise, shutdown_policy_config, std::move(shutdown_callback));

            // Print actual bound port (important when port 0 was requested for dynamic allocation)
            // Flush immediately so clients parsing stdout can detect the port before we block
            std::cout << "Listening on port " << server.port() << std::endl;
            std::cout << Memory::MACHINE_DISPLAY_NAME << " ready. Press Ctrl+C to stop." << std::endl;

            // Handle wait mode
            handle_wait_mode(machine, config.wait_mode);

            // Run main emulation loop (blocks until shutdown)
            run_emulation_loop(machine, server);

            std::cout << "\nShutting down...\n";
            server.stop();

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::SOFTWARE;
        }

        return ExitCode::OK;
    }
};

// ListFdcs subcommand - list available disc controllers
template<typename MachineType>
class ListFdcsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-fdcs"; }
    std::string_view description() const override { return "List available disc controllers"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-fdcs\n"
                  << "\n"
                  << "Lists all disc controllers that can be installed in machines\n"
                  << "with a disc controller socket (e.g., Model B).\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help for this subcommand
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            // Unknown argument
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Resolve format based on TTY detection
        OutputFormat format = resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Available disc controllers:\n";
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << "  " << info.id << " - " << info.display_name
                              << " (" << info.fdc_chip << ")\n"
                              << "      " << info.description << "\n";
                }
                std::cout << "  none - No disc controller (leave socket empty)\n";
                break;

            case OutputFormat::Tsv:
                std::cout << "id\tdisplay_name\tfdc_chip\tdescription\n";
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << info.id << "\t" << info.display_name << "\t"
                              << info.fdc_chip << "\t" << info.description << "\n";
                }
                std::cout << "none\tNo controller\t-\tLeave socket empty (no disc)\n";
                break;

            case OutputFormat::Jsonl:
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << "{\"id\":\"" << info.id
                              << "\",\"display_name\":\"" << info.display_name
                              << "\",\"fdc_chip\":\"" << info.fdc_chip
                              << "\",\"description\":\"" << info.description << "\"}\n";
                }
                std::cout << "{\"id\":\"none\",\"display_name\":\"No controller\","
                          << "\"fdc_chip\":\"-\",\"description\":\"Leave socket empty (no disc)\"}\n";
                break;

            case OutputFormat::Auto:
                // Should not reach here after resolve_output_format
                break;
        }
        return ExitCode::OK;
    }
};

// DescribeMachine subcommand - output machine info
template<typename MachineType>
class DescribeMachineSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-machine"; }
    std::string_view description() const override { return "Output machine information"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-machine\n"
                  << "\n"
                  << "Outputs machine information for programmatic use.\n"
                  << "Includes machine type, default ROMs, and version information.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help for this subcommand
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            // Unknown argument
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Resolve format based on TTY detection
        OutputFormat format = resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Machine:        " << Memory::MACHINE_DISPLAY_NAME << "\n"
                          << "Executable:     " << argv[0] << "\n"
                          << "Version:        " << BEEBIUM_VERSION << "\n"
                          << "MOS ROM:        " << Memory::DEFAULT_MOS_ROM << "\n"
                          << "Language ROM:   " << Memory::DEFAULT_LANGUAGE_ROM
                          << " (slot " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT) << ")\n";
                if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
                    std::cout << "DFS ROM:        " << Memory::DEFAULT_DFS_ROM
                              << " (slot " << static_cast<int>(Memory::DEFAULT_DFS_SLOT) << ")\n";
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "key\tvalue\n"
                          << "machine_type\t" << Memory::MACHINE_TYPE << "\n"
                          << "display_name\t" << Memory::MACHINE_DISPLAY_NAME << "\n"
                          << "executable\t" << argv[0] << "\n"
                          << "version\t" << BEEBIUM_VERSION << "\n"
                          << "default_mos_rom\t" << Memory::DEFAULT_MOS_ROM << "\n"
                          << "default_language_rom\t" << Memory::DEFAULT_LANGUAGE_ROM << "\n"
                          << "default_language_slot\t" << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT) << "\n";
                if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
                    std::cout << "default_dfs_rom\t" << Memory::DEFAULT_DFS_ROM << "\n"
                              << "default_dfs_slot\t" << static_cast<int>(Memory::DEFAULT_DFS_SLOT) << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                std::cout << "{\"executable\":\"" << argv[0]
                          << "\",\"machine_type\":\"" << Memory::MACHINE_TYPE
                          << "\",\"display_name\":\"" << Memory::MACHINE_DISPLAY_NAME
                          << "\",\"version\":\"" << BEEBIUM_VERSION
                          << "\",\"default_mos_rom\":\"" << Memory::DEFAULT_MOS_ROM
                          << "\",\"default_language_rom\":\"" << Memory::DEFAULT_LANGUAGE_ROM
                          << "\",\"default_language_slot\":" << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT);
                if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
                    std::cout << ",\"default_dfs_rom\":\"" << Memory::DEFAULT_DFS_ROM
                              << "\",\"default_dfs_slot\":" << static_cast<int>(Memory::DEFAULT_DFS_SLOT);
                }
                std::cout << "}\n";
                break;

            case OutputFormat::Auto:
                // Should not reach here after resolve_output_format
                break;
        }
        return ExitCode::OK;
    }
};

// DescribePresetSchema subcommand - output preset configuration schema as JSON
template<typename MachineType>
class DescribePresetSchemaSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-preset-schema"; }
    std::string_view description() const override { return "Output preset configuration schema as JSON"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-preset-schema\n"
                  << "\n"
                  << "Outputs JSON describing the machine's configurable options for presets.\n"
                  << "This is used by frontends to dynamically build configuration UI.\n"
                  << "\n"
                  << "Output includes:\n"
                  << "  - schema_version: Schema format version\n"
                  << "  - model: Machine identification (id, name, description)\n"
                  << "  - sections: Array of configuration sections (storage, etc.)\n"
                  << "\n"
                  << "Output is always JSON regardless of --format option.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help for this subcommand
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            // Unknown argument
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Build JSON output using ordered_json to preserve key insertion order
        // (standard json sorts keys alphabetically, making output harder to read)
        using ojson = nlohmann::ordered_json;

        ojson output;
        output["schema_version"] = 1;
        output["model"] = ojson{
            {"id", Memory::MACHINE_TYPE},
            {"name", Memory::MACHINE_DISPLAY_NAME},
            {"description", Memory::MACHINE_DESCRIPTION}
        };

        // Build storage section - "type" first for readability
        ojson storage;
        storage["type"] = "storage";

        // Built-in hardware - all current models have cassette
        storage["builtin"]["cassette"] = true;

        if constexpr (HasDiscControllerSocket<Memory>) {
            // Model B: socketed FDC, no built-in controller
            storage["builtin"]["fdc"] = nullptr;

            // List available FDC options for the socket - "id" first in each option
            ojson fdc_options = ojson::array();
            fdc_options.push_back(ojson{
                {"id", "none"},
                {"label", "Empty"},
                {"device", nullptr},
                {"description", "No disc controller"}
            });
            for (const auto& info : DiscControllerRegistry::available()) {
                fdc_options.push_back(ojson{
                    {"id", info.id},
                    {"label", info.display_name},
                    {"device", info.fdc_chip},
                    {"description", info.description}
                });
            }
            storage["fdc_socket"]["options"] = fdc_options;
        } else if constexpr (HasDiscDrives<Memory>) {
            // Model B+: built-in FDC, no socket
            storage["builtin"]["fdc"] = ojson{
                {"device", "WD1770"},
                {"label", "Built-in WD1770"}
            };
            // No fdc_socket key for machines with built-in FDC
        }

        // Floppy drives (if hardware has them) - "drive" first in each entry
        if constexpr (HasDiscDrives<Memory>) {
            storage["floppy_drives"] = ojson::array({
                ojson{{"drive", 0}, {"tracks", {40, 80}}, {"sides", 2}, {"image_types", {"ssd", "dsd", "adf", "adl"}}},
                ojson{{"drive", 1}, {"tracks", {40, 80}}, {"sides", 2}, {"image_types", {"ssd", "dsd", "adf", "adl"}}}
            });
        }

        output["sections"] = ojson::array({storage});

        std::cout << output.dump(2) << "\n";
        return ExitCode::OK;
    }
};

// Help subcommand - shows help for other subcommands
template<typename MachineType>
class HelpSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "help"; }
    std::string_view description() const override { return "Show help for a subcommand"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " help [subcommand]\n"
                  << "\n"
                  << "Shows help for the specified subcommand, or global help if none specified.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override;  // Defined after get_subcommands
};

// ============================================================================
// Subcommand registry and dispatch
// ============================================================================

// Get the singleton registry of all subcommands
template<typename MachineType>
const std::vector<std::unique_ptr<Subcommand<MachineType>>>& get_subcommands() {
    static auto cmds = []() {
        std::vector<std::unique_ptr<Subcommand<MachineType>>> v;
        v.push_back(std::make_unique<StartSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListFdcsSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeMachineSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribePresetSchemaSubcommand<MachineType>>());
        v.push_back(std::make_unique<HelpSubcommand<MachineType>>());
        return v;
    }();
    return cmds;
}

// HelpSubcommand::invoke implementation (needs get_subcommands)
template<typename MachineType>
int HelpSubcommand<MachineType>::invoke(int argc, char* argv[], const GlobalConfig& global) const {
    // Check for target subcommand name in arguments
    std::string target_name;
    for (int i = global.subcommand_argv_start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            // help --help shows help for the help command
            help(argv[0]);
            return ExitCode::OK;
        }
        if (!arg.empty() && arg[0] != '-') {
            target_name = std::string(arg);
            break;
        }
    }

    if (target_name.empty()) {
        // No target: show global help
        print_global_help<MachineType>(argv[0]);
        return ExitCode::OK;
    }

    // Find and show help for the target subcommand
    for (const auto& cmd : get_subcommands<MachineType>()) {
        if (cmd->name() == target_name) {
            cmd->help(argv[0]);
            return ExitCode::OK;
        }
    }

    std::cerr << "Unknown subcommand: " << target_name << "\n";
    print_global_help<MachineType>(argv[0]);
    return ExitCode::USAGE;
}

// Print global help (lists all subcommands)
template<typename MachineType>
void print_global_help(const char* program_name) {
    using Memory = typename MachineType::Memory;

    std::cerr << "Usage: " << program_name << " [global-options] <subcommand> [subcommand-options]\n"
              << "\n"
              << "Beebium " << BEEBIUM_VERSION << " - " << Memory::MACHINE_DISPLAY_NAME << " emulator server\n"
              << "\n"
              << "Global options:\n"
              << "  --help, -h          Show this help message\n"
              << "  --format <format>   Output format for data commands:\n"
              << "                      pretty - human-friendly (default if TTY)\n"
              << "                      tsv    - tab-separated values (default if not TTY)\n"
              << "                      jsonl  - JSON Lines (one JSON object per line)\n"
              << "\n"
              << "Subcommands:\n";

    for (const auto& cmd : get_subcommands<MachineType>()) {
        std::cerr << "  " << cmd->name();
        // Pad to align descriptions
        for (size_t i = cmd->name().length(); i < 18; ++i) {
            std::cerr << ' ';
        }
        std::cerr << cmd->description() << "\n";
    }

    std::cerr << "\n"
              << "Use '" << program_name << " <subcommand> --help' for subcommand options.\n"
              << "Use '" << program_name << " help <subcommand>' for subcommand help.\n"
              << "\n"
              << "If no subcommand is specified, 'start' is assumed.\n";
}

// Dispatch to the appropriate subcommand
template<typename MachineType>
int dispatch_subcommand(int argc, char* argv[], const GlobalConfig& global) {
    // Determine subcommand name (default to "start")
    std::string subcommand_name = global.subcommand_name;
    if (subcommand_name.empty()) {
        // No subcommand specified
        if (global.help_requested) {
            // Global --help with no subcommand
            print_global_help<MachineType>(argv[0]);
            return ExitCode::OK;
        }
        subcommand_name = "start";  // Default subcommand
    }

    // Find and invoke the subcommand
    for (const auto& cmd : get_subcommands<MachineType>()) {
        if (cmd->name() == subcommand_name) {
            return cmd->invoke(argc, argv, global);
        }
    }

    // Unknown subcommand
    std::cerr << "Unknown subcommand: " << subcommand_name << "\n";
    print_global_help<MachineType>(argv[0]);
    return ExitCode::USAGE;
}

// ============================================================================
// Main entry point
// ============================================================================

template<typename MachineType>
int server_main(int argc, char* argv[]) {
    // Parse global arguments
    GlobalConfig global;
    if (auto exit_code = parse_global_arguments(argc, argv, global)) {
        return *exit_code;
    }

    // Dispatch to appropriate subcommand
    return dispatch_subcommand<MachineType>(argc, argv, global);
}

// ============================================================================
// Legacy function for backwards compatibility with tests
// ============================================================================

// Parse command-line arguments into a ServerConfig struct.
// This function is provided for backwards compatibility with existing tests.
// New code should use the subcommand-based architecture.
// Returns:
//   - std::nullopt on success (continue execution)
//   - 0 for --help (exit successfully)
//   - 1 for errors (exit with error)
template<typename MachineType>
std::optional<int> parse_arguments(int argc, char* argv[], ServerConfig<MachineType>& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage<MachineType>(argv[0]);
            return 0;
        } else if (arg == "--mos" && i + 1 < argc) {
            config.mos_filepath = argv[++i];
        } else if (arg == "--sideways" && i + 1 < argc) {
            std::string value = argv[++i];
            complete_colon_arg(value, i, argc, argv);
            auto sideways_config = parse_sideways_arg(value);
            config.sideways_configs.push_back(sideways_config);
            if (sideways_config.type == SidewaysSlotType::Rom) {
                config.rom_slots[sideways_config.slot] = sideways_config.image_filepath;
            } else if (sideways_config.type == SidewaysSlotType::Empty) {
                config.rom_slots[sideways_config.slot] = EMPTY_SLOT_MARKER;
            } else if (sideways_config.type == SidewaysSlotType::Ram) {
                config.rom_slots[sideways_config.slot] = RAM_SLOT_MARKER;
            }
        } else if (arg == "--rom-dir" && i + 1 < argc) {
            config.rom_dirpath = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                config.port = static_cast<uint16_t>(parse_int(argv[++i], "--port"));
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return 1;
            }
        } else if (arg == "--floppy" && i + 1 < argc) {
            std::string value = argv[++i];
            complete_colon_arg(value, i, argc, argv);
            auto [drive, filepath] = parse_floppy_arg(value);
            config.floppy_filepaths[drive] = filepath;
        } else if (arg == "--screen-mode" && i + 1 < argc) {
            try {
                config.screen_mode = parse_int(argv[++i], "--screen-mode");
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return 1;
            }
            if (config.screen_mode < 0 || config.screen_mode > 7) {
                std::cerr << "Error: --screen-mode must be 0-7\n";
                return 1;
            }
            config.screen_mode_set = true;
        } else if (arg == "--auto-boot") {
            config.auto_boot = true;
            config.auto_boot_set = true;
        } else if (arg == "--links" && i + 1 < argc) {
            try {
                config.raw_links = parse_int(argv[++i], "--links");
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return 1;
            }
            if (config.raw_links < 0 || config.raw_links > 255) {
                std::cerr << "Error: --links must be 0-255\n";
                return 1;
            }
        } else if (arg == "--wait") {
            config.wait_mode = platform::is_stdin_tty() ? WaitMode::Cli : WaitMode::Api;
        } else if (arg.rfind("--wait=", 0) == 0) {
            std::string wait_value = arg.substr(7);
            config.wait_mode = parse_wait_arg(wait_value);
        } else if (arg == "--fdc" && i + 1 < argc) {
            config.fdc_type = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage<MachineType>(argv[0]);
            return 1;
        }
    }

    return std::nullopt;
}

} // namespace beebium::server

#endif // BEEBIUM_SERVER_SERVER_MAIN_HPP
