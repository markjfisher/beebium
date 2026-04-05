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

#include "PacingConfig.hpp"
#include "PacingController.hpp"
#include "PlatformSleep.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace beebium {

/// Real-time pacing clock with smooth progress.
///
/// Every tick: one fixed-duration minimum-length OS sleep, then the
/// emulation thread runs a variable number of cycles computed by a
/// deficit controller. The sleep quantum is measured at startup to
/// adapt to the platform.
///
/// Progress is perfectly smooth: every tick is identical in structure
/// (sleep + run). Only the cycle count varies, and it changes gradually.
///
/// Usage:
///   PacingClock clock(config, quantum);
///   clock.start();
///   while (running) {
///       clock.wait_for_tick();
///       uint64_t n = clock.cycles_for_next_tick();
///       machine.run(n);
///       clock.report_cycles(machine.cycle_count());
///   }
///   clock.stop();
///
class PacingClock {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::nanoseconds;

    /// @param config    Pacing configuration (target clock rate, speed).
    /// @param quantum   Measured sleep quantum from measure_sleep_quantum().
    /// @param sleeper   Platform-specific sleep implementation (moved in).
    /// @param io_pending Optional I/O pending flag for sub-quantum wakeup.
    PacingClock(const PacingConfig& config, Duration quantum,
                PlatformSleep sleeper,
                std::atomic<bool>* io_pending = nullptr)
        : config_(config)
        , quantum_(quantum)
        , controller_(config.base_clock_hz, quantum.count())
        , sleeper_(std::move(sleeper))
        , io_pending_(io_pending) {}

    ~PacingClock() { stop(); }

    PacingClock(const PacingClock&) = delete;
    PacingClock& operator=(const PacingClock&) = delete;

    void start() {
        if (running_) return;
        running_ = true;
        paused_ = false;
        tick_ready_ = false;
        start_time_ = Clock::now();
        controller_.reset();
        timer_thread_ = std::thread(&PacingClock::timer_loop, this);
    }

    void request_stop() {
        running_ = false;
        {
            std::lock_guard lock(mutex_);
            tick_ready_ = true;
        }
        cv_.notify_all();
        pause_cv_.notify_all();
    }

    void stop() {
        if (!timer_thread_.joinable()) return;
        request_stop();
        timer_thread_.join();
    }

    bool is_running() const { return running_; }

    /// Report the emulation's current cycle count (after each run).
    void report_cycles(uint64_t total_cycles) {
        total_cycles_.store(total_cycles, std::memory_order_release);
    }

    /// Get cycles to execute in the next tick (deficit controller).
    uint64_t cycles_for_next_tick() {
        if (config_.is_unlimited()) {
            return controller_.nominal_cycles();
        }
        auto now = Clock::now();
        auto wall_ns = std::chrono::duration_cast<Duration>(
            now - start_time_).count();
        auto cycles = total_cycles_.load(std::memory_order_acquire);
        return controller_.update(wall_ns, cycles);
    }

    /// Legacy: fixed cycles per tick for non-paced mode.
    uint64_t cycles_per_tick() const {
        return config_.cycles_per_tick();
    }

    /// Block until next tick is ready.
    void wait_for_tick() {
        if (config_.is_unlimited()) return;
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100),
                     [this] { return tick_ready_ || !running_; });
        tick_ready_ = false;
    }

    const PacingConfig& config() const { return config_; }
    Duration quantum() const { return quantum_; }

    void set_speed_multiplier(double m) {
        std::lock_guard lock(mutex_);
        config_.speed_multiplier = m;
    }

    double speed_multiplier() const {
        std::lock_guard lock(mutex_);
        return config_.speed_multiplier;
    }

    void pause() {
        std::lock_guard lock(mutex_);
        paused_ = true;
    }

    void resume() {
        {
            std::lock_guard lock(mutex_);
            paused_ = false;
            start_time_ = Clock::now();
            total_cycles_.store(0, std::memory_order_relaxed);
            controller_.reset();
        }
        pause_cv_.notify_one();
    }

    bool is_paused() const {
        std::lock_guard lock(mutex_);
        return paused_;
    }

    struct TimingStats {
        uint64_t ticks_executed;
        uint64_t ticks_io_woken;
        double controller_deficit;
        double controller_integral;  // Always 0 (no integral in deficit mode)
    };

    TimingStats timing_stats() const {
        std::lock_guard lock(mutex_);
        return {
            ticks_executed_,
            ticks_io_woken_,
            controller_.last_deficit(),
            0.0
        };
    }

private:
    void timer_loop() {
        while (running_) {
            // Handle pause
            {
                std::unique_lock lock(mutex_);
                pause_cv_.wait(lock, [this] { return !paused_ || !running_; });
                if (!running_) break;
            }

            // Unlimited mode: signal immediately
            if (config_.is_unlimited()) {
                {
                    std::lock_guard lock(mutex_);
                    tick_ready_ = true;
                    ++ticks_executed_;
                }
                cv_.notify_one();
                std::this_thread::yield();
                continue;
            }

            // One fixed-duration sleep. Optionally interruptible by I/O.
            if (io_pending_) {
                // Interruptible: sleep in short intervals, check io_pending
                auto deadline = Clock::now() + quantum_;
                static constexpr auto poll_interval =
                    std::chrono::microseconds(100);
                while (Clock::now() < deadline && running_) {
                    if (io_pending_->load(std::memory_order_relaxed)) {
                        io_pending_->store(false, std::memory_order_relaxed);
                        {
                            std::lock_guard lock(mutex_);
                            ++ticks_io_woken_;
                        }
                        break;
                    }
                    auto remaining = deadline - Clock::now();
                    if (remaining > poll_interval)
                        sleeper_.sleep(poll_interval);
                    else if (remaining > Duration::zero())
                        sleeper_.sleep(std::chrono::duration_cast<Duration>(remaining));
                }
            } else {
                // Non-interruptible: single efficient sleep
                sleeper_.sleep(quantum_);
            }

            // Signal emulation thread
            {
                std::lock_guard lock(mutex_);
                tick_ready_ = true;
                ++ticks_executed_;
            }
            cv_.notify_one();
        }
    }

    // Configuration
    PacingConfig config_;
    Duration quantum_;

    // Deficit controller
    PacingController controller_;

    // Platform-specific high-resolution sleep
    PlatformSleep sleeper_; // must be after controller_ to match initialiser order

    // Stats
    uint64_t ticks_executed_ = 0;
    uint64_t ticks_io_woken_ = 0;

    // Timing
    Clock::time_point start_time_;
    std::atomic<uint64_t> total_cycles_{0};

    // Thread control
    std::atomic<bool> running_{false};
    std::thread timer_thread_;

    // Synchronization
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable pause_cv_;
    bool tick_ready_{false};
    bool paused_{false};

    // I/O pending flag (optional)
    std::atomic<bool>* io_pending_ = nullptr;
};

} // namespace beebium
