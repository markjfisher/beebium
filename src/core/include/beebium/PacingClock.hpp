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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace beebium {

/// Real-time pacing clock for emulation (PWM mode).
///
/// Uses a dedicated timer thread to generate fixed-interval ticks. The
/// emulation thread asks for a variable number of cycles per tick, computed
/// by a deficit-based controller that maintains the correct average clock rate.
///
/// The sleep is always interruptible by I/O pending, giving low latency
/// for Tube handshakes. The controller compensates by reducing cycles on
/// shortened ticks and catching up on subsequent full ticks.
///
/// Usage:
///   PacingClock clock(config);
///   clock.start();
///   while (running) {
///       clock.wait_for_tick();
///       uint64_t cycles = clock.cycles_for_next_tick();
///       machine.run(cycles);
///       clock.report_cycles(machine.cycle_count());
///   }
///   clock.stop();
///
class PacingClock {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::nanoseconds;
    using TimePoint = Clock::time_point;

    explicit PacingClock(const PacingConfig& config,
                         std::atomic<bool>* io_pending = nullptr)
        : config_(config)
        , interval_(config.tick_interval())
        , controller_(config.base_clock_hz, config.pacing_hz)
        , running_(false)
        , tick_ready_(false)
        , paused_(false)
        , io_pending_(io_pending) {}

    ~PacingClock() { stop(); }

    PacingClock(const PacingClock&) = delete;
    PacingClock& operator=(const PacingClock&) = delete;
    PacingClock(PacingClock&&) = delete;
    PacingClock& operator=(PacingClock&&) = delete;

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
            std::lock_guard<std::mutex> lock(mutex_);
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

    /// Called by emulation thread after each run() to report cycle count.
    void report_cycles(uint64_t total_cycles) {
        total_cycles_.store(total_cycles, std::memory_order_release);
    }

    /// Called by emulation thread to get cycles for the next tick.
    /// Uses the deficit controller with current wall-clock and cycle count.
    uint64_t cycles_for_next_tick() {
        if (config_.is_unlimited()) {
            return config_.cycles_per_tick();
        }
        auto now = Clock::now();
        auto wall_ns = std::chrono::duration_cast<Duration>(
            now - start_time_).count();
        uint64_t cycles = total_cycles_.load(std::memory_order_acquire);
        return controller_.update(wall_ns, cycles);
    }

    /// Legacy accessor for non-paced mode.
    uint64_t cycles_per_tick() const { return config_.cycles_per_tick(); }

    /// Blocks until next tick is ready.
    void wait_for_tick() {
        if (config_.is_unlimited()) return;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100),
                     [this] { return tick_ready_ || !running_; });
        tick_ready_ = false;
    }

    const PacingConfig& config() const { return config_; }

    void set_speed_multiplier(double multiplier) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.speed_multiplier = multiplier;
    }

    double speed_multiplier() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_.speed_multiplier;
    }

    void pause() {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = true;
    }

    void resume() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            paused_ = false;
            start_time_ = Clock::now();
            total_cycles_.store(0, std::memory_order_relaxed);
            controller_.reset();
        }
        pause_cv_.notify_one();
    }

    bool is_paused() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return paused_;
    }

    struct TimingStats {
        uint64_t ticks_executed;
        uint64_t ticks_skipped;
        uint64_t ticks_io_woken;
        double controller_drift;
        double controller_integral;
    };

    TimingStats timing_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {
            ticks_executed_,
            ticks_skipped_,
            ticks_io_woken_,
            controller_.last_drift(),
            controller_.integral()
        };
    }

private:
    void timer_loop() {
        auto next_tick = Clock::now() + interval_;

        while (running_) {
            // Handle pause
            {
                std::unique_lock<std::mutex> lock(mutex_);
                pause_cv_.wait(lock, [this] { return !paused_ || !running_; });
                if (!running_) break;
            }

            // Unlimited mode: signal immediately
            if (config_.is_unlimited()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    tick_ready_ = true;
                    ++ticks_executed_;
                }
                cv_.notify_one();
                std::this_thread::yield();
                continue;
            }

            // Sleep until next tick, interruptible by I/O pending.
            // When I/O wakes us early, we signal a tick immediately
            // (with reduced cycles from the deficit controller) but
            // do NOT advance next_tick -- the remainder of the interval
            // is used for the next sleep, allowing multiple I/O wakeups
            // within a single base interval.
            auto now = Clock::now();
            bool io_woke = false;
            if (next_tick > now) {
                if (io_pending_) {
                    // Spin-check io_pending with brief yields. Using
                    // sleep_for would add ~200us+ minimum latency per
                    // R2 byte exchange, limiting Tube throughput. Spinning
                    // gives sub-microsecond response but uses more CPU
                    // during I/O-active periods. When idle (no I/O pending),
                    // we exit the loop at next_tick and the outer loop's
                    // normal sleep takes over.
                    while (Clock::now() < next_tick && running_) {
                        if (io_pending_->load(std::memory_order_relaxed)) {
                            io_pending_->store(false, std::memory_order_relaxed);
                            io_woke = true;
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                ++ticks_io_woken_;
                            }
                            break;
                        }
                        std::this_thread::yield();
                    }
                } else {
                    std::this_thread::sleep_until(next_tick);
                }
            }

            // Signal emulation thread
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tick_ready_ = true;
                ++ticks_executed_;
            }
            cv_.notify_one();

            // Only advance the deadline when the full interval elapsed
            // (not when woken early by I/O). This allows multiple short
            // ticks within one base interval for rapid Tube handshakes.
            if (!io_woke) {
                next_tick += interval_;
                now = Clock::now();
                if (next_tick + interval_ < now) {
                    auto behind = (now - next_tick) / interval_;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        ticks_skipped_ += static_cast<uint64_t>(behind);
                    }
                    next_tick += interval_ * (behind + 1);
                }
            }
        }
    }

    // Configuration
    PacingConfig config_;
    Duration interval_;

    // Deficit controller
    PacingController controller_;

    // Stats
    uint64_t ticks_executed_ = 0;
    uint64_t ticks_skipped_ = 0;
    uint64_t ticks_io_woken_ = 0;

    // Timing
    TimePoint start_time_;
    std::atomic<uint64_t> total_cycles_{0};

    // Thread control
    std::atomic<bool> running_;
    std::thread timer_thread_;

    // Synchronization
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable pause_cv_;
    bool tick_ready_;
    bool paused_;

    // I/O pending flag
    std::atomic<bool>* io_pending_ = nullptr;
};

} // namespace beebium
