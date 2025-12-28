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

#pragma once

#include "PacingConfig.hpp"

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
/// monitors wall-clock time and signals the emulation thread when it should
/// run the next batch of cycles.
///
/// Key features:
/// - Adaptive sleep optimization: learns typical sleep overshoot and adjusts
///   safety margins to minimize CPU-burning spin-wait time
/// - Graceful handling of falling behind: skips ticks rather than trying to
///   catch up, preventing death spirals
/// - Support for speed changes at runtime
/// - Clean separation: emulation thread has no timing logic
///
/// Usage:
///   PacingClock clock(config);
///   clock.start();
///   while (running) {
///       machine.run(clock.cycles_per_tick());
///       clock.wait_for_tick();
///   }
///   clock.stop();
///
class PacingClock {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::nanoseconds;
    using TimePoint = Clock::time_point;

    /// Construct a pacing clock with the given configuration.
    explicit PacingClock(const PacingConfig& config)
        : config_(config)
        , interval_(config.tick_interval())
        , safety_margin_(interval_ / 10)  // Start conservative: 10% margin
        , min_margin_(std::chrono::microseconds(100))
        , max_margin_(interval_ / 2)
        , running_(false)
        , tick_ready_(false)
        , paused_(false) {}

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
        if (running_) {
            return;
        }
        running_ = true;
        paused_ = false;
        tick_ready_ = false;
        next_tick_ = Clock::now() + interval_;
        timer_thread_ = std::thread(&PacingClock::timer_loop, this);
    }

    /// Stop the pacing clock. Joins the timer thread.
    void stop() {
        if (!running_) {
            return;
        }
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tick_ready_ = true;  // Unblock any waiting emulation thread
        }
        cv_.notify_all();
        pause_cv_.notify_all();
        if (timer_thread_.joinable()) {
            timer_thread_.join();
        }
    }

    /// Check if the clock is running.
    bool is_running() const {
        return running_;
    }

    /// Called by emulation thread - blocks until next tick is ready.
    /// Returns immediately if running in unlimited mode.
    void wait_for_tick() {
        if (config_.is_unlimited()) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return tick_ready_ || !running_; });
        tick_ready_ = false;
    }

    /// Get the number of cycles to run per tick.
    uint64_t cycles_per_tick() const {
        return config_.cycles_per_tick();
    }

    /// Get the current configuration.
    const PacingConfig& config() const {
        return config_;
    }

    /// Update the speed multiplier at runtime.
    /// Thread-safe: can be called from any thread.
    void set_speed_multiplier(double multiplier) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.speed_multiplier = multiplier;
    }

    /// Get the current speed multiplier.
    double speed_multiplier() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_.speed_multiplier;
    }

    /// Pause the pacing clock. The timer thread will stop signaling ticks
    /// until resume() is called. Used by debugger.
    void pause() {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = true;
    }

    /// Resume the pacing clock after a pause.
    /// Resets timing to avoid trying to catch up for lost time.
    void resume() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            paused_ = false;
            // Reset next_tick to avoid catch-up attempts after pause
            next_tick_ = Clock::now() + interval_;
        }
        pause_cv_.notify_one();
    }

    /// Check if the clock is paused.
    bool is_paused() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return paused_;
    }

    /// Get adaptive timing statistics (for debugging/monitoring).
    struct TimingStats {
        double avg_overshoot_us;      // Average sleep overshoot in microseconds
        double max_recent_overshoot_us; // Recent maximum overshoot
        double safety_margin_us;      // Current safety margin
        uint64_t ticks_executed;      // Total ticks executed
        uint64_t ticks_skipped;       // Ticks skipped due to falling behind
    };

    TimingStats timing_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {
            avg_overshoot_ns_ / 1000.0,
            max_recent_overshoot_ns_ / 1000.0,
            static_cast<double>(safety_margin_.count()) / 1000.0,
            ticks_executed_,
            ticks_skipped_
        };
    }

private:
    void timer_loop() {
        while (running_) {
            // Handle pause state
            {
                std::unique_lock<std::mutex> lock(mutex_);
                pause_cv_.wait(lock, [this] { return !paused_ || !running_; });
                if (!running_) {
                    break;
                }
            }

            // Check if we're in unlimited mode (no pacing)
            if (config_.is_unlimited()) {
                // In unlimited mode, just signal immediately
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    tick_ready_ = true;
                    ++ticks_executed_;
                }
                cv_.notify_one();
                // Brief yield to prevent complete CPU starvation
                std::this_thread::yield();
                continue;
            }

            // Calculate sleep target (interval minus adaptive safety margin)
            Duration current_margin;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                current_margin = safety_margin_;
            }
            auto sleep_target = next_tick_ - current_margin;
            auto before_sleep = Clock::now();

            // Sleep until near the target time
            if (sleep_target > before_sleep) {
                std::this_thread::sleep_until(sleep_target);
            }

            auto after_sleep = Clock::now();

            // Spin-wait for precise timing
            while (Clock::now() < next_tick_ && running_) {
                // Spin (could use _mm_pause() on x86 for power efficiency)
            }

            auto now = Clock::now();

            // Signal emulation thread
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tick_ready_ = true;
                ++ticks_executed_;
            }
            cv_.notify_one();

            // Adapt safety margin based on observed overshoot
            adapt_margin(sleep_target, after_sleep);

            // Advance to next tick
            next_tick_ += interval_;

            // Handle falling behind: if we're more than one interval late,
            // skip ahead rather than trying to catch up
            if (next_tick_ + interval_ < now) {
                auto intervals_behind = (now - next_tick_) / interval_;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ticks_skipped_ += static_cast<uint64_t>(intervals_behind);
                }
                next_tick_ += interval_ * (intervals_behind + 1);
            }
        }
    }

    void adapt_margin(TimePoint sleep_target, TimePoint after_sleep) {
        // How much did we overshoot the sleep target?
        auto overshoot = after_sleep - sleep_target;
        auto overshoot_ns = std::chrono::duration_cast<Duration>(overshoot).count();

        // Clamp negative overshoot (woke up early - rare but possible)
        if (overshoot_ns < 0) {
            overshoot_ns = 0;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Exponential moving average (alpha ~= 0.1)
        avg_overshoot_ns_ = avg_overshoot_ns_ * 0.9 + static_cast<double>(overshoot_ns) * 0.1;

        // Track max overshoot in recent window for safety (with decay)
        max_recent_overshoot_ns_ = std::max(
            max_recent_overshoot_ns_ * 0.95,  // Decay factor
            static_cast<double>(overshoot_ns)
        );

        // New margin: average + headroom based on recent max
        // The 0.5 factor provides buffer for variance
        auto new_margin_ns = static_cast<int64_t>(
            avg_overshoot_ns_ + max_recent_overshoot_ns_ * 0.5
        );

        safety_margin_ = std::clamp(
            Duration(new_margin_ns),
            min_margin_,
            max_margin_
        );
    }

    // Configuration
    PacingConfig config_;
    Duration interval_;
    Duration safety_margin_;
    Duration min_margin_;
    Duration max_margin_;

    // Adaptive timing state
    double avg_overshoot_ns_ = 0.0;
    double max_recent_overshoot_ns_ = 0.0;
    uint64_t ticks_executed_ = 0;
    uint64_t ticks_skipped_ = 0;

    // Timing
    TimePoint next_tick_;

    // Thread control
    std::atomic<bool> running_;
    std::thread timer_thread_;

    // Synchronization
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable pause_cv_;
    bool tick_ready_;
    bool paused_;
};

} // namespace beebium
