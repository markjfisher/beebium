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

/// Real-time pacing clock for emulation.
///
/// Uses a dedicated timer thread to control emulation speed. The timer thread
/// uses a PI controller to compute sleep durations that maintain the correct
/// average emulated clock rate, even during I/O bursts where ticks are skipped.
///
/// Usage:
///   PacingClock clock(config);
///   clock.start();
///   while (running) {
///       machine.run(clock.cycles_per_tick());
///       clock.report_cycles(machine.cycle_count());
///       clock.wait_for_tick();
///   }
///   clock.stop();
///
class PacingClock {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::nanoseconds;
    using TimePoint = Clock::time_point;

    /// @param config     Pacing configuration (clock rate, tick rate, speed).
    /// @param io_pending Optional pointer to an atomic flag that external I/O
    ///   sources can set to request an immediate tick (skip sleeping).
    explicit PacingClock(const PacingConfig& config,
                         std::atomic<bool>* io_pending = nullptr)
        : config_(config)
        , interval_(config.tick_interval())
        , controller_(config.base_clock_hz, config.pacing_hz)
        , running_(false)
        , tick_ready_(false)
        , paused_(false)
        , io_pending_(io_pending) {}

    ~PacingClock() {
        stop();
    }

    // Non-copyable, non-movable (owns thread)
    PacingClock(const PacingClock&) = delete;
    PacingClock& operator=(const PacingClock&) = delete;
    PacingClock(PacingClock&&) = delete;
    PacingClock& operator=(PacingClock&&) = delete;

    /// Start the pacing clock. Spawns the timer thread.
    void start() {
        if (running_) return;
        running_ = true;
        paused_ = false;
        tick_ready_ = false;
        start_time_ = Clock::now();
        controller_.reset();
        timer_thread_ = std::thread(&PacingClock::timer_loop, this);
    }

    /// Request the pacing clock to stop (signal-safe, non-blocking).
    void request_stop() {
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tick_ready_ = true;
        }
        cv_.notify_all();
        pause_cv_.notify_all();
    }

    /// Stop the pacing clock. Joins the timer thread.
    void stop() {
        if (!timer_thread_.joinable()) return;
        request_stop();
        timer_thread_.join();
    }

    bool is_running() const { return running_; }

    /// Called by emulation thread after each run() to report the current
    /// cycle count. The timer thread reads this to compute drift.
    void report_cycles(uint64_t total_cycles) {
        total_cycles_.store(total_cycles, std::memory_order_release);
    }

    /// Called by emulation thread - blocks until next tick is ready.
    void wait_for_tick() {
        if (config_.is_unlimited()) return;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100),
                     [this] { return tick_ready_ || !running_; });
        tick_ready_ = false;
    }

    uint64_t cycles_per_tick() const { return config_.cycles_per_tick(); }
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
            // Reset controller and start time to avoid catch-up after pause
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
        double avg_overshoot_us;
        double max_recent_overshoot_us;
        double safety_margin_us;
        uint64_t ticks_executed;
        uint64_t ticks_skipped;
        uint64_t ticks_io_skipped;
        double controller_drift;
        double controller_integral;
    };

    TimingStats timing_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {
            avg_overshoot_ns_ / 1000.0,
            max_recent_overshoot_ns_ / 1000.0,
            static_cast<double>(safety_margin_.count()) / 1000.0,
            ticks_executed_,
            ticks_skipped_,
            ticks_io_skipped_,
            controller_.last_drift(),
            controller_.integral()
        };
    }

private:
    void timer_loop() {
        while (running_) {
            // Handle pause state
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

            // Check I/O pending flag
            bool io_skip = io_pending_ &&
                io_pending_->load(std::memory_order_acquire);
            if (io_skip) {
                io_pending_->store(false, std::memory_order_relaxed);
            }

            // Read current emulation state for the PI controller
            auto now = Clock::now();
            auto wall_elapsed_ns = std::chrono::duration_cast<Duration>(
                now - start_time_).count();
            uint64_t cycles = total_cycles_.load(std::memory_order_acquire);

            // Ask the PI controller for the recommended sleep duration
            int64_t sleep_ns = controller_.update(wall_elapsed_ns, cycles, io_skip);

            if (io_skip) {
                // I/O pending: override controller and signal immediately.
                // The controller still saw the update (tracking drift/integral)
                // so it knows we skipped sleeping and will compensate later.
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    tick_ready_ = true;
                    ++ticks_executed_;
                    ++ticks_io_skipped_;
                }
                cv_.notify_one();
            } else {
                // Normal path: sleep for the controller-recommended duration

                // Subtract safety margin for spin-wait precision
                Duration current_margin;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    current_margin = safety_margin_;
                }
                auto sleep_duration = Duration(sleep_ns) - current_margin;
                auto sleep_target = now + sleep_duration;

                if (sleep_duration > Duration::zero()) {
                    if (io_pending_) {
                        // Interruptible sleep: short intervals with I/O checks
                        static constexpr auto io_poll_interval =
                            std::chrono::microseconds(200);
                        while (Clock::now() < sleep_target && running_) {
                            if (io_pending_->load(std::memory_order_relaxed))
                                break;
                            auto remaining = sleep_target - Clock::now();
                            if (remaining > io_poll_interval)
                                std::this_thread::sleep_for(io_poll_interval);
                            else if (remaining > Duration::zero())
                                std::this_thread::sleep_for(remaining);
                        }
                    } else {
                        std::this_thread::sleep_until(sleep_target);
                    }
                }

                auto after_sleep = Clock::now();

                // Spin-wait for remaining margin (precision)
                auto tick_deadline = now + Duration(sleep_ns);
                while (Clock::now() < tick_deadline && running_) {
                    if (io_pending_ &&
                        io_pending_->load(std::memory_order_relaxed))
                        break;
                }

                // Signal emulation thread
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    tick_ready_ = true;
                    ++ticks_executed_;
                }
                cv_.notify_one();

                // Adapt safety margin
                if (sleep_duration > Duration::zero()) {
                    adapt_margin(sleep_target, after_sleep);
                }
            }
        }
    }

    void adapt_margin(TimePoint sleep_target, TimePoint after_sleep) {
        auto overshoot = after_sleep - sleep_target;
        auto overshoot_ns = std::chrono::duration_cast<Duration>(overshoot).count();
        if (overshoot_ns < 0) overshoot_ns = 0;

        std::lock_guard<std::mutex> lock(mutex_);
        avg_overshoot_ns_ = avg_overshoot_ns_ * 0.9 + static_cast<double>(overshoot_ns) * 0.1;
        max_recent_overshoot_ns_ = std::max(
            max_recent_overshoot_ns_ * 0.95,
            static_cast<double>(overshoot_ns)
        );
        auto new_margin_ns = static_cast<int64_t>(
            avg_overshoot_ns_ + max_recent_overshoot_ns_ * 0.5
        );
        auto min_m = std::chrono::duration_cast<Duration>(std::chrono::microseconds(100));
        auto max_m = interval_ / 2;
        safety_margin_ = std::clamp(Duration(new_margin_ns), min_m, max_m);
    }

    // Configuration
    PacingConfig config_;
    Duration interval_;

    // PI controller for sleep duration computation
    PacingController controller_;

    // Adaptive sleep margin
    Duration safety_margin_{interval_ / 10};
    double avg_overshoot_ns_ = 0.0;
    double max_recent_overshoot_ns_ = 0.0;

    // Stats
    uint64_t ticks_executed_ = 0;
    uint64_t ticks_skipped_ = 0;
    uint64_t ticks_io_skipped_ = 0;

    // Timing
    TimePoint start_time_;

    // Cycle counter (written by emulation thread, read by timer thread)
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

    // I/O pending flag (optional, cross-process via shared memory)
    std::atomic<bool>* io_pending_ = nullptr;
};

} // namespace beebium
