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

#include "BuiltinExtensions.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/service/EconetTransportService.hpp"
#include "beebium/service/ExtensionUiService.hpp"
#include "beebium/extension/ExtensionArgParser.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/OneMHzBusPort.hpp"
#include "beebium/extension/PluginLoader.hpp"
#include "beebium/service/PeripheralExtensionService.hpp"
#include "SecondProcessor65C02Extension.hpp"
#include "beebium/Machines.hpp"
#include "beebium/PacingClock.hpp"
#include "beebium/SleepQuantum.hpp"
#include "beebium/disc/DiscLoader.hpp"
#include "beebium/disc/DiscDrive.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"
#include "beebium/disc/DiscConcepts.hpp"
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/econet/TestBackend.hpp"
#include "beebium/tube/TubeConcepts.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/server/PresetLoader.hpp"
#include "beebium/server/PresetPaths.hpp"
#include "beebium/server/RomPaths.hpp"

#include <nlohmann/json.hpp>
#include "stb_image_write.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cerrno>
#include <chrono>
#include "beebium/server/Platform.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
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

    // Econet configuration
    int station_number = -1;                          // -1 = Econet not fitted
    // Both AUN (built-in) and Piconet (plugin) are selected via the
    // generic extension dispatch:
    //
    //   --aun     port=...:map=...
    //   --piconet device_path=/dev/ttyX
    //
    // ServerConfig has no transport-specific fields; the values live
    // in the relevant ExtensionInstance::config map and are consumed
    // by the corresponding EconetTransportExtension::create_backend.

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

    // Extension configuration
    std::string extension_dirpath;

    // Parsed extension instances with config (from --<cli-name> flags)
    struct ExtensionInstance {
        std::string name;                                               // canonical extension name
        std::map<std::string, std::string> config;                      // parsed KV pairs (scalar params)
        std::map<std::string, std::vector<std::string>> list_config;    // list params (is_list=true)
    };
    std::vector<ExtensionInstance> extension_instances;
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

    if constexpr (HasEconetSocket<Memory>) {
        std::cerr << "  --station <1-254>        Econet station number (enables Econet)\n"
                  << "  --aun [port=<n>][:map=<net.stn@ip@port>]...\n"
                  << "                           AUN UDP transport (default: port="
                  << beebium::AUN_DEFAULT_PORT << ").\n"
                  << "                           port=none disables the network.\n"
                  << "                           map= entries are repeatable; the inner\n"
                  << "                           separator is ';' (not ':').\n"
                  << "  --piconet device_path=/dev/ttyX\n"
                  << "                           Piconet USB-CDC bridge to a real Econet wire\n"
                  << "                           (POSIX-only; provided by the piconet plugin).\n"
                  << "                           Mutually exclusive with --aun.\n";
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
              << "  --extension-dir <path>   Directory containing extension plugins\n"
              << "                           Enables --<extension> flags (see below)\n"
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

    // Econet
    if (preset.econet) {
        const auto& econet = *preset.econet;
        config.station_number = econet.station;

        // Transport: convert preset.econet.transport (if present) into an
        // ExtensionInstance so it flows through the same dispatch as a
        // CLI --aun ... or --piconet ... flag. AUN is built-in; Piconet
        // is a plugin; the dispatcher routes by name.
        if (econet.transport) {
            typename ServerConfig<MachineType>::ExtensionInstance inst;
            inst.name = econet.transport->name;
            inst.config = econet.transport->parameters;
            inst.list_config = econet.transport->list_parameters;
            config.extension_instances.push_back(std::move(inst));
        }
    }

    // Extensions (from preset, applied as baseline; CLI can add more)
    for (const auto& ext : preset.extensions) {
        typename ServerConfig<MachineType>::ExtensionInstance inst;
        inst.name = ext.name;
        inst.config = ext.config;
        inst.list_config = ext.list_config;
        config.extension_instances.push_back(std::move(inst));
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

    // First pass: find --preset and --extension-dir before other options
    // --preset is loaded first so CLI can override; --extension-dir is needed
    // to scan manifests so extension CLI flags can be recognised in second pass
    for (int i = start_index; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--extension-dir" && i + 1 < argc) {
            config.extension_dirpath = argv[i + 1];
            ++i;
            continue;
        }
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

    // If --extension-dir not specified, derive a default from the
    // executable path: <exe-dir>/extensions/<plugin>/manifest.json.
    // beebium_finalize_plugin() (cmake/BeebiumPlugin.cmake) post-build-
    // copies every dynamic plugin to this layout so the same rule works
    // on every platform and every build configuration -- including
    // MSBuild / Xcode multi-config where the exe sits in a $(Configuration)
    // subdirectory. The canonical install layout mirrors this: plugins
    // ship alongside the server binary.
    if (config.extension_dirpath.empty()) {
        std::error_code ec;
        auto exe_path = std::filesystem::canonical(std::filesystem::path(argv[0]), ec);
        if (!ec) {
            auto default_ext_dirpath = exe_path.parent_path() / "extensions";
            if (std::filesystem::exists(default_ext_dirpath)) {
                config.extension_dirpath = default_ext_dirpath.string();
            }
        }
    }

    // Built-in extension manifests so the CLI parser recognises them.
    const auto& builtin_entries = beebium::builtin_extensions::entries();

    // Normalise an extension CLI flag to lowercase for case-insensitive matching.
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    };

    // Scan plugin extension manifests so we can recognise --<cli-name> flags in the second pass
    std::vector<beebium::ExtensionManifest> scanned_manifests;
    std::map<std::string, const beebium::ExtensionManifest*> cli_name_to_manifest;
    for (const auto& e : builtin_entries) {
        cli_name_to_manifest[to_lower("--" + std::string(e.manifest.effective_cli_name()))]
            = &e.manifest;
    }
    if (!config.extension_dirpath.empty()) {
        beebium::PluginLoader scanner;
        scanned_manifests = scanner.scan_manifests(config.extension_dirpath);
        for (const auto& m : scanned_manifests) {
            std::string cli = std::string(m.effective_cli_name());
            cli_name_to_manifest[to_lower("--" + cli)] = &m;
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
        } else if (arg == "--station" && i + 1 < argc) {
            try {
                config.station_number = parse_int(argv[++i], "--station");
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            if (config.station_number < 1 || config.station_number > 254) {
                std::cerr << "Error: --station must be 1-254\n";
                return ExitCode::USAGE;
            }
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
        } else if (arg == "--extension-dir" && i + 1 < argc) {
            // Already handled in first pass, skip
            ++i;
            continue;
        } else {
            // Check if this is an extension CLI flag (e.g. --acorn-scsi, --scsi-hdd)
            auto manifest_it = cli_name_to_manifest.find(to_lower(arg));
            if (manifest_it != cli_name_to_manifest.end()) {
                const auto* manifest = manifest_it->second;
                std::string arg_string;
                // Consume the next argv as the colon-separated argument string
                // (if it exists and doesn't start with --)
                if (i + 1 < argc && std::string_view(argv[i + 1]).substr(0, 2) != "--") {
                    arg_string = argv[++i];
                }
                auto parse_result = beebium::parse_extension_args(
                    manifest->effective_cli_name(), arg_string, manifest->parameters);
                if (!parse_result.ok) {
                    std::cerr << "Error: " << parse_result.error << "\n";
                    return ExitCode::USAGE;
                }
                config.extension_instances.push_back({
                    manifest->name,
                    std::move(parse_result.config),
                    std::move(parse_result.list_config)
                });
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                print_usage<MachineType>(argv[0]);
                return ExitCode::USAGE;
            }
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

    // Econet transports are mutually exclusive. Both AUN and Piconet
    // are now extension instances; mutex enforcement lives in
    // install_econet (which sees the EconetTransportRegistry).

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

        if (rom_data.empty() || rom_data.size() > 16384
            || !std::has_single_bit(rom_data.size())) {
            std::cerr << "Warning: ROM is " << rom_data.size()
                      << " bytes, expected a power-of-two size up to 16384\n";
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
                          << "Use the list-fdcs command to see available disc controllers.\n";
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

// Install Econet hardware for machines with Econet sockets.
// Returns exit code on error, nullopt on success.
//
// transport_registry is populated by start() from any econet-transport
// extension instances; if it contains a transport, install_econet uses
// it in preference to the legacy --aun-port / --piconet flags. Phase 2
// keeps the legacy paths working as a transitional measure; phase 3
// removes them.
template<typename MachineType>
std::optional<int> install_econet(MachineType& machine,
                                   const ServerConfig<MachineType>& config,
                                   beebium::EconetTransportRegistry& transport_registry) {
    using Memory = typename MachineType::Memory;

    if constexpr (HasEconetSocket<Memory>) {
        if (config.station_number >= 1) {
            auto station = static_cast<uint8_t>(config.station_number);

            // Prefer an econet-transport extension if one was configured.
            // BBC machine policy: at most one transport. (Future Acorn
            // Econet Bridge machine type would relax this.)
            if (!transport_registry.empty()) {
                if (transport_registry.size() > 1) {
                    std::cerr << "Error: BBC machines support at most one Econet "
                                 "transport (got " << transport_registry.size() << ").\n";
                    return 1;
                }
                auto& transport = *transport_registry.extensions().front();
                auto backend = transport.create_backend(station);
                if (backend) {
                    std::cout << "Econet station " << config.station_number
                              << " via transport extension '"
                              << transport.name() << "'\n";
                    machine.state().memory.econet_socket.enable(
                        station, std::move(backend),
                        true);  // FourWayHandshake active
                } else {
                    // Transport configured but unavailable (e.g. port=none,
                    // bind failed): install a disconnected TestBackend so
                    // the ADLC reports "no clock" cleanly.
                    auto stub = std::make_unique<beebium::TestBackend>();
                    stub->set_connected(false);
                    machine.state().memory.econet_socket.enable(
                        station, std::move(stub), true);
                    std::cout << "Econet station " << config.station_number
                              << " (transport '" << transport.name()
                              << "' configured but unavailable -- no network)\n";
                }
                return std::nullopt;
            }

            // No transport configured: Econet hardware fitted but no
            // network connection. The ADLC reports "no clock" so the
            // NFS ROM behaves as if the cable is unplugged.
            auto backend = std::make_unique<beebium::TestBackend>();
            backend->set_connected(false);
            machine.state().memory.econet_socket.enable(
                station, std::move(backend),
                true);  // aun_mode = true (FourWayHandshake wraps disconnected backend)

            std::cout << "Econet station " << config.station_number
                      << " (no network -- pass --aun port=N or --piconet device_path=... to enable a transport)\n";
        }
    } else if (config.station_number >= 1) {
        std::cerr << "Warning: --station option ignored (machine has no Econet socket)\n";
    }

    return std::nullopt;
}

// Load a single disc image into a floppy drive.
// Returns an exit code on error, or std::nullopt on success.
inline std::optional<int> load_single_disc(DiscDrive& drive, int drive_num,
                                            const std::string& filepath_or_url) {
    // Resolve filepath to canonical path, checking existence
    std::filesystem::path filepath(filepath_or_url);
    if (filepath_or_url.find("://") == std::string::npos) {
        // Bare filepath: check existence before trying to canonicalize
        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec)) {
            std::cerr << "Error: disc image not found: " << filepath_or_url << "\n";
            return ExitCode::NOINPUT;
        }
        filepath = std::filesystem::canonical(filepath, ec);
        if (ec) {
            std::cerr << "Error: cannot resolve disc image path: " << filepath_or_url
                      << " (" << ec.message() << ")\n";
            return ExitCode::NOINPUT;
        }
    }

    auto source_url = "file://" + filepath.string();
    std::cout << "Loading disc into floppy " << drive_num << ": " << filepath.string() << "\n";

    auto result = load_disc_from_url_or_filepath(filepath.string());
    if (!result) {
        std::cerr << "Error: " << result.error << "\n";
        return ExitCode::DATAERR;
    }

    if (result.disc->is_write_protected()) {
        std::cout << "  (write-protected)\n";
    }
    std::cout << "  format: " << result.disc->format_name() << "\n";

    drive.insert(std::move(result.disc), source_url);
    return std::nullopt;
}

// Load disc images into floppy drives.
// Returns an exit code on error, or std::nullopt on success.
template<typename MachineType>
std::optional<int> load_disc_images(MachineType& machine, const ServerConfig<MachineType>& config) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        if (!config.floppy_filepaths[0].empty()) {
            if (auto exit_code = load_single_disc(
                    machine.state().memory.disc_drive_0, 0, config.floppy_filepaths[0])) {
                return exit_code;
            }
        }

        if (!config.floppy_filepaths[1].empty()) {
            if (auto exit_code = load_single_disc(
                    machine.state().memory.disc_drive_1, 1, config.floppy_filepaths[1])) {
                return exit_code;
            }
        }
    }
    return std::nullopt;
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

    // Create platform-specific sleep and measure its quantum
    PlatformSleep sleeper;
    auto quantum = beebium::measure_sleep_quantum(sleeper);

    // Create and start pacing clock with measured quantum
    PacingClock pacing_clock(Memory::default_pacing_config(), quantum,
                             std::move(sleeper));

    if (use_pacing) {
        pacing_clock.start();
        std::cout << "Pacing: quantum " << quantum.count() / 1000 << " us"
                  << " (" << 1'000'000'000 / quantum.count() << " Hz)"
                  << ", nominal " << pacing_clock.config().base_clock_hz / 1'000'000 << " MHz"
                  << "\n";
    } else {
        std::cout << "Pacing: DISABLED (BEEBIUM_NO_PACING set)\n";
    }

    // Wire pacing clock to gRPC SystemService for stats monitoring
    if (use_pacing) {
        server.set_pacing_clock(&pacing_clock);
    }

    // Set up shutdown callbacks so signal handler can interrupt blocked waits
    // and notify clients of impending shutdown
    g_request_machine_shutdown = [&machine]() { machine.request_shutdown(); };
    g_request_pacing_stop = [&pacing_clock]() { pacing_clock.request_stop(); };
    g_notify_clients_shutdown = [&server]() { server.notify_shutdown(5000); };

    // Main emulation loop
    constexpr uint64_t cycles_per_frame = 40000;  // For non-paced mode
    auto last_stats_time = std::chrono::steady_clock::now();
    uint64_t last_cycle_count = machine.cycle_count();
    std::chrono::steady_clock::duration run_duration{};
    // Reset VSYNC edge counter for frequency measurement
    machine.memory().system_via_peripheral.consume_vsync_rising_edges();
    while (g_running) {
        // Check for pending SIGINT/SIGTERM and dispatch shutdown callback.
        // The signal handler only sets an atomic flag (async-signal-safe);
        // the actual shutdown work (notifying CVs, etc.) happens here
        // in a normal context where mutex operations are safe.
        platform::dispatch_pending_signal();

        // Block if debugger has paused execution
        machine.wait_if_paused();

        // Check if shutdown was requested during wait
        if (machine.shutdown_requested()) {
            break;
        }

        if (use_pacing) {
            pacing_clock.wait_for_tick();
            uint64_t cycles = pacing_clock.cycles_for_next_tick();
            auto run_start = std::chrono::steady_clock::now();
            machine.run(cycles);
            run_duration += std::chrono::steady_clock::now() - run_start;
            pacing_clock.report_cycles(machine.cycle_count());
        } else {
            auto run_start = std::chrono::steady_clock::now();
            machine.run(cycles_per_frame);
            run_duration += std::chrono::steady_clock::now() - run_start;
        }

        // Periodic pacing stats (every 5 seconds)
        if (use_pacing) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time);
            if (elapsed.count() >= 5) {
                auto stats = pacing_clock.timing_stats();
                uint64_t current_cycles = machine.cycle_count();
                uint64_t cycles_delta = current_cycles - last_cycle_count;
                double elapsed_secs = std::chrono::duration<double>(now - last_stats_time).count();
                double actual_hz = static_cast<double>(cycles_delta) / elapsed_secs;
                double target_hz = static_cast<double>(Memory::default_pacing_config().base_clock_hz);
                uint32_t vsync_edges = machine.memory().system_via_peripheral.consume_vsync_rising_edges();
                double vsync_hz = static_cast<double>(vsync_edges) / elapsed_secs;
                double run_secs = std::chrono::duration<double>(run_duration).count();
                double run_pct = 100.0 * run_secs / elapsed_secs;
                std::cerr << "Pacing: "
                          << std::fixed << std::setprecision(3)
                          << (actual_hz / 1e6) << " MHz"
                          << " (target " << (target_hz / 1e6) << " MHz, "
                          << std::setprecision(1)
                          << (100.0 * actual_hz / target_hz) << "%)"
                          << " | vsync " << std::setprecision(1) << vsync_hz << " Hz"
                          << " | io " << stats.ticks_io_woken
                          << " | deficit " << std::setprecision(0) << stats.controller_deficit
                          << " | run " << std::setprecision(1) << run_pct << "%"
                          << "\n";
                last_stats_time = now;
                last_cycle_count = current_cycles;
                run_duration = {};
            }
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

            // Resolve the machine identity UUID up front so transport
            // extensions can pick it up via inst.config["machine_uuid"]
            // before their create_backend runs. The MachineIdentity
            // proper is constructed later, but the UUID (the only
            // value extensions need today) is fixed once it's been
            // generated.
            if (config.machine_uuid.empty()) {
                config.machine_uuid = generate_uuid_v4();
            }

            // Plugin manifests have to be scanned now (not later in the
            // peripheral setup) because econet-transport plugins like
            // piconet need to be constructed before install_econet so
            // their backend is ready when the machine reads the ADLC.
            beebium::PluginLoader plugin_loader;
            std::vector<beebium::ExtensionManifest> plugin_manifests;
            if (!config.extension_dirpath.empty()) {
                std::cout << "Extension directory: " << config.extension_dirpath << "\n";
                plugin_manifests = plugin_loader.scan_manifests(config.extension_dirpath);
            }

            // Build the Econet transport registry from any econet-transport
            // extension instances. Done before install_econet so the
            // transport is ready when the machine first reads the ADLC.
            // Transport-extension instances are removed from
            // config.extension_instances; the later peripheral-extension
            // load loop only sees peripherals.
            beebium::EconetTransportRegistry transport_registry;
            {
                std::vector<typename ServerConfig<MachineType>::ExtensionInstance> remaining;
                remaining.reserve(config.extension_instances.size());
                for (auto& inst : config.extension_instances) {
                    // Locate the manifest: built-ins first, then scanned plugins.
                    const auto* entry = beebium::builtin_extensions::find(inst.name);
                    const beebium::ExtensionManifest* manifest =
                        entry ? &entry->manifest
                              : beebium::PluginLoader::find_manifest(plugin_manifests, inst.name);

                    if (!manifest || manifest->extension_kind != "econet-transport") {
                        remaining.push_back(std::move(inst));
                        continue;
                    }

                    if (inst.config.find("id") == inst.config.end()) {
                        inst.config["id"] = generate_uuid_v4();
                    }
                    // Inject the machine identity UUID under a
                    // framework-reserved key. The AUN extension reads
                    // this and surfaces it as the impl-identity TXT
                    // record on its mDNS announcement so a discovering
                    // peer can correlate the AUN announcement with the
                    // matching _beebium._tcp gRPC announcement (which
                    // already publishes the same UUID under "uuid").
                    inst.config["machine_uuid"] = config.machine_uuid;
                    // Normalise scalar-form list params from presets into list_config
                    // so the extension only sees one source of truth.
                    normalise_list_params(inst.config, inst.list_config, manifest->parameters);
                    std::cout << "Loading transport: " << manifest->name
                              << " (id=" << inst.config["id"] << ")\n";
                    for (const auto& [k, v] : inst.config) {
                        if (k != "id" && k != "machine_uuid") {
                            std::cout << "  " << k << "=" << v << "\n";
                        }
                    }
                    for (const auto& [k, vs] : inst.list_config) {
                        for (const auto& v : vs) {
                            std::cout << "  " << k << "=" << v << "\n";
                        }
                    }

                    std::unique_ptr<beebium::Extension> loaded;
                    if (entry) {
                        loaded = entry->factory();
                        loaded->set_manifest(entry->manifest);
                        loaded->set_config(std::move(inst.config));
                        loaded->set_list_config(std::move(inst.list_config));
                    } else {
                        loaded = plugin_loader.load_extension(
                            *manifest, std::move(inst.config), std::move(inst.list_config));
                    }

                    auto* transport = dynamic_cast<beebium::EconetTransportExtension*>(
                        loaded.get());
                    if (!transport) {
                        std::cerr << "Error: extension '" << manifest->name
                                  << "' has extension_kind=econet-transport but is "
                                     "not an EconetTransportExtension instance.\n";
                        return 1;
                    }
                    (void)loaded.release();
                    transport_registry.add(
                        std::unique_ptr<beebium::EconetTransportExtension>(transport));
                }
                config.extension_instances = std::move(remaining);
            }

            // Install Econet hardware
            if (auto exit_code = install_econet(machine, config, transport_registry)) {
                return *exit_code;
            }

            // Load disc images
            if (auto exit_code = load_disc_images(machine, config)) {
                return *exit_code;
            }

            // Enable video output
            machine.state().memory.enable_video_output();

            // Enable audio output
            machine.state().memory.enable_audio_output();

            // Apply startup options before reset
            apply_startup_options(machine, config);

            // Reset machine
            machine.reset();

            // Set up peripheral extension registry. plugin_loader and
            // plugin_manifests already exist from the earlier scan that
            // populated the transport registry.
            beebium::ExtensionRegistry extension_registry;
            if constexpr (beebium::HasOneMHzBus<Memory>) {
                extension_registry.register_extension_point("1mhz-bus");
            }
            if constexpr (beebium::HasUserPort<Memory>) {
                extension_registry.register_extension_point("user-port");
            }
            if constexpr (beebium::HasTubeSocket<Memory>) {
                extension_registry.register_extension_point("tube");
            }

            // Note: extensions are loaded below from --<cli-name> flags
            // via the plugin path. test-scratch-ram used to be
            // unconditionally auto-registered here as a built-in; it is
            // now a plugin and users opt in via --test-scratch-ram on
            // the CLI (or via a preset that lists it).

            // Load extensions (from --<cli-name> flags). plugin_loader and
            // plugin_manifests were already populated above (so the
            // transport registry could be built before machine.reset()).
            // Sort extension instances so providers (those with "provides" in
            // their manifest) are loaded before consumers. This ensures
            // RTLD_GLOBAL takes effect before child plugins try to resolve
            // parent symbols. Uses stable_partition to preserve command-line
            // order within each group.
            std::stable_partition(
                config.extension_instances.begin(),
                config.extension_instances.end(),
                [&plugin_manifests](const auto& inst) {
                    auto* m = beebium::PluginLoader::find_manifest(
                        plugin_manifests, inst.name);
                    return m && !m->provides.empty();
                });

            for (auto& inst : config.extension_instances) {
                // Assign instance ID if not provided
                if (inst.config.find("id") == inst.config.end()) {
                    inst.config["id"] = generate_uuid_v4();
                }

                // Resolve manifest first (built-in preferred), so we can
                // normalise scalar-form list params from presets before
                // logging / handing to the extension.
                const beebium::ExtensionManifest* manifest = nullptr;
                const beebium::builtin_extensions::Entry* entry =
                    beebium::builtin_extensions::find(inst.name);
                if (entry) {
                    manifest = &entry->manifest;
                } else {
                    manifest = beebium::PluginLoader::find_manifest(plugin_manifests, inst.name);
                    if (!manifest) {
                        std::cerr << "Error: Extension '" << inst.name << "' not found";
                        if (!config.extension_dirpath.empty()) {
                            std::cerr << " in " << config.extension_dirpath;
                        }
                        std::cerr << "\n";
                        return ExitCode::CONFIG;
                    }
                }
                normalise_list_params(inst.config, inst.list_config, manifest->parameters);

                std::cout << "Loading extension: " << inst.name;
                if (inst.config.count("id")) {
                    std::cout << " (id=" << inst.config["id"] << ")";
                }
                std::cout << "\n";
                for (const auto& [key, value] : inst.config) {
                    if (key != "id") {
                        std::cout << "  " << key << "=" << value << "\n";
                    }
                }
                for (const auto& [key, values] : inst.list_config) {
                    for (const auto& v : values) {
                        std::cout << "  " << key << "=" << v << "\n";
                    }
                }

                // Construct the extension instance: built-in factories take
                // precedence over scanned plugins. Both yield a generic
                // unique_ptr<Extension> which then dispatches by
                // manifest.extension_kind.
                std::unique_ptr<beebium::Extension> loaded;
                if (entry) {
                    loaded = entry->factory();
                    loaded->set_manifest(entry->manifest);
                    loaded->set_config(std::move(inst.config));
                    loaded->set_list_config(std::move(inst.list_config));
                } else {
                    loaded = plugin_loader.load_extension(
                        *manifest, std::move(inst.config), std::move(inst.list_config));
                }

                // Dispatch by extension_kind. Phase 1/2 supports peripherals
                // only; econet-transport dispatch lands in phase 2 follow-up.
                if (manifest->extension_kind == "peripheral") {
                    auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
                    if (!peripheral) {
                        std::cerr << "Error: Extension '" << inst.name
                                  << "' has extension_kind=peripheral but is not a "
                                     "PeripheralExtension instance.\n";
                        return ExitCode::CONFIG;
                    }
                    (void)loaded.release();
                    extension_registry.register_extension(
                        std::unique_ptr<beebium::PeripheralExtension>(peripheral));
                } else {
                    std::cerr << "Error: Extension '" << inst.name
                              << "' has unsupported extension_kind '"
                              << manifest->extension_kind << "'.\n";
                    return ExitCode::CONFIG;
                }
            }

            // Initialise extensions (dependency resolution + topological sort)
            std::cout << "Initialising extensions...\n";
            beebium::ExtensionContext extension_context(
                beebium::HasOneMHzBus<Memory> ? &machine.state().memory.one_mhz_bus() : nullptr,
                beebium::HasUserPort<Memory> ? &machine.state().memory.user_port() : nullptr,
                beebium::HasTubeSocket<Memory> ? &machine.state().memory.tube_socket : nullptr);
            extension_registry.resolve_and_init(extension_context);

            // Log initialisation order
            for (auto* ext : extension_registry.extensions()) {
                std::cout << "  " << ext->name();
                if (!ext->id().empty()) {
                    std::cout << " [" << ext->id() << "]";
                }
                if (!ext->provides().empty()) {
                    std::cout << " provides:";
                    for (auto p : ext->provides()) std::cout << " " << p;
                }
                if (!ext->attaches_to().empty()) {
                    std::cout << " on:";
                    for (auto a : ext->attaches_to()) std::cout << " " << a;
                }
                std::cout << "\n";
            }

            // Create core discovery services for frontends to enumerate
            // extensions. PeripheralExtensionService lists peripheral
            // extensions; EconetTransportService lists econet transport
            // extensions and identifies which is active. ExtensionUiService
            // exposes the server-driven UI tree of any extension that has
            // an ExtensionUi -- it reads from both registries so a single
            // SubscribeView call can target a peripheral or a transport.
            beebium::service::PeripheralExtensionServiceImpl peripheral_extension_service(
                extension_registry);
            beebium::service::EconetTransportServiceImpl econet_transport_service(
                transport_registry);
            beebium::service::ExtensionUiServiceImpl extension_ui_service(
                transport_registry, extension_registry);

            // Collect all extension-provided services plus the discovery
            // services. Both peripheral and transport extensions can
            // contribute services; AunService / PiconetService come via
            // the transport registry's grpc_services hook.
            auto extension_services = extension_registry.collect_grpc_services();
            for (auto* svc : transport_registry.collect_grpc_services()) {
                extension_services.push_back(svc);
            }
            extension_services.push_back(&peripheral_extension_service);
            extension_services.push_back(&econet_transport_service);
            extension_services.push_back(&extension_ui_service);

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
                        config.advertise, shutdown_policy_config, std::move(shutdown_callback),
                        extension_services);

            // Print actual bound port (important when port 0 was requested for dynamic allocation)
            // Flush immediately so clients parsing stdout can detect the port before we block
            std::cout << "Listening on port " << server.port() << std::endl;
            std::cout << Memory::MACHINE_DISPLAY_NAME << " ready. Press Ctrl+C to stop." << std::endl;

            // Wire cross-processor debugger stop for Tube extensions.
            // When a breakpoint with stop_counterpart fires on one side,
            // the callback pauses the other side.
            for (auto* ext : extension_registry.extensions()) {
                auto* tube_ext = dynamic_cast<beebium::SecondProcessor65C02Extension*>(ext);
                if (tube_ext && tube_ext->running()) {
                    // Host breakpoint → pause parasite
                    server.debugger_service().set_counterpart_stop_callback(
                        tube_ext->parasite_pause_callback());
                    // Parasite breakpoint → pause host
                    tube_ext->wire_counterpart_stop([&machine] {
                        machine.pause();
                    });
                }
            }

            // Handle wait mode
            handle_wait_mode(machine, config.wait_mode);

            // Run main emulation loop (blocks until shutdown)
            run_emulation_loop(machine, server);

            std::cout << "\nShutting down...\n";

            server.stop();
            extension_registry.shutdown();

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

// ListFloppyFormats subcommand - list supported floppy disc image formats
template<typename MachineType>
class ListFloppyFormatsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-floppy-formats"; }
    std::string_view description() const override { return "List supported floppy disc image formats"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-floppy-formats\n"
                  << "\n"
                  << "Lists all floppy disc image formats that can be loaded.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        OutputFormat format = resolve_output_format(global.output_format);
        const auto& registry = default_format_registry();

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Supported floppy disc image formats:\n";
                for (const auto& handler : registry.handlers()) {
                    std::cout << "  " << handler->format_name() << "\n"
                              << "      " << handler->format_description() << "\n"
                              << "      Extensions:";
                    for (auto ext : handler->file_extensions()) {
                        std::cout << " " << ext;
                    }
                    std::cout << "\n";
                    std::cout << "      Write support: " << (handler->supports_write() ? "yes" : "no") << "\n";
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "name\tdescription\textensions\twrite_support\n";
                for (const auto& handler : registry.handlers()) {
                    std::cout << handler->format_name() << "\t"
                              << handler->format_description() << "\t";
                    bool first = true;
                    for (auto ext : handler->file_extensions()) {
                        if (!first) std::cout << ",";
                        std::cout << ext;
                        first = false;
                    }
                    std::cout << "\t" << (handler->supports_write() ? "yes" : "no") << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& handler : registry.handlers()) {
                    std::cout << "{\"name\":\"" << handler->format_name()
                              << "\",\"description\":\"" << handler->format_description()
                              << "\",\"extensions\":[";
                    bool first = true;
                    for (auto ext : handler->file_extensions()) {
                        if (!first) std::cout << ",";
                        std::cout << "\"" << ext << "\"";
                        first = false;
                    }
                    std::cout << "],\"write_support\":" << (handler->supports_write() ? "true" : "false")
                              << "}\n";
                }
                break;

            case OutputFormat::Auto:
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
            // Build image_types from the format registry so new formats appear automatically
            ojson image_types = ojson::array();
            for (const auto& handler : default_format_registry().handlers()) {
                for (auto ext : handler->file_extensions()) {
                    // Strip the leading dot for the schema
                    std::string_view ext_no_dot = ext.substr(1);
                    image_types.push_back(std::string(ext_no_dot));
                }
            }
            storage["floppy_drives"] = ojson::array({
                ojson{{"drive", 0}, {"tracks", {40, 80}}, {"sides", 2}, {"image_types", image_types}},
                ojson{{"drive", 1}, {"tracks", {40, 80}}, {"sides", 2}, {"image_types", image_types}}
            });
        }

        ojson sections = ojson::array();
        sections.push_back(storage);

        if constexpr (HasEconetSocket<Memory>) {
            ojson networking;
            networking["type"] = "networking";
            networking["econet"]["station_range"] = ojson::array({1, 254});
            networking["econet"]["default_enabled"] = false;
            networking["aun"]["default_port"] = beebium::AUN_DEFAULT_PORT;
            sections.push_back(networking);
        }

        output["sections"] = sections;

        std::cout << output.dump(2) << "\n";
        return ExitCode::OK;
    }
};

// ListPresets subcommand - list available presets for this model
template<typename MachineType>
class ListPresetsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-presets"; }
    std::string_view description() const override { return "List available presets for this model"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-presets [--json]\n"
                  << "\n"
                  << "Lists presets compatible with this machine model.\n"
                  << "\n"
                  << "Options:\n"
                  << "  --json    Output machine-readable JSON\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        bool json_output = false;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--json") {
                json_output = true;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        // Get model ID for this executable
        std::string_view model_id = Memory::MACHINE_TYPE;

        // Collect presets from system and user directories
        struct PresetEntry {
            std::string id;
            std::string name;
            std::string source;  // "system" or "user"
        };
        std::vector<PresetEntry> system_presets;
        std::vector<PresetEntry> user_presets;

        // List system presets
        if (auto system_dir = PresetPaths::get_system_presets_dirpath()) {
            for (const auto& preset_id : PresetPaths::list_presets_in_directory(*system_dir)) {
                auto filepath = *system_dir / (preset_id + std::string(PresetPaths::PRESET_EXTENSION));
                auto result = load_preset(filepath);
                if (result) {
                    // Filter by model
                    if (result.config->model && *result.config->model != model_id) {
                        continue;  // Skip presets for other models
                    }
                    system_presets.push_back({preset_id, result.config->name, "system"});
                }
            }
        }

        // List user presets
        auto user_dir = PresetPaths::get_user_presets_dirpath();
        if (std::filesystem::is_directory(user_dir)) {
            for (const auto& preset_id : PresetPaths::list_presets_in_directory(user_dir)) {
                auto filepath = user_dir / (preset_id + std::string(PresetPaths::PRESET_EXTENSION));
                auto result = load_preset(filepath);
                if (result) {
                    // Filter by model
                    if (result.config->model && *result.config->model != model_id) {
                        continue;  // Skip presets for other models
                    }
                    user_presets.push_back({preset_id, result.config->name, "user"});
                }
            }
        }

        // Output
        if (json_output) {
            nlohmann::json output;
            output["presets"] = nlohmann::json::array();
            for (const auto& p : system_presets) {
                output["presets"].push_back({{"id", p.id}, {"name", p.name}, {"source", p.source}});
            }
            for (const auto& p : user_presets) {
                output["presets"].push_back({{"id", p.id}, {"name", p.name}, {"source", p.source}});
            }
            std::cout << output.dump() << "\n";
        } else {
            // Human-readable output
            std::cout << "Built-in presets:\n";
            if (system_presets.empty()) {
                std::cout << "  (none)\n";
            } else {
                for (const auto& p : system_presets) {
                    std::cout << "  " << p.id;
                    // Pad for alignment
                    for (size_t i = p.id.size(); i < 26; ++i) {
                        std::cout << ' ';
                    }
                    std::cout << p.name << "\n";
                }
            }
            std::cout << "\nUser presets:\n";
            if (user_presets.empty()) {
                std::cout << "  (none)\n";
            } else {
                for (const auto& p : user_presets) {
                    std::cout << "  " << p.id;
                    // Pad for alignment
                    for (size_t i = p.id.size(); i < 26; ++i) {
                        std::cout << ' ';
                    }
                    std::cout << p.name << "\n";
                }
            }
        }

        return ExitCode::OK;
    }
};

// ShowPreset subcommand - output preset file contents
template<typename MachineType>
class ShowPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "show-preset"; }
    std::string_view description() const override { return "Output preset file contents"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " show-preset <id>\n"
                  << "\n"
                  << "Outputs the JSON contents of the specified preset file.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_id;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_id.empty()) {
                    preset_id = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_id.empty()) {
            std::cerr << "Error: preset ID required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Find preset file
        auto filepath = PresetPaths::find_preset_filepath(preset_id);
        if (!filepath) {
            std::cerr << "Error: preset not found: " << preset_id << "\n";
            return ExitCode::NOINPUT;
        }

        // Read and output file contents
        std::ifstream file(*filepath);
        if (!file) {
            std::cerr << "Error: cannot read preset file: " << filepath->string() << "\n";
            return ExitCode::NOINPUT;
        }

        std::cout << file.rdbuf();
        return ExitCode::OK;
    }
};

// ReportPresetsDirpath subcommand - report user presets directory
template<typename MachineType>
class ReportPresetsDirpathSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "report-presets-dirpath"; }
    std::string_view description() const override { return "Report user presets directory path"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " report-presets-dirpath\n"
                  << "\n"
                  << "Outputs the directory path where user presets are stored.\n"
                  << "This path can be overridden with the BEEBIUM_USER_PRESETS_DIRPATH\n"
                  << "environment variable.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
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
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        std::cout << PresetPaths::get_user_presets_dirpath().string() << "\n";
        return ExitCode::OK;
    }
};

// CreatePreset subcommand - create a new preset
template<typename MachineType>
class CreatePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-preset"; }
    std::string_view description() const override { return "Create a new preset"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " create-preset --name <name> [options]\n"
                  << "\n"
                  << "Creates a new preset for this machine model.\n"
                  << "\n"
                  << "Options:\n"
                  << "  --name <name>     Display name for the preset (required)\n"
                  << "  --from <id>       Source preset to copy configuration from\n"
                  << "  --output <path>   Write to specified path instead of user directory\n"
                  << "\n"
                  << "The preset ID is derived by slugifying the name.\n"
                  << "Outputs the created preset ID (or path if --output used).\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_name;
        std::string from_id;
        std::string output_filepath;

        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--name" && i + 1 < argc) {
                preset_name = argv[++i];
            } else if (arg == "--from" && i + 1 < argc) {
                from_id = argv[++i];
            } else if (arg == "--output" && i + 1 < argc) {
                output_filepath = argv[++i];
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_name.empty()) {
            std::cerr << "Error: --name is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Build preset JSON
        nlohmann::ordered_json preset;
        preset["model"] = Memory::MACHINE_TYPE;
        preset["name"] = preset_name;

        // If --from is specified, copy configuration from source preset
        if (!from_id.empty()) {
            auto source_path = PresetPaths::find_preset_filepath(from_id);
            if (!source_path) {
                std::cerr << "Error: source preset not found: " << from_id << "\n";
                return ExitCode::NOINPUT;
            }

            std::ifstream source_file(*source_path);
            if (!source_file) {
                std::cerr << "Error: cannot read source preset: " << source_path->string() << "\n";
                return ExitCode::NOINPUT;
            }

            try {
                nlohmann::json source_json = nlohmann::json::parse(source_file);
                // Copy configuration sections (excluding name and model)
                for (auto& [key, value] : source_json.items()) {
                    if (key != "name" && key != "model") {
                        preset[key] = value;
                    }
                }
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "Error: invalid JSON in source preset: " << e.what() << "\n";
                return ExitCode::DATAERR;
            }
        }

        // Determine output path
        std::filesystem::path target_filepath;
        std::string preset_id;

        if (!output_filepath.empty()) {
            target_filepath = output_filepath;
            // For --output, just output the path
            preset_id = output_filepath;
        } else {
            // Create in user presets directory
            if (!PresetPaths::ensure_user_presets_dirpath_exists()) {
                std::cerr << "Error: cannot create user presets directory\n";
                return ExitCode::IOERR;
            }

            // Generate unique ID
            preset_id = PresetPaths::generate_unique_preset_id(PresetPaths::slugify(preset_name));
            target_filepath = PresetPaths::get_user_presets_dirpath() /
                              (preset_id + std::string(PresetPaths::PRESET_EXTENSION));
        }

        // Write preset file
        std::ofstream output_file(target_filepath);
        if (!output_file) {
            std::cerr << "Error: cannot write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        output_file << preset.dump(2) << "\n";
        output_file.close();

        if (!output_file) {
            std::cerr << "Error: failed to write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        // Output the created preset ID (or path)
        std::cout << preset_id << "\n";
        return ExitCode::OK;
    }
};

// DeletePreset subcommand - delete a user preset
template<typename MachineType>
class DeletePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "delete-preset"; }
    std::string_view description() const override { return "Delete a user preset"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " delete-preset <id>\n"
                  << "\n"
                  << "Deletes a user preset. System presets cannot be deleted.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_id;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_id.empty()) {
                    preset_id = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_id.empty()) {
            std::cerr << "Error: preset ID required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Find preset file
        auto filepath = PresetPaths::find_preset_filepath(preset_id);
        if (!filepath) {
            std::cerr << "Error: preset not found: " << preset_id << "\n";
            return ExitCode::NOINPUT;
        }

        // Check if it's a system preset
        if (PresetPaths::is_system_preset(*filepath)) {
            std::cerr << "Error: cannot delete system preset: " << preset_id << "\n";
            return ExitCode::USAGE;
        }

        // Delete the file
        std::error_code ec;
        if (!std::filesystem::remove(*filepath, ec)) {
            std::cerr << "Error: cannot delete preset file: " << filepath->string();
            if (ec) {
                std::cerr << " (" << ec.message() << ")";
            }
            std::cerr << "\n";
            return ExitCode::IOERR;
        }

        return ExitCode::OK;
    }
};

// ImportPreset subcommand - import a preset file into user directory
template<typename MachineType>
class ImportPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "import-preset"; }
    std::string_view description() const override { return "Import a preset file into user directory"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " import-preset <filepath>\n"
                  << "\n"
                  << "Imports a preset file into the user presets directory.\n"
                  << "The preset's model field must match this executable's model.\n"
                  << "Outputs the imported preset ID.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::filesystem::path source_filepath;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (source_filepath.empty()) {
                    source_filepath = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (source_filepath.empty()) {
            std::cerr << "Error: filepath required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Verify source file exists
        if (!std::filesystem::exists(source_filepath)) {
            std::cerr << "Error: file not found: " << source_filepath.string() << "\n";
            return ExitCode::NOINPUT;
        }

        // Read and parse the preset file
        std::ifstream input_file(source_filepath);
        if (!input_file) {
            std::cerr << "Error: cannot read file: " << source_filepath.string() << "\n";
            return ExitCode::NOINPUT;
        }

        nlohmann::json preset;
        try {
            input_file >> preset;
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Error: invalid JSON: " << e.what() << "\n";
            return ExitCode::DATAERR;
        }

        // Verify model field matches this executable
        if (!preset.contains("model") || !preset["model"].is_string()) {
            std::cerr << "Error: preset missing 'model' field\n";
            return ExitCode::DATAERR;
        }

        std::string preset_model = preset["model"].get<std::string>();
        if (preset_model != Memory::MACHINE_TYPE) {
            std::cerr << "Error: preset model '" << preset_model << "' does not match this executable's model '"
                      << Memory::MACHINE_TYPE << "'\n";
            return ExitCode::DATAERR;
        }

        // Determine ID from filename
        std::string filename = source_filepath.stem().string();
        // Remove .preset suffix if present
        if (filename.size() > 7 && filename.substr(filename.size() - 7) == ".preset") {
            filename = filename.substr(0, filename.size() - 7);
        }
        std::string preset_id = PresetPaths::slugify(filename);
        if (preset_id.empty()) {
            // Fall back to using name field if present
            if (preset.contains("name") && preset["name"].is_string()) {
                preset_id = PresetPaths::slugify(preset["name"].get<std::string>());
            }
        }
        if (preset_id.empty()) {
            preset_id = "imported-preset";
        }

        // Ensure user presets directory exists
        if (!PresetPaths::ensure_user_presets_dirpath_exists()) {
            std::cerr << "Error: cannot create user presets directory\n";
            return ExitCode::IOERR;
        }

        // Generate unique ID to avoid conflicts
        preset_id = PresetPaths::generate_unique_preset_id(preset_id);

        // Write to user presets directory
        std::filesystem::path target_filepath = PresetPaths::get_user_presets_dirpath() /
                                                (preset_id + std::string(PresetPaths::PRESET_EXTENSION));

        std::ofstream output_file(target_filepath);
        if (!output_file) {
            std::cerr << "Error: cannot write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        output_file << preset.dump(2) << "\n";
        output_file.close();

        if (!output_file) {
            std::cerr << "Error: failed to write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        std::cout << preset_id << "\n";
        return ExitCode::OK;
    }
};

// ExportPreset subcommand - export a preset to a file
template<typename MachineType>
class ExportPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "export-preset"; }
    std::string_view description() const override { return "Export a preset to a file"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " export-preset <id> --output <filepath>\n"
                  << "\n"
                  << "Exports a preset to the specified file path.\n"
                  << "\n"
                  << "Options:\n"
                  << "  --output <path>   Output file path (required)\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_id;
        std::filesystem::path output_filepath;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--output" || arg == "-o") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --output requires a path\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
                output_filepath = argv[++i];
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_id.empty()) {
                    preset_id = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_id.empty()) {
            std::cerr << "Error: preset ID required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        if (output_filepath.empty()) {
            std::cerr << "Error: --output is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Find preset file
        auto source_filepath = PresetPaths::find_preset_filepath(preset_id);
        if (!source_filepath) {
            std::cerr << "Error: preset not found: " << preset_id << "\n";
            return ExitCode::NOINPUT;
        }

        // Copy file to output path
        std::error_code ec;
        std::filesystem::copy_file(*source_filepath, output_filepath,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "Error: cannot write to output file: " << output_filepath.string()
                      << " (" << ec.message() << ")\n";
            return ExitCode::IOERR;
        }

        return ExitCode::OK;
    }
};

// CaptureScreenshot subcommand - headless emulation to PNG
template<typename MachineType>
class CaptureScreenshotSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "capture-screenshot"; }
    std::string_view description() const override {
        return "Capture a screenshot from headless emulation";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " capture-screenshot --output <filepath> [options]\n"
                  << "\n"
                  << "Runs the emulator headlessly and captures a framebuffer screenshot as PNG.\n"
                  << "Accepts all 'start' options (--preset, --floppy, --fdc, etc.).\n"
                  << "\n"
                  << "Required:\n"
                  << "  --output <filepath>      Output PNG filepath\n"
                  << "\n"
                  << "Optional:\n"
                  << "  --duration <seconds>     Emulation duration before capture (default: from\n"
                  << "                           preset thumbnail_capture_delay_seconds, or 2.0)\n"
                  << "  --border <pixels>        Black border width around the image (default: 20)\n"
                  << "\n"
                  << "All 'start' subcommand options are also accepted.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Pre-scan argv for capture-specific args (--output, --duration, --border),
        // then pass the rest to parse_start_arguments unchanged.
        std::string output_filepath;
        std::optional<double> cli_duration;
        int border_pixels = 20;

        std::vector<char*> filtered_argv;
        for (int i = 0; i < argc; ++i) {
            if (i >= global.subcommand_argv_start) {
                std::string_view arg = argv[i];
                if (arg == "--output" && i + 1 < argc) {
                    output_filepath = argv[i + 1];
                    ++i;
                    continue;
                } else if (arg == "--duration" && i + 1 < argc) {
                    try {
                        cli_duration = std::stod(argv[i + 1]);
                    } catch (...) {
                        std::cerr << "Error: --duration must be a number\n";
                        return ExitCode::USAGE;
                    }
                    if (*cli_duration < 0) {
                        std::cerr << "Error: --duration must be non-negative\n";
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg == "--border" && i + 1 < argc) {
                    try {
                        border_pixels = parse_int(argv[i + 1], "--border");
                    } catch (const std::exception& e) {
                        std::cerr << "Error: " << e.what() << "\n";
                        return ExitCode::USAGE;
                    }
                    if (border_pixels < 0) {
                        std::cerr << "Error: --border must be non-negative\n";
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg == "--help" || arg == "-h") {
                    help(argv[0]);
                    return ExitCode::OK;
                }
            }
            filtered_argv.push_back(argv[i]);
        }
        int filtered_argc = static_cast<int>(filtered_argv.size());

        if (output_filepath.empty()) {
            std::cerr << "Error: --output is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Parse machine configuration from remaining args
        ServerConfig<MachineType> config;
        if (auto exit_code = parse_start_arguments<MachineType>(
                filtered_argc, filtered_argv.data(),
                global.subcommand_argv_start, config)) {
            return *exit_code;
        }

        if (auto error = validate_config<MachineType>(config)) {
            std::cerr << "Error: " << *error << "\n";
            return ExitCode::CONFIG;
        }

        // Determine capture duration.
        // Priority: CLI --duration > preset thumbnail_capture_delay_seconds > 2.0
        double duration_seconds = 2.0;
        if (cli_duration) {
            duration_seconds = *cli_duration;
        } else if (config.preset_filepath) {
            auto result = load_preset(*config.preset_filepath);
            if (result && result.config->thumbnail_capture_delay_seconds) {
                duration_seconds = *result.config->thumbnail_capture_delay_seconds;
            }
        }

        try {
            if (!config.rom_dirpath.empty()) {
                RomPaths::set_rom_directory(config.rom_dirpath);
            }

            MachineType machine;
            load_roms(machine, config);

            if (auto exit_code = install_disc_controller(machine, config)) {
                return *exit_code;
            }
            // Screenshot capture uses no extensions; pass an empty
            // transport registry. install_econet sees no transports and
            // installs a disconnected stub backend (matching the
            // "Econet hardware fitted but no network" case).
            beebium::EconetTransportRegistry transport_registry;
            if (auto exit_code = install_econet(machine, config, transport_registry)) {
                return *exit_code;
            }
            if (auto exit_code = load_disc_images(machine, config)) {
                return *exit_code;
            }

            // Enable video output only (no audio needed for screenshots)
            machine.state().memory.enable_video_output();

            apply_startup_options(machine, config);
            machine.reset();

            // Create local framebuffer and renderer (no gRPC server needed)
            FrameBuffer frame_buffer;
            FrameRenderer renderer(&frame_buffer);
            auto& video_output = *machine.state().memory.video_output;

            // Helper to drain the video output queue completely.
            // Each machine.run() step produces ~20,000 PixelBatches (one per 2 CPU
            // cycles), so we must drain the entire queue after each step rather than
            // relying on the default max_units of 1000.
            auto drain_video = [&]() {
                while (renderer.process(video_output, 100000) > 0) {}
            };

            constexpr uint64_t cycles_per_step = 40000;

            if (duration_seconds == 0.0) {
                // Duration 0: capture first complete frame
                uint64_t initial_version = frame_buffer.version();
                while (frame_buffer.version() == initial_version) {
                    machine.run(cycles_per_step);
                    drain_video();
                }
            } else {
                uint64_t total_cycles = static_cast<uint64_t>(
                    duration_seconds * timing::CYCLES_PER_SECOND);
                uint64_t cycles_run = 0;
                while (cycles_run < total_cycles) {
                    machine.run(cycles_per_step);
                    drain_video();
                    cycles_run += cycles_per_step;
                }
            }

            if (frame_buffer.version() == 0) {
                std::cerr << "Error: no frames produced during emulation\n";
                return ExitCode::SOFTWARE;
            }

            // Extract frame data
            const auto& metadata = frame_buffer.metadata();
            uint32_t frame_width = metadata.width;
            uint32_t frame_height = metadata.height;
            size_t stride_pixels = frame_buffer.stride_pixels();

            std::vector<uint32_t> frame_copy(stride_pixels * frame_height);
            frame_buffer.copy_frame(frame_copy.data(), frame_copy.size());

            // Scale from logical frame to display dimensions (nearest-neighbour)
            // with black border around the image
            uint32_t display_width = metadata.display_width;
            uint32_t display_height = metadata.display_height;
            uint32_t border = static_cast<uint32_t>(border_pixels);
            uint32_t output_width = display_width + 2 * border;
            uint32_t output_height = display_height + 2 * border;

            // Zero-initialised = black with alpha 0, then we set alpha for all pixels
            std::vector<uint8_t> rgba(output_width * output_height * 4, 0);

            // Set alpha to 0xFF for all pixels (border stays black RGB)
            for (size_t i = 3; i < rgba.size(); i += 4) {
                rgba[i] = 0xFF;
            }

            // Write display content into the bordered region
            for (uint32_t y = 0; y < display_height; ++y) {
                uint32_t src_y = y * frame_height / display_height;
                if (src_y >= frame_height) src_y = frame_height - 1;

                for (uint32_t x = 0; x < display_width; ++x) {
                    uint32_t src_x = x * frame_width / display_width;
                    if (src_x >= frame_width) src_x = frame_width - 1;

                    uint32_t bgra = frame_copy[src_y * stride_pixels + src_x];

                    // BGRA32 to RGBA bytes, offset by border
                    size_t dst_idx = ((y + border) * output_width + (x + border)) * 4;
                    rgba[dst_idx + 0] = static_cast<uint8_t>((bgra >> 16) & 0xFF);  // R
                    rgba[dst_idx + 1] = static_cast<uint8_t>((bgra >> 8) & 0xFF);   // G
                    rgba[dst_idx + 2] = static_cast<uint8_t>(bgra & 0xFF);           // B
                    rgba[dst_idx + 3] = static_cast<uint8_t>((bgra >> 24) & 0xFF);   // A
                }
            }

            int write_result = stbi_write_png(
                output_filepath.c_str(),
                static_cast<int>(output_width),
                static_cast<int>(output_height),
                4,
                rgba.data(),
                static_cast<int>(output_width * 4)
            );

            if (!write_result) {
                std::cerr << "Error: failed to write PNG: " << output_filepath << "\n";
                return ExitCode::IOERR;
            }

            std::cout << output_filepath << "\n";
            return ExitCode::OK;

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::SOFTWARE;
        }
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
        v.push_back(std::make_unique<ListFloppyFormatsSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeMachineSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribePresetSchemaSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListPresetsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ShowPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ReportPresetsDirpathSubcommand<MachineType>>());
        v.push_back(std::make_unique<CreatePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<DeletePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ImportPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ExportPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<CaptureScreenshotSubcommand<MachineType>>());
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
        for (size_t i = cmd->name().length(); i < 22; ++i) {
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

} // namespace beebium::server

#endif // BEEBIUM_SERVER_SERVER_MAIN_HPP
