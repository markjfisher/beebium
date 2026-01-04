// Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_INDICATORS_INDICATOR_FILTER_HPP
#define BEEBIUM_INDICATORS_INDICATOR_FILTER_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>

namespace beebium {

/// Base interface for indicator filters.
///
/// Filters transform raw indicator updates (0 or 255) into smoothed output values (0-255).
/// Different filter policies handle different use cases:
/// - PassthroughFilter: No filtering, immediate updates
/// - DebounceFilter: Requires stable input for a duration before changing
/// - DutyCycleFilter: Computes PWM duty cycle over a time window
class IndicatorFilter {
public:
    using time_point = std::chrono::steady_clock::time_point;

    virtual ~IndicatorFilter() = default;

    /// Accept a raw update from the emulation.
    /// Called from the emulation thread; must be fast and non-blocking.
    /// @param value The raw indicator value (typically 0 or 255)
    /// @param timestamp Wall-clock time of the update
    virtual void update(uint8_t value, time_point timestamp) = 0;

    /// Compute the filtered output value.
    /// Called periodically (~50Hz) by the consumer thread.
    /// @param now Current wall-clock time
    /// @return Filtered value in range 0-255
    virtual uint8_t sample(time_point now) = 0;
};

/// Pass-through filter: returns the most recent value unchanged.
///
/// Use for indicators that don't need filtering (e.g., motor state that's
/// already debounced by the hardware).
class PassthroughFilter : public IndicatorFilter {
public:
    void update(uint8_t value, time_point /*timestamp*/) override {
        value_.store(value, std::memory_order_relaxed);
    }

    uint8_t sample(time_point /*now*/) override {
        return value_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint8_t> value_{0};
};

/// Debounce filter: only changes output after input is stable for a duration.
///
/// Use for indicators that may have spurious transients that should be ignored.
class DebounceFilter : public IndicatorFilter {
public:
    explicit DebounceFilter(std::chrono::milliseconds min_stable)
        : min_stable_(min_stable) {}

    void update(uint8_t value, time_point timestamp) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (value != pending_value_) {
            pending_value_ = value;
            last_change_ = timestamp;
        }
    }

    uint8_t sample(time_point now) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_value_ != stable_value_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_change_);
            if (elapsed >= min_stable_) {
                stable_value_ = pending_value_;
            }
        }
        return stable_value_;
    }

private:
    std::chrono::milliseconds min_stable_;
    std::mutex mutex_;
    uint8_t pending_value_ = 0;
    uint8_t stable_value_ = 0;
    time_point last_change_{};
};

/// Duty cycle filter: computes the on-time fraction over a sliding window.
///
/// Use for LEDs that are PWM-modulated by software (e.g., BBC Micro's Caps Lock LED).
/// The output represents brightness as a value 0-255.
class DutyCycleFilter : public IndicatorFilter {
public:
    explicit DutyCycleFilter(std::chrono::milliseconds window)
        : window_(window) {}

    void update(uint8_t value, time_point timestamp) override {
        std::lock_guard<std::mutex> lock(mutex_);

        // Record transition
        bool is_on = (value > 127);
        if (is_on != current_state_) {
            transitions_.push_back({timestamp, is_on});
            current_state_ = is_on;
        }
    }

    uint8_t sample(time_point now) override {
        std::lock_guard<std::mutex> lock(mutex_);

        auto window_start = now - window_;

        // Remove transitions older than the window
        while (!transitions_.empty() && transitions_.front().timestamp < window_start) {
            // Before discarding, we need to know what state we were in at window_start
            initial_state_at_window_ = transitions_.front().to_on;
            transitions_.pop_front();
        }

        // Calculate on-time within the window
        auto on_time = std::chrono::nanoseconds{0};
        bool state = initial_state_at_window_;
        time_point segment_start = window_start;

        for (const auto& t : transitions_) {
            if (state) {
                on_time += t.timestamp - segment_start;
            }
            state = t.to_on;
            segment_start = t.timestamp;
        }

        // Account for time from last transition to now
        if (state) {
            on_time += now - segment_start;
        }

        // Convert to duty cycle (0-255)
        auto window_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(window_);
        if (window_ns.count() == 0) {
            return current_state_ ? 255 : 0;
        }

        auto duty_cycle = (on_time.count() * 255) / window_ns.count();
        return static_cast<uint8_t>(std::min(duty_cycle, int64_t{255}));
    }

private:
    struct Transition {
        time_point timestamp;
        bool to_on;
    };

    std::chrono::milliseconds window_;
    std::mutex mutex_;
    std::deque<Transition> transitions_;
    bool current_state_ = false;
    bool initial_state_at_window_ = false;
};

} // namespace beebium

#endif // BEEBIUM_INDICATORS_INDICATOR_FILTER_HPP
