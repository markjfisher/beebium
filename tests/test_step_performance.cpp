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

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <beebium/Machines.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>

#include <array>
#include <memory>

using namespace beebium;

namespace {

constexpr uint64_t kCyclesPerTick = 10000;

// Minimal OneMHzBusDevice stub with no-op implementations.
// Used to isolate the dispatch overhead from actual device logic.
struct NullOneMHzBusDevice : OneMHzBusDevice {
    uint8_t read(uint16_t) override { return 0xFF; }
    void write(uint16_t, uint8_t) override {}
    // tick() and irq_pending() use default no-op implementations
};

// Load a minimal MOS (NOP-filled with reset vector at 0x0400) and reset the CPU.
template <typename MachineType>
void setup_minimal_mos(MachineType& machine) {
    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);  // Fill with NOPs
    mos[0x3FFC] = 0x00;  // Reset vector low byte -> 0x0400
    mos[0x3FFD] = 0x04;  // Reset vector high byte
    machine.memory().load_mos(mos.data(), mos.size());
    M6502_Reset(&machine.cpu());
    // Complete reset sequence (7 cycles)
    machine.step_instruction();
}

}  // namespace

TEST_CASE("step() throughput", "[performance][!benchmark]") {

    BENCHMARK_ADVANCED("bare machine")(Catch::Benchmark::Chronometer meter) {
        ModelBRomRamBoard machine;
        setup_minimal_mos(machine);
        meter.measure([&] { machine.run(kCyclesPerTick); });
    };

    BENCHMARK_ADVANCED("econet idle")(Catch::Benchmark::Chronometer meter) {
        ModelBRomRamBoard machine;
        setup_minimal_mos(machine);
        auto backend = std::make_unique<TestBackend>();
        machine.memory().econet_socket.enable(254, std::move(backend));
        meter.measure([&] { machine.run(kCyclesPerTick); });
    };

    BENCHMARK_ADVANCED("1 bus device")(Catch::Benchmark::Chronometer meter) {
        ModelBRomRamBoard machine;
        setup_minimal_mos(machine);
        NullOneMHzBusDevice dev;
        // Claim a single FRED address (offset 0x40 = 0xFC40)
        machine.memory().one_mhz_bus().claim_addresses(0x40, 0x40, dev);
        meter.measure([&] { machine.run(kCyclesPerTick); });
    };

    BENCHMARK_ADVANCED("3 bus devices")(Catch::Benchmark::Chronometer meter) {
        ModelBRomRamBoard machine;
        setup_minimal_mos(machine);
        NullOneMHzBusDevice dev1, dev2, dev3;
        machine.memory().one_mhz_bus().claim_addresses(0x40, 0x40, dev1);
        machine.memory().one_mhz_bus().claim_addresses(0x50, 0x50, dev2);
        machine.memory().one_mhz_bus().claim_addresses(0x60, 0x60, dev3);
        meter.measure([&] { machine.run(kCyclesPerTick); });
    };

    BENCHMARK_ADVANCED("econet + 1 bus device")(Catch::Benchmark::Chronometer meter) {
        ModelBRomRamBoard machine;
        setup_minimal_mos(machine);
        auto backend = std::make_unique<TestBackend>();
        machine.memory().econet_socket.enable(254, std::move(backend));
        NullOneMHzBusDevice dev;
        machine.memory().one_mhz_bus().claim_addresses(0x40, 0x40, dev);
        meter.measure([&] { machine.run(kCyclesPerTick); });
    };

    BENCHMARK_ADVANCED("econet + 3 bus devices")(Catch::Benchmark::Chronometer meter) {
        ModelBRomRamBoard machine;
        setup_minimal_mos(machine);
        auto backend = std::make_unique<TestBackend>();
        machine.memory().econet_socket.enable(254, std::move(backend));
        NullOneMHzBusDevice dev1, dev2, dev3;
        machine.memory().one_mhz_bus().claim_addresses(0x40, 0x40, dev1);
        machine.memory().one_mhz_bus().claim_addresses(0x50, 0x50, dev2);
        machine.memory().one_mhz_bus().claim_addresses(0x60, 0x60, dev3);
        meter.measure([&] { machine.run(kCyclesPerTick); });
    };
}
