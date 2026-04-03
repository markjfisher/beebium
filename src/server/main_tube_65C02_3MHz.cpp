// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Tube parasite executable: 65C02 at 3 MHz (external "cheese wedge" second processor).
//
// This is a standalone process that connects to a host beebium-model-b (or
// similar) server via gRPC, maps the shared memory region, loads the parasite
// boot ROM, and runs the 65C02 emulation loop at 3 MHz.
//
// Usage:
//   beebium-tube-65C02-3MHz --host=localhost:50051
//
// The host launches this executable automatically when started with:
//   beebium-model-b --tube 65C02-3MHz

#include <beebium/PacingClock.hpp>
#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeSharedMemory.hpp>
#include <beebium/service/ParasiteServer.hpp>

#include "tube.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <array>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

static constexpr uint64_t PARASITE_CLOCK_HZ = 3'000'000;
static constexpr const char* ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr size_t ROM_SIZE = 2048;
static constexpr uint32_t PROTOCOL_VERSION = 1;
static constexpr const char* PARASITE_TYPE = "65C02";

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int) {
    g_shutdown_requested.store(true, std::memory_order_release);
}

struct ParasiteConfig {
    std::string host_address;     // e.g. "localhost:50051"
    std::string rom_dirpath;      // Optional ROM directory override
    uint16_t port = 0;            // gRPC server port (0 = dynamic)
    bool advertise = false;       // Enable mDNS advertisement
};

bool parse_args(int argc, char* argv[], ParasiteConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.starts_with("--host=")) {
            config.host_address = arg.substr(7);
        } else if (arg.starts_with("--rom-dir=")) {
            config.rom_dirpath = arg.substr(10);
        } else if (arg.starts_with("--port=")) {
            config.port = static_cast<uint16_t>(std::stoi(arg.substr(7)));
        } else if (arg == "--advertise") {
            config.advertise = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " --host=HOST:PORT [OPTIONS]\n"
                      << "\n"
                      << "Options:\n"
                      << "  --host=HOST:PORT  Host server address (required)\n"
                      << "  --rom-dir=DIR     ROM directory override\n"
                      << "  --port=PORT       Parasite gRPC server port (default: dynamic)\n"
                      << "  --advertise       Enable mDNS advertisement\n"
                      << "  --help            Show this help\n";
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    if (config.host_address.empty()) {
        std::cerr << "Error: --host=HOST:PORT is required\n";
        return false;
    }

    return true;
}

// Find the ROM file. Search order:
// 1. Explicit --rom-dir
// 2. Sibling "roms/" directory to this executable
// 3. ../share/beebium/roms/ relative to executable
std::filesystem::path find_rom(const char* argv0, const ParasiteConfig& config) {
    namespace fs = std::filesystem;

    if (!config.rom_dirpath.empty()) {
        auto filepath = fs::path(config.rom_dirpath) / ROM_FILENAME;
        if (fs::exists(filepath)) return filepath;
    }

    auto exe_dirpath = fs::path(argv0).parent_path();

    auto sibling = exe_dirpath / "roms" / ROM_FILENAME;
    if (fs::exists(sibling)) return sibling;

    auto share = exe_dirpath / ".." / "share" / "beebium" / "roms" / ROM_FILENAME;
    if (fs::exists(share)) return fs::canonical(share);

    return {};
}

std::array<uint8_t, ROM_SIZE> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open ROM: " + filepath.string());
    }
    std::array<uint8_t, ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), ROM_SIZE);
    if (file.gcount() != static_cast<std::streamsize>(ROM_SIZE)) {
        throw std::runtime_error("ROM file is not " + std::to_string(ROM_SIZE) +
                                 " bytes: " + filepath.string());
    }
    return rom;
}

}  // namespace

int main(int argc, char* argv[]) {
    // --- Parse arguments ---
    ParasiteConfig config;
    if (!parse_args(argc, argv, config)) {
        return 1;
    }

    // --- Install signal handlers ---
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    // --- Find and load ROM ---
    auto rom_filepath = find_rom(argv[0], config);
    if (rom_filepath.empty()) {
        std::cerr << "Error: Tube Client ROM not found: " << ROM_FILENAME << "\n";
        return 66;  // EX_NOINPUT
    }

    std::array<uint8_t, ROM_SIZE> rom;
    try {
        rom = load_rom(rom_filepath);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 66;
    }

    std::cout << "ROM: " << rom_filepath.string() << "\n";

    // --- Connect to host ---
    auto channel = grpc::CreateChannel(
        config.host_address, grpc::InsecureChannelCredentials());
    auto stub = beebium::TubeService::NewStub(channel);

    // Wait for channel to be ready (with timeout)
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (!channel->WaitForConnected(deadline)) {
        std::cerr << "Error: Cannot connect to host at " << config.host_address << "\n";
        return 69;  // EX_UNAVAILABLE
    }

    // --- Handshake: call TubeService.Connect ---
    grpc::ClientContext connect_ctx;
    beebium::TubeConnectRequest connect_req;
    beebium::TubeConnectResponse connect_resp;
    connect_req.set_protocol_version(PROTOCOL_VERSION);
    connect_req.set_parasite_type(PARASITE_TYPE);
    connect_req.set_parasite_clock_hz(PARASITE_CLOCK_HZ);

    auto status = stub->Connect(&connect_ctx, connect_req, &connect_resp);
    if (!status.ok()) {
        std::cerr << "Error: TubeService.Connect failed: "
                  << status.error_message() << "\n";
        return 70;  // EX_SOFTWARE
    }

    std::cout << "Connected to host. Shared memory: "
              << connect_resp.shared_memory_name() << " ("
              << connect_resp.shared_memory_size() << " bytes)\n";

    // --- Map shared memory ---
    // Extract the suffix from the shared memory name.
    // POSIX name is "/<suffix>", Windows name is "beebium_tube_<suffix>".
    // TubeSharedMemory reconstructs the platform name from the suffix.
    std::string shm_name = connect_resp.shared_memory_name();
    std::string suffix;
    if (shm_name.starts_with("/")) {
        suffix = shm_name.substr(1);
    } else if (shm_name.starts_with("beebium_tube_")) {
        suffix = shm_name.substr(std::strlen("beebium_tube_"));
    } else {
        std::cerr << "Error: Unexpected shared memory name format: " << shm_name << "\n";
        return 70;
    }

    beebium::TubeSharedMemory shm(suffix, beebium::TubeSharedMemoryRole::Joiner);
    std::cout << "Shared memory mapped.\n";

    // --- Create parasite runner ---
    beebium::ParasiteRunner runner(shm.get(), rom);
    runner.reset();

    // --- Start parasite gRPC server ---
    beebium::service::ParasiteServer parasite_server(runner, "127.0.0.1", config.port);

    beebium::service::Provenance provenance;
    provenance.type = "tube-parasite";
    provenance.timestamp = std::chrono::system_clock::now();

    beebium::service::MachineIdentity identity;
    identity.model_type = "Tube65C02";
    identity.model_name = "65C02 3 MHz Second Processor";

    parasite_server.start(std::move(provenance), std::move(identity),
                          config.advertise,
                          static_cast<uint32_t>(PARASITE_CLOCK_HZ),
                          [&runner]() { runner.request_shutdown(); });

    std::cout << "Parasite gRPC server on port " << parasite_server.port() << "\n";

    // --- Register parasite's gRPC endpoint with the host ---
    {
        grpc::ClientContext register_ctx;
        beebium::RegisterEndpointRequest register_req;
        beebium::RegisterEndpointResponse register_resp;
        register_req.set_grpc_address(
            "localhost:" + std::to_string(parasite_server.port()));
        auto register_status = stub->RegisterEndpoint(
            &register_ctx, register_req, &register_resp);
        if (register_status.ok()) {
            std::cout << "Registered gRPC endpoint with host.\n";
        } else {
            std::cerr << "Warning: Failed to register endpoint: "
                      << register_status.error_message() << "\n";
        }
    }

    // --- Pacing clock (3 MHz, 200 Hz tick rate) ---
    beebium::PacingConfig pacing_config{
        .base_clock_hz = PARASITE_CLOCK_HZ,
        .pacing_hz = 200,
        .speed_multiplier = 1.0
    };
    beebium::PacingClock clock(pacing_config, &shm.get()->io_pending_parasite);
    uint64_t cycles_per_tick = pacing_config.cycles_per_tick();

    std::cout << "Starting 65C02 at " << PARASITE_CLOCK_HZ / 1'000'000 << " MHz "
              << "(" << cycles_per_tick << " cycles/tick at "
              << pacing_config.pacing_hz << " Hz)\n";

    // --- Main execution loop ---
    clock.start();

    while (!g_shutdown_requested.load(std::memory_order_acquire) &&
           !runner.shutdown_requested())
    {
        runner.run(cycles_per_tick);
        clock.wait_for_tick();
    }

    clock.stop();
    parasite_server.stop();

    std::cout << "Parasite shutdown. Cycles: " << runner.cycle_count() << "\n";
    return 0;
}
