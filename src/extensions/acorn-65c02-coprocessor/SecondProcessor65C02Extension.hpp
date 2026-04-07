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

#pragma once

#include "beebium/extension/PeripheralExtension.hpp"
#include "beebium/tube/ParasiteRunner.hpp"
#include "beebium/tube/TubeSocket.hpp"
#include "beebium/tube/TubeUla.hpp"
#include "beebium/PacingClock.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <thread>

namespace beebium {

// Acorn 65C02 3 MHz second processor, implemented as a Peripheral Extension.
//
// Owns everything on the parasite side of the Tube cable:
//   - TubeUla (thread-safe bridging hardware)
//   - ParasiteRunner (CPU, memory map, boot ROM, breakpoints)
//   - Execution thread (parasite runs asynchronously)
//   - PacingClock (3 MHz parasite pacing)
//
// The TubeUla is installed into the host's TubeSocket as the backend.
// Host register access goes through TubeUla::host_read/host_write from the
// host thread; parasite register access goes through TubeUla::parasite_read/
// parasite_write from the extension's thread. The mutex in TubeUla serialises
// concurrent access.
//
// Lifecycle:
//   init()     -- load ROM, create components, install backend, start thread
//   shutdown() -- stop thread, uninstall backend

class SecondProcessor65C02Extension : public PeripheralExtension {
public:
    SecondProcessor65C02Extension() = default;
    ~SecondProcessor65C02Extension() override { shutdown(); }

    // --- PeripheralExtension interface ---

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"tube"};
        return deps;
    }

    std::span<const std::string_view> provides() const override {
        return {};
    }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    std::vector<grpc::Service*> grpc_services() override { return {}; }

    // --- Accessors ---

    TubeUla* tube_ula() { return tube_ula_.get(); }
    ParasiteRunner* runner() { return runner_.get(); }
    bool running() const { return running_.load(std::memory_order_acquire); }

private:
    void run_parasite();
    bool load_rom(std::array<uint8_t, 2048>& rom) const;

    std::unique_ptr<TubeUla> tube_ula_;
    std::unique_ptr<ParasiteRunner> runner_;
    std::unique_ptr<PacingClock> pacing_clock_;
    std::thread parasite_thread_;
    std::atomic<bool> running_{false};
    TubeSocket* tube_socket_ = nullptr;  // non-owning, from ExtensionContext
};

}  // namespace beebium
