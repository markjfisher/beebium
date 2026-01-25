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

#include "beebium/service/ShutdownCoordinator.hpp"
#include "beebium/service/ShutdownCondition.hpp"

#include <algorithm>
#include <thread>

namespace beebium::service {

void ShutdownCoordinator::register_condition(ShutdownCondition* condition) {
    std::lock_guard lock(mutex_);
    // Avoid duplicates
    if (std::find(conditions_.begin(), conditions_.end(), condition) == conditions_.end()) {
        conditions_.push_back(condition);
    }
}

void ShutdownCoordinator::unregister_condition(ShutdownCondition* condition) {
    std::lock_guard lock(mutex_);
    conditions_.erase(
        std::remove(conditions_.begin(), conditions_.end(), condition),
        conditions_.end());
}

std::vector<std::string> ShutdownCoordinator::coordinate_shutdown(
    std::function<void(const std::vector<ConditionStatus>&)> progress_callback) {

    std::vector<std::string> timed_out;

    // Phase 1: Notify all subsystems to prepare
    {
        std::lock_guard lock(mutex_);
        shutdown_start_ = std::chrono::steady_clock::now();
        shutdown_in_progress_ = true;

        for (auto* cond : conditions_) {
            cond->prepare_for_shutdown();
        }
    }

    // Phase 2: Wait for each condition (with individual timeouts)
    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - shutdown_start_);

        // Hard cap on total shutdown time
        if (total_elapsed >= DEFAULT_MAX_TOTAL_TIME) {
            std::lock_guard lock(mutex_);
            for (auto* cond : conditions_) {
                if (!cond->ready_for_shutdown()) {
                    timed_out.push_back(cond->name());
                }
            }
            break;
        }

        bool all_ready = true;
        std::vector<ConditionStatus> status;

        {
            std::lock_guard lock(mutex_);
            for (auto* cond : conditions_) {
                bool ready = cond->ready_for_shutdown();
                auto timeout = cond->timeout();

                status.push_back({
                    cond->name(),
                    ready,
                    total_elapsed,
                    timeout
                });

                if (!ready) {
                    if (total_elapsed < timeout) {
                        all_ready = false;
                    } else {
                        // This condition has timed out
                        if (std::find(timed_out.begin(), timed_out.end(), cond->name()) == timed_out.end()) {
                            timed_out.push_back(cond->name());
                        }
                    }
                }
            }
        }

        // Report progress
        if (progress_callback) {
            progress_callback(status);
        }

        if (all_ready) {
            break;
        }

        std::this_thread::sleep_for(DEFAULT_POLL_INTERVAL);
    }

    {
        std::lock_guard lock(mutex_);
        shutdown_in_progress_ = false;
    }

    return timed_out;
}

std::vector<ConditionStatus> ShutdownCoordinator::get_condition_status() const {
    std::lock_guard lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = shutdown_in_progress_
        ? std::chrono::duration_cast<std::chrono::milliseconds>(now - shutdown_start_)
        : std::chrono::milliseconds(0);

    std::vector<ConditionStatus> status;
    for (auto* cond : conditions_) {
        status.push_back({
            cond->name(),
            cond->ready_for_shutdown(),
            elapsed,
            cond->timeout()
        });
    }

    return status;
}

bool ShutdownCoordinator::has_conditions() const {
    std::lock_guard lock(mutex_);
    return !conditions_.empty();
}

} // namespace beebium::service
