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

#ifndef BEEBIUM_INDICATORS_INDICATORS_HPP
#define BEEBIUM_INDICATORS_INDICATORS_HPP

#include "beebium/indicators/IndicatorFilter.hpp"
#include <moodycamel/readerwriterqueue.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace beebium {

/// Event representing an indicator value change.
/// Pushed to the event queue by the emulation thread.
struct IndicatorEvent {
    uint16_t id;
    uint8_t value;
    std::chrono::steady_clock::time_point timestamp;
};

/// Manages observable hardware state (LEDs, motors) with configurable filtering.
///
/// Architecture:
/// - Emulation thread pushes events to a lock-free ring buffer (non-blocking)
/// - Consumer thread runs at ~50Hz, drains queue, applies filters, updates published state
/// - gRPC clients read published state via atomic reads
///
/// Each indicator has:
/// - A unique string name (e.g., "caps-lock-led", "floppy-0-activity-led")
/// - A filter policy (passthrough, debounce, duty-cycle)
/// - Optional metadata (label, color, shape) for frontend rendering
class Indicators {
public:
    Indicators() = default;

    ~Indicators() {
        stop();
    }

    // Non-copyable, non-movable (owns thread)
    Indicators(const Indicators&) = delete;
    Indicators& operator=(const Indicators&) = delete;
    Indicators(Indicators&&) = delete;
    Indicators& operator=(Indicators&&) = delete;

    /// Register an indicator with a filter policy.
    /// Must be called before start().
    /// @param name Unique indicator name (e.g., "caps-lock-led")
    /// @param filter Filter policy to apply to updates
    /// @param metadata Key-value pairs for frontend rendering (e.g., {"label", "CAPS LOCK"})
    /// @return ID for efficient updates via set(id, value)
    uint16_t register_indicator(
        const std::string& name,
        std::unique_ptr<IndicatorFilter> filter,
        std::unordered_map<std::string, std::string> metadata = {}) {

        std::unique_lock lock(registry_mutex_);

        uint16_t id = static_cast<uint16_t>(indicators_.size());
        name_to_id_[name] = id;

        IndicatorState state;
        state.name = name;
        state.filter = std::move(filter);
        state.metadata = std::move(metadata);
        state.published_value.store(0, std::memory_order_relaxed);

        indicators_.push_back(std::move(state));

        return id;
    }

    /// Update an indicator by ID (hot path, no string lookup).
    /// Non-blocking; pushes to event queue.
    /// @param id Indicator ID returned by register_indicator()
    /// @param value New value (typically 0 or 255)
    /// @return true if event was queued, false if queue was full (event dropped)
    bool set(uint16_t id, uint8_t value) {
        IndicatorEvent event{id, value, std::chrono::steady_clock::now()};
        return event_queue_.try_enqueue(event);
    }

    /// Update an indicator by name (convenience, does string lookup).
    /// @param name Indicator name
    /// @param value New value
    /// @return true if event was queued, false if name unknown or queue full
    bool set(const std::string& name, uint8_t value) {
        std::shared_lock lock(registry_mutex_);
        auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            return false;
        }
        return set(it->second, value);
    }

    /// Batch update multiple indicators.
    /// @param updates List of (id, value) pairs
    /// @return true if all events were queued
    bool set(std::initializer_list<std::pair<uint16_t, uint8_t>> updates) {
        bool all_ok = true;
        for (const auto& [id, value] : updates) {
            if (!set(id, value)) {
                all_ok = false;
            }
        }
        return all_ok;
    }

    /// Get the published value of an indicator by name.
    /// Lock-free atomic read.
    uint8_t get(const std::string& name) const {
        std::shared_lock lock(registry_mutex_);
        auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            return 0;
        }
        return indicators_[it->second].published_value.load(std::memory_order_relaxed);
    }

    /// Get all published indicator values.
    std::unordered_map<std::string, uint8_t> values() const {
        std::unordered_map<std::string, uint8_t> result;
        std::shared_lock lock(registry_mutex_);
        for (const auto& state : indicators_) {
            result[state.name] = state.published_value.load(std::memory_order_relaxed);
        }
        return result;
    }

    /// Get metadata for an indicator.
    /// @param name Indicator name
    /// @return Metadata map, or empty map if not found
    std::unordered_map<std::string, std::string> metadata(const std::string& name) const {
        std::shared_lock lock(registry_mutex_);
        auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            return {};
        }
        return indicators_[it->second].metadata;
    }

    /// Get all registered indicator names.
    std::vector<std::string> names() const {
        std::vector<std::string> result;
        std::shared_lock lock(registry_mutex_);
        result.reserve(indicators_.size());
        for (const auto& state : indicators_) {
            result.push_back(state.name);
        }
        return result;
    }

    /// Get the change sequence number.
    /// Incremented each time any published value changes.
    uint64_t sequence() const {
        return sequence_.load(std::memory_order_acquire);
    }

    /// Start the consumer thread.
    /// Call after all indicators are registered.
    void start() {
        if (running_.exchange(true)) {
            return;  // Already running
        }
        consumer_thread_ = std::thread(&Indicators::consumer_loop, this);
    }

    /// Stop the consumer thread and wait for it to finish.
    void stop() {
        if (!running_.exchange(false)) {
            return;  // Not running
        }
        if (consumer_thread_.joinable()) {
            consumer_thread_.join();
        }
    }

    /// Check if the consumer thread is running.
    bool is_running() const {
        return running_.load(std::memory_order_relaxed);
    }

private:
    struct IndicatorState {
        std::string name;
        std::unique_ptr<IndicatorFilter> filter;
        std::unordered_map<std::string, std::string> metadata;
        std::atomic<uint8_t> published_value{0};

        // IndicatorState needs to be movable for vector storage
        IndicatorState() = default;
        IndicatorState(IndicatorState&& other) noexcept
            : name(std::move(other.name))
            , filter(std::move(other.filter))
            , metadata(std::move(other.metadata))
            , published_value(other.published_value.load()) {}
    };

    void consumer_loop() {
        while (running_.load(std::memory_order_relaxed)) {
            auto now = std::chrono::steady_clock::now();

            // Drain the event queue, dispatch to filters
            IndicatorEvent event;
            while (event_queue_.try_dequeue(event)) {
                std::shared_lock lock(registry_mutex_);
                if (event.id < indicators_.size()) {
                    indicators_[event.id].filter->update(event.value, event.timestamp);
                }
            }

            // Sample all filters, update published values
            bool any_changed = false;
            {
                std::shared_lock lock(registry_mutex_);
                for (auto& state : indicators_) {
                    uint8_t old_val = state.published_value.load(std::memory_order_relaxed);
                    uint8_t new_val = state.filter->sample(now);
                    if (old_val != new_val) {
                        state.published_value.store(new_val, std::memory_order_relaxed);
                        any_changed = true;
                    }
                }
            }

            if (any_changed) {
                sequence_.fetch_add(1, std::memory_order_release);
            }

            // Sleep for ~20ms (50Hz update rate)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // Indicator registry (read-heavy after initialization)
    std::vector<IndicatorState> indicators_;
    std::unordered_map<std::string, uint16_t> name_to_id_;
    mutable std::shared_mutex registry_mutex_;

    // Lock-free event queue (ring buffer, drops old events when full)
    moodycamel::ReaderWriterQueue<IndicatorEvent> event_queue_{4096};

    // Consumer thread
    std::thread consumer_thread_;
    std::atomic<bool> running_{false};

    // Change tracking
    std::atomic<uint64_t> sequence_{0};
};

} // namespace beebium

#endif // BEEBIUM_INDICATORS_INDICATORS_HPP
