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

// test_cli.cpp
// Tests for CLI subcommand architecture (parse_global_arguments, dispatch_subcommand)

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
// parse_global_arguments() tests
// ============================================================================

TEST_CASE("parse_global_arguments: no arguments returns empty subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name.empty());
    REQUIRE(global.subcommand_argv_start == 1);
    REQUIRE(global.help_requested == false);
}

TEST_CASE("parse_global_arguments: --help sets help_requested", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "--help"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.help_requested == true);
}

TEST_CASE("parse_global_arguments: -h sets help_requested", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "-h"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.help_requested == true);
}

TEST_CASE("parse_global_arguments: subcommand name is recognized", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "start"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "start");
    REQUIRE(global.subcommand_argv_start == 2);
}

TEST_CASE("parse_global_arguments: subcommand with options", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "start", "--port", "8080"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "start");
    REQUIRE(global.subcommand_argv_start == 2);
}

TEST_CASE("parse_global_arguments: --help before subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "--help", "start"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.help_requested == true);
    REQUIRE(global.subcommand_name == "start");
}

TEST_CASE("parse_global_arguments: list-fdcs subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: describe-machine subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "describe-machine");
}

TEST_CASE("parse_global_arguments: help subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "help"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "help");
}

TEST_CASE("parse_global_arguments: unknown option passes through to default subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "--unknown-global"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    // Unknown options are NOT errors at the global level - they pass through
    // to the default "start" subcommand which will handle them
    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name.empty());  // No explicit subcommand
    REQUIRE(global.subcommand_argv_start == 1);  // Options start at index 1
}

// ============================================================================
// parse_start_arguments() tests
// ============================================================================

TEST_CASE("parse_start_arguments: --help returns OK", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--help"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::OK);
}

TEST_CASE("parse_start_arguments: --port sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--port", "8080"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.port == 8080);
}

TEST_CASE("parse_start_arguments: --port accepts hex value", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--port", "0xbeeb"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.port == 48875);  // 0xBEEB
}

TEST_CASE("parse_start_arguments: --mos sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--mos", "custom.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.mos_filepath == "custom.rom");
}

TEST_CASE("parse_start_arguments: --screen-mode sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--screen-mode", "4"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.screen_mode == 4);
    REQUIRE(config.screen_mode_set == true);
}

TEST_CASE("parse_start_arguments: --screen-mode accepts binary value", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--screen-mode", "0b101"};  // Binary 5
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.screen_mode == 5);
    REQUIRE(config.screen_mode_set == true);
}

TEST_CASE("parse_start_arguments: --screen-mode out of range returns USAGE", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--screen-mode", "8"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: --links sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--links", "128"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.raw_links == 128);
}

TEST_CASE("parse_start_arguments: --links accepts hex value", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--links", "0xff"};  // Hex 255
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.raw_links == 255);
}

TEST_CASE("parse_start_arguments: --links out of range returns USAGE", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--links", "256"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: unknown argument returns USAGE", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--unknown"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: multiple options combined", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--mos", "mos.rom", "--port", "9000", "--screen-mode", "0"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.mos_filepath == "mos.rom");
    REQUIRE(config.port == 9000);
    REQUIRE(config.screen_mode == 0);
}

TEST_CASE("parse_start_arguments: implicit start (no subcommand)", "[cli][parse_start_arguments]") {
    // When no subcommand is given, start_index is 1 (first arg after program name)
    ArgvHelper args{"beebium", "--port", "7000"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 1, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.port == 7000);
}

// ============================================================================
// dispatch_subcommand() tests
// ============================================================================

TEST_CASE("dispatch_subcommand: list-fdcs returns OK", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "list-fdcs"};
    GlobalConfig global;
    global.subcommand_name = "list-fdcs";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: describe-machine returns OK", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "describe-machine"};
    GlobalConfig global;
    global.subcommand_name = "describe-machine";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: help returns OK", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "help"};
    GlobalConfig global;
    global.subcommand_name = "help";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: unknown subcommand returns USAGE", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "unknown-command"};
    GlobalConfig global;
    global.subcommand_name = "unknown-command";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: global --help with no subcommand shows global help", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "--help"};
    GlobalConfig global;
    global.help_requested = true;
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: help with target subcommand", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "help", "list-fdcs"};
    GlobalConfig global;
    global.subcommand_name = "help";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: help with unknown target returns USAGE", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "help", "unknown-cmd"};
    GlobalConfig global;
    global.subcommand_name = "help";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

// ============================================================================
// ExitCode constants tests
// ============================================================================

TEST_CASE("ExitCode: constants have expected values", "[cli][ExitCode]") {
    REQUIRE(ExitCode::OK == 0);
    REQUIRE(ExitCode::USAGE == 64);
    REQUIRE(ExitCode::DATAERR == 65);
    REQUIRE(ExitCode::NOINPUT == 66);
    REQUIRE(ExitCode::SOFTWARE == 70);
    REQUIRE(ExitCode::IOERR == 74);
    REQUIRE(ExitCode::CONFIG == 78);
}

// ============================================================================
// GlobalConfig tests
// ============================================================================

TEST_CASE("GlobalConfig: default values", "[cli][GlobalConfig]") {
    GlobalConfig global;

    REQUIRE(global.subcommand_name.empty());
    REQUIRE(global.subcommand_argv_start == 1);
    REQUIRE(global.help_requested == false);
}

// ============================================================================
// get_subcommands() tests
// ============================================================================

TEST_CASE("get_subcommands: returns correct number of subcommands", "[cli][get_subcommands]") {
    const auto& cmds = get_subcommands<MachineType>();

    REQUIRE(cmds.size() == 4);
}

TEST_CASE("get_subcommands: contains expected subcommands", "[cli][get_subcommands]") {
    const auto& cmds = get_subcommands<MachineType>();

    std::vector<std::string> names;
    for (const auto& cmd : cmds) {
        names.push_back(std::string(cmd->name()));
    }

    REQUIRE(std::find(names.begin(), names.end(), "start") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "list-fdcs") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "describe-machine") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "help") != names.end());
}

TEST_CASE("get_subcommands: subcommands have non-empty descriptions", "[cli][get_subcommands]") {
    const auto& cmds = get_subcommands<MachineType>();

    for (const auto& cmd : cmds) {
        REQUIRE_FALSE(cmd->description().empty());
    }
}

// ============================================================================
// parse_int() tests
// ============================================================================

TEST_CASE("parse_int: decimal integers", "[cli][parse_int]") {
    REQUIRE(parse_int("0") == 0);
    REQUIRE(parse_int("1") == 1);
    REQUIRE(parse_int("123") == 123);
    REQUIRE(parse_int("255") == 255);
    REQUIRE(parse_int("48875") == 48875);
    REQUIRE(parse_int("2147483647") == 2147483647);  // INT_MAX
}

TEST_CASE("parse_int: negative decimal integers", "[cli][parse_int]") {
    REQUIRE(parse_int("-1") == -1);
    REQUIRE(parse_int("-123") == -123);
    REQUIRE(parse_int("-2147483648") == -2147483648);  // INT_MIN
}

TEST_CASE("parse_int: binary integers (0b prefix)", "[cli][parse_int]") {
    REQUIRE(parse_int("0b0") == 0);
    REQUIRE(parse_int("0b1") == 1);
    REQUIRE(parse_int("0b10") == 2);
    REQUIRE(parse_int("0b1010") == 10);
    REQUIRE(parse_int("0b11111111") == 255);
    REQUIRE(parse_int("0b1111") == 15);
}

TEST_CASE("parse_int: binary integers (0B prefix, uppercase)", "[cli][parse_int]") {
    REQUIRE(parse_int("0B1010") == 10);
    REQUIRE(parse_int("0B11111111") == 255);
}

TEST_CASE("parse_int: octal integers (0o prefix)", "[cli][parse_int]") {
    REQUIRE(parse_int("0o0") == 0);
    REQUIRE(parse_int("0o1") == 1);
    REQUIRE(parse_int("0o7") == 7);
    REQUIRE(parse_int("0o10") == 8);
    REQUIRE(parse_int("0o17") == 15);
    REQUIRE(parse_int("0o377") == 255);
}

TEST_CASE("parse_int: octal integers (0O prefix, uppercase)", "[cli][parse_int]") {
    REQUIRE(parse_int("0O17") == 15);
    REQUIRE(parse_int("0O377") == 255);
}

TEST_CASE("parse_int: hexadecimal integers (0x prefix)", "[cli][parse_int]") {
    REQUIRE(parse_int("0x0") == 0);
    REQUIRE(parse_int("0x1") == 1);
    REQUIRE(parse_int("0xf") == 15);
    REQUIRE(parse_int("0xff") == 255);
    REQUIRE(parse_int("0xbeeb") == 48875);
    REQUIRE(parse_int("0xBEEB") == 48875);
    REQUIRE(parse_int("0x7fffffff") == 2147483647);  // INT_MAX in hex
}

TEST_CASE("parse_int: hexadecimal integers (0X prefix, uppercase)", "[cli][parse_int]") {
    REQUIRE(parse_int("0Xff") == 255);
    REQUIRE(parse_int("0XBEEB") == 48875);
}

TEST_CASE("parse_int: empty string throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int(""), std::runtime_error);
}

TEST_CASE("parse_int: invalid format throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int("abc"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("12abc"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("0b2"), std::runtime_error);  // Invalid binary digit
    REQUIRE_THROWS_AS(parse_int("0o8"), std::runtime_error);  // Invalid octal digit
    REQUIRE_THROWS_AS(parse_int("0xgg"), std::runtime_error); // Invalid hex digit
}

TEST_CASE("parse_int: prefix without digits throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int("0b"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("0o"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("0x"), std::runtime_error);
}

TEST_CASE("parse_int: overflow throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int("0xDEADBEEF"), std::runtime_error);  // > INT_MAX
    REQUIRE_THROWS_AS(parse_int("9999999999"), std::runtime_error);  // > INT_MAX
}

TEST_CASE("parse_int: context appears in error message", "[cli][parse_int]") {
    try {
        parse_int("invalid", "--port");
        FAIL("Expected exception");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("--port") != std::string::npos);
    }
}

// ============================================================================
// OutputFormat and --format option tests
// ============================================================================

TEST_CASE("parse_global_arguments: default output_format is Auto", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Auto);
}

TEST_CASE("parse_global_arguments: --format pretty sets output_format", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "pretty", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Pretty);
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: --format tsv sets output_format", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "tsv", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Tsv);
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: --format jsonl sets output_format", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "jsonl", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Jsonl);
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: --format=pretty works", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=pretty", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Pretty);
    REQUIRE(global.subcommand_name == "describe-machine");
}

TEST_CASE("parse_global_arguments: --format=tsv works", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=tsv", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Tsv);
}

TEST_CASE("parse_global_arguments: --format=jsonl works", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=jsonl", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Jsonl);
}

TEST_CASE("parse_global_arguments: invalid --format returns USAGE", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "invalid", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_global_arguments: invalid --format=value returns USAGE", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=xml", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("resolve_output_format: Pretty returns Pretty", "[cli][resolve_output_format]") {
    REQUIRE(resolve_output_format(OutputFormat::Pretty) == OutputFormat::Pretty);
}

TEST_CASE("resolve_output_format: Tsv returns Tsv", "[cli][resolve_output_format]") {
    REQUIRE(resolve_output_format(OutputFormat::Tsv) == OutputFormat::Tsv);
}

TEST_CASE("resolve_output_format: Jsonl returns Jsonl", "[cli][resolve_output_format]") {
    REQUIRE(resolve_output_format(OutputFormat::Jsonl) == OutputFormat::Jsonl);
}

TEST_CASE("parse_format_arg: valid values", "[cli][parse_format_arg]") {
    REQUIRE(parse_format_arg("pretty") == OutputFormat::Pretty);
    REQUIRE(parse_format_arg("tsv") == OutputFormat::Tsv);
    REQUIRE(parse_format_arg("jsonl") == OutputFormat::Jsonl);
}

TEST_CASE("parse_format_arg: invalid value throws", "[cli][parse_format_arg]") {
    REQUIRE_THROWS_AS(parse_format_arg("invalid"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_format_arg("json"), std::runtime_error);  // Not jsonl
    REQUIRE_THROWS_AS(parse_format_arg("PRETTY"), std::runtime_error);  // Case sensitive
}

// ============================================================================
// Provenance CLI option tests
// ============================================================================

TEST_CASE("parse_start_arguments: --provenance-type sets config", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start", "--provenance-type", "python-client"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type == "python-client");
}

TEST_CASE("parse_start_arguments: --provenance-uuid sets config", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start", "--provenance-uuid", "550e8400-e29b-41d4-a716-446655440000"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_uuid == "550e8400-e29b-41d4-a716-446655440000");
}

TEST_CASE("parse_start_arguments: --provenance-version sets config", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start", "--provenance-version", "1.2.3"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_version == "1.2.3");
}

TEST_CASE("parse_start_arguments: all provenance options combined", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start",
        "--provenance-type", "macos-gui",
        "--provenance-uuid", "12345678-1234-1234-1234-123456789abc",
        "--provenance-version", "2.0.0"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type == "macos-gui");
    REQUIRE(config.provenance_uuid == "12345678-1234-1234-1234-123456789abc");
    REQUIRE(config.provenance_version == "2.0.0");
}

TEST_CASE("parse_start_arguments: provenance type values", "[cli][parse_start_arguments][provenance]") {
    SECTION("terminal type") {
        ArgvHelper args{"beebium", "start", "--provenance-type", "terminal"};
        ServerConfig<MachineType> config;
        parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
        REQUIRE(config.provenance_type == "terminal");
    }

    SECTION("unknown type") {
        ArgvHelper args{"beebium", "start", "--provenance-type", "unknown"};
        ServerConfig<MachineType> config;
        parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
        REQUIRE(config.provenance_type == "unknown");
    }

    SECTION("typescript-oracle type") {
        ArgvHelper args{"beebium", "start", "--provenance-type", "typescript-oracle"};
        ServerConfig<MachineType> config;
        parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
        REQUIRE(config.provenance_type == "typescript-oracle");
    }
}

TEST_CASE("parse_start_arguments: provenance defaults are empty", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type.empty());
    REQUIRE(config.provenance_uuid.empty());
    REQUIRE(config.provenance_version.empty());
}

// ============================================================================
// UUID validation function tests
// ============================================================================

TEST_CASE("is_valid_uuid: valid UUIDs", "[cli][uuid]") {
    REQUIRE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000"));
    REQUIRE(is_valid_uuid("12345678-1234-1234-1234-123456789abc"));
    REQUIRE(is_valid_uuid("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF"));
    REQUIRE(is_valid_uuid("00000000-0000-0000-0000-000000000000"));
    REQUIRE(is_valid_uuid("abcdef01-2345-6789-abcd-ef0123456789"));
}

TEST_CASE("is_valid_uuid: invalid UUIDs", "[cli][uuid]") {
    REQUIRE_FALSE(is_valid_uuid(""));
    REQUIRE_FALSE(is_valid_uuid("not-a-uuid"));
    REQUIRE_FALSE(is_valid_uuid("550e8400e29b41d4a716446655440000"));  // No hyphens
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-44665544000"));   // Too short
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-4466554400000")); // Too long
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-44665544000g"));  // Invalid hex char
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a71-6446655440000"));  // Wrong position hyphen
}

TEST_CASE("generate_uuid_v4: generates valid format", "[cli][uuid]") {
    auto uuid = generate_uuid_v4();

    REQUIRE(uuid.length() == 36);
    REQUIRE(is_valid_uuid(uuid));

    // Check version 4 indicator (character at position 14 should be '4')
    REQUIRE(uuid[14] == '4');

    // Check variant (character at position 19 should be 8, 9, a, or b)
    char variant = uuid[19];
    REQUIRE((variant == '8' || variant == '9' || variant == 'a' || variant == 'b'));
}

TEST_CASE("generate_uuid_v4: generates unique values", "[cli][uuid]") {
    auto uuid1 = generate_uuid_v4();
    auto uuid2 = generate_uuid_v4();
    auto uuid3 = generate_uuid_v4();

    REQUIRE(uuid1 != uuid2);
    REQUIRE(uuid2 != uuid3);
    REQUIRE(uuid1 != uuid3);
}

// ============================================================================
// Machine Identity CLI option tests
// ============================================================================

TEST_CASE("parse_start_arguments: --machine-uuid sets config", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-uuid", "550e8400-e29b-41d4-a716-446655440000"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_uuid == "550e8400-e29b-41d4-a716-446655440000");
}

TEST_CASE("parse_start_arguments: --machine-uuid rejects invalid UUID", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-uuid", "not-a-uuid"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: --machine-name sets config", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-name", "My BBC Micro"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_name == "My BBC Micro");
}

TEST_CASE("parse_start_arguments: --machine-name allows spaces", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-name", "Level 3 Fileserver"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_name == "Level 3 Fileserver");
}

TEST_CASE("parse_start_arguments: all identity options combined", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start",
        "--machine-uuid", "12345678-1234-1234-1234-123456789abc",
        "--machine-name", "Teletext Server"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_uuid == "12345678-1234-1234-1234-123456789abc");
    REQUIRE(config.machine_name == "Teletext Server");
}

TEST_CASE("parse_start_arguments: identity defaults are empty", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_uuid.empty());
    REQUIRE(config.machine_name.empty());
}

TEST_CASE("parse_start_arguments: identity and provenance combined", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start",
        "--provenance-type", "python-client",
        "--provenance-uuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
        "--machine-uuid", "11111111-2222-3333-4444-555555555555",
        "--machine-name", "Test Server"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type == "python-client");
    REQUIRE(config.provenance_uuid == "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    REQUIRE(config.machine_uuid == "11111111-2222-3333-4444-555555555555");
    REQUIRE(config.machine_name == "Test Server");
}
