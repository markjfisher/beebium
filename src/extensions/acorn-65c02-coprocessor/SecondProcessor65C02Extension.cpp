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

#include "SecondProcessor65C02Extension.hpp"

#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/PlatformSleep.hpp"
#include "beebium/SleepQuantum.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace beebium {

static constexpr uint64_t PARASITE_CLOCK_HZ = 3'000'000;

void SecondProcessor65C02Extension::init(ExtensionContext& ctx)
{
    // Obtain the Tube connector from the host.
    tube_socket_ = &ctx.get<TubeSocket>();

    // Load the Tube client ROM.
    std::array<uint8_t, 2048> rom{};
    if (!load_rom(rom)) {
        throw std::runtime_error(
            "SecondProcessor65C02Extension: failed to load Tube client ROM");
    }

    // Create components.
    tube_ula_ = std::make_unique<TubeUla>();
    runner_ = std::make_unique<ParasiteRunner>(*tube_ula_, rom);
    runner_->reset();

    // Install the TubeUla as the host-side backend.
    tube_socket_->install_backend(tube_ula_.get());

    // Create pacing clock.
    PlatformSleep sleeper;
    auto quantum = measure_sleep_quantum(sleeper);
    PacingConfig pacing_config{
        .base_clock_hz = PARASITE_CLOCK_HZ,
        .pacing_hz = 200,
        .speed_multiplier = 1.0
    };
    pacing_clock_ = std::make_unique<PacingClock>(
        pacing_config, quantum, std::move(sleeper));

    // Create debugger service (wraps ParasiteRunner with the same
    // DebuggerControlServiceImpl template used by the host debugger).
    debugger_service_ = std::make_unique<service::DebuggerControlServiceImpl<ParasiteRunner>>(*runner_);

    // Start the parasite thread.
    running_.store(true, std::memory_order_release);
    parasite_thread_ = std::thread([this] { run_parasite(); });

    std::cout << "  65C02 coprocessor started at "
              << PARASITE_CLOCK_HZ / 1'000'000 << " MHz\n";
}

void SecondProcessor65C02Extension::shutdown()
{
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    // Signal the runner to stop and unblock the pacing wait.
    if (runner_)
        runner_->request_shutdown();
    if (pacing_clock_)
        pacing_clock_->request_stop();

    // Join the thread.
    if (parasite_thread_.joinable())
        parasite_thread_.join();

    // Stop the pacing timer thread.
    if (pacing_clock_)
        pacing_clock_->stop();

    // Uninstall backend from host.
    if (tube_socket_)
        tube_socket_->install_backend(nullptr);

    debugger_service_.reset();
    pacing_clock_.reset();
    runner_.reset();
    tube_ula_.reset();
}

void SecondProcessor65C02Extension::run_parasite()
{
    pacing_clock_->start();

    while (running_.load(std::memory_order_acquire) &&
           !runner_->shutdown_requested())
    {
        pacing_clock_->wait_for_tick();
        if (!running_.load(std::memory_order_acquire))
            break;
        uint64_t cycles = pacing_clock_->cycles_for_next_tick();
        runner_->run(cycles);
        pacing_clock_->report_cycles(runner_->cycle_count());
    }

    // Stop pacing before thread exits, not after.
}

bool SecondProcessor65C02Extension::load_rom(std::array<uint8_t, 2048>& rom) const
{
    static constexpr const char* ROM_FILENAME = "acorn-tube-6502_1_10.rom";

    // Check explicit config first.
    auto rom_config = config_value("rom");
    if (rom_config) {
        std::filesystem::path rom_filepath(*rom_config);
        std::ifstream file(rom_filepath, std::ios::binary);
        if (!file.good()) {
            std::cerr << "Error: cannot open ROM file: " << rom_filepath << "\n";
            return false;
        }
        file.read(reinterpret_cast<char*>(rom.data()), 2048);
        return file.gcount() == 2048;
    }

    // Search standard ROM locations.
    auto exe_dirpath = std::filesystem::current_path();  // Fallback
    // Try sibling "share" directory, then "roms" directory.
    for (const auto& dir : {
        exe_dirpath / "share" / "beebium" / "roms",
        exe_dirpath / "roms",
        std::filesystem::path("/usr/local/share/beebium/roms"),
        std::filesystem::path("/usr/share/beebium/roms"),
    }) {
        auto filepath = dir / ROM_FILENAME;
        if (std::filesystem::exists(filepath)) {
            std::ifstream file(filepath, std::ios::binary);
            if (file.good()) {
                file.read(reinterpret_cast<char*>(rom.data()), 2048);
                if (file.gcount() == 2048) return true;
            }
        }
    }

    std::cerr << "Error: Tube client ROM not found: " << ROM_FILENAME << "\n";
    return false;
}

std::vector<grpc::Service*> SecondProcessor65C02Extension::grpc_services()
{
    // The parasite DebuggerService is not registered here because it uses
    // the same DebuggerControl proto service name as the host. Registering
    // both on the same gRPC server would cause the parasite to override the
    // host. A separate ParasiteDebuggerControl proto service is needed.
    // For now, the debugger_service_ member is used internally for
    // breakpoint/watchpoint management and cross-processor stop.
    return {};
}

}  // namespace beebium
