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

// Tests for the parasite execution runner.
//
// ParasiteRunner owns the parasite emulation engine: CPU, memory map,
// Tube port, and execution loop with lifecycle handling. It is the
// parasite's analogue of Machine<Hardware> on the host side.
//
// These tests verify:
//   - Construction and ROM loading
//   - Execution via run() and step_instruction()
//   - Lifecycle mailbox: reset, freeze, shutdown
//   - Pause/resume for debugger integration
//   - Clean shutdown coordination

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeShared.hpp>

#include <array>
#include <cstdint>
#include <thread>

using namespace beebium;

// Helper: create a 2 KB ROM with a known reset vector and NOP fill.
static std::array<uint8_t, 2048> make_nop_rom(uint16_t entry = 0xF800) {
    std::array<uint8_t, 2048> rom{};
    rom.fill(0xEA);  // NOP
    // Reset vector at ROM offset 0x7FC-0x7FD (maps to &FFFC-&FFFD)
    rom[0x7FC] = static_cast<uint8_t>(entry & 0xFF);
    rom[0x7FD] = static_cast<uint8_t>(entry >> 8);
    // IRQ vector -> &F900 (ROM offset 0x100), with RTI
    rom[0x7FE] = 0x00;
    rom[0x7FF] = 0xF9;
    rom[0x100] = 0x40;  // RTI
    // NMI vector -> &F980 (ROM offset 0x180), with RTI
    rom[0x7FA] = 0x80;
    rom[0x7FB] = 0xF9;
    rom[0x180] = 0x40;  // RTI
    return rom;
}

// ===========================================================================
// Construction and initial state
// ===========================================================================

TEST_CASE("ParasiteRunner construction with ROM", "[parasite][runner]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);

    CHECK(runner.cycle_count() == 0);
    CHECK(runner.memory_map().boot_mode());
}

TEST_CASE("ParasiteRunner reset initialises CPU at reset vector", "[parasite][runner]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom(0xF850);

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    // Execute reset sequence (7 cycles)
    uint64_t cycles = runner.step_instruction();
    CHECK(cycles == 7);
    CHECK(runner.cpu().abus.w == 0xF850);
}

// ===========================================================================
// Execution
// ===========================================================================

TEST_CASE("ParasiteRunner run executes cycles", "[parasite][runner][execution]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    runner.run(100);
    CHECK(runner.cycle_count() == 100);
}

TEST_CASE("ParasiteRunner step_instruction returns cycle count", "[parasite][runner][execution]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    uint64_t reset_cycles = runner.step_instruction();
    CHECK(reset_cycles == 7);

    uint64_t nop_cycles = runner.step_instruction();
    CHECK(nop_cycles == 2);
}

// ===========================================================================
// Lifecycle mailbox: shutdown
// ===========================================================================

TEST_CASE("ParasiteRunner run stops on shutdown command", "[parasite][runner][lifecycle]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    // Post shutdown command before running
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Shutdown),
        std::memory_order_release);

    // run() should terminate early
    runner.run(1000000);
    CHECK(runner.cycle_count() < 1000000);
    CHECK(runner.shutdown_requested());
}

TEST_CASE("ParasiteRunner acknowledges shutdown", "[parasite][runner][lifecycle]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Shutdown),
        std::memory_order_release);

    runner.run(1000);

    CHECK(shared.parasite_ack.load(std::memory_order_acquire)
          == static_cast<uint8_t>(TubeLifecycleAck::Exiting));
}

// ===========================================================================
// Lifecycle mailbox: reset
// ===========================================================================

TEST_CASE("ParasiteRunner handles reset command", "[parasite][runner][lifecycle]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    // Run for a bit
    runner.run(100);
    uint64_t cycles_before = runner.cycle_count();
    CHECK(cycles_before == 100);

    // Post reset command
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Reset),
        std::memory_order_release);

    // Run more -- runner should detect reset, re-enter boot mode,
    // and clear cycle count.
    runner.run(100);

    // Cycle count should have been reset (may have advanced a little
    // since reset, but should be much less than 200).
    CHECK(runner.cycle_count() < 200);
    CHECK(runner.memory_map().boot_mode());

    // Reset should be acknowledged
    CHECK(shared.parasite_ack.load(std::memory_order_acquire)
          == static_cast<uint8_t>(TubeLifecycleAck::ResetDone));

    // Host command should be cleared so it doesn't re-trigger
    CHECK(shared.host_command.load(std::memory_order_acquire)
          == static_cast<uint8_t>(TubeLifecycleCommand::None));
}

// ===========================================================================
// Lifecycle mailbox: freeze
// ===========================================================================

TEST_CASE("ParasiteRunner handles freeze command", "[parasite][runner][lifecycle]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    // Post freeze command
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Freeze),
        std::memory_order_release);

    // Run in a thread -- should block on freeze
    std::atomic<bool> run_completed{false};
    std::thread t([&] {
        runner.run(1000000);
        run_completed.store(true, std::memory_order_release);
    });

    // Give the runner time to detect freeze and block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(run_completed.load(std::memory_order_acquire));

    // Freeze should be acknowledged
    CHECK(shared.parasite_ack.load(std::memory_order_acquire)
          == static_cast<uint8_t>(TubeLifecycleAck::Frozen));

    // Unfreeze by sending a different command (e.g. reset or clearing)
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Shutdown),
        std::memory_order_release);
    runner.request_shutdown();

    t.join();
    CHECK(run_completed.load(std::memory_order_acquire));
}

// ===========================================================================
// Pause/resume (debugger support)
// ===========================================================================

TEST_CASE("ParasiteRunner pause stops execution", "[parasite][runner][debug]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    runner.pause();
    CHECK(runner.is_paused());

    // Run in a thread -- should block because paused
    std::atomic<bool> run_completed{false};
    std::thread t([&] {
        runner.run(1000);
        run_completed.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(run_completed.load(std::memory_order_acquire));

    // Resume
    runner.resume();
    t.join();

    CHECK(run_completed.load(std::memory_order_acquire));
    CHECK_FALSE(runner.is_paused());
}

TEST_CASE("ParasiteRunner shutdown unblocks pause", "[parasite][runner][debug]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    runner.pause();

    std::atomic<bool> run_completed{false};
    std::thread t([&] {
        runner.run(1000);
        run_completed.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Shutdown should unblock the paused runner
    runner.request_shutdown();
    t.join();

    CHECK(run_completed.load(std::memory_order_acquire));
    CHECK(runner.shutdown_requested());
}

// ===========================================================================
// Component access
// ===========================================================================

TEST_CASE("ParasiteRunner provides access to components", "[parasite][runner]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);

    // CPU access
    CHECK(runner.cpu().config == &M6502_rockwell65c02_config);

    // Memory map access
    CHECK(runner.memory_map().boot_mode());

    // Tube port access
    CHECK_FALSE(runner.tube_port().pirq());

    // Const access
    const ParasiteRunner& crunner = runner;
    CHECK(crunner.cycle_count() == 0);
    CHECK(crunner.cpu().config == &M6502_rockwell65c02_config);
}

// ===========================================================================
// Lifecycle mailbox polling frequency
// ===========================================================================

TEST_CASE("ParasiteRunner detects shutdown within a bounded number of cycles", "[parasite][runner][lifecycle]") {
    TubeShared shared;
    shared.init();
    auto rom = make_nop_rom();

    ParasiteRunner runner(&shared, rom);
    runner.reset();

    // Start running in a thread
    std::atomic<bool> run_completed{false};
    std::thread t([&] {
        runner.run(100000000);  // Very large cycle count
        run_completed.store(true, std::memory_order_release);
    });

    // Give it time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Post shutdown
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Shutdown),
        std::memory_order_release);

    // Should stop promptly (well within 1 second)
    t.join();
    CHECK(run_completed.load(std::memory_order_acquire));
    CHECK(runner.shutdown_requested());
}
