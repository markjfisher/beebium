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

#ifndef BEEBIUM_SERVICE_SHUTDOWN_COORDINATOR_HPP
#define BEEBIUM_SERVICE_SHUTDOWN_COORDINATOR_HPP

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace beebium::service {

class ShutdownCondition;

/// Status of a single shutdown condition for reporting to clients.
struct ConditionStatus {
    std::string name;
    bool ready;
    std::chrono::milliseconds elapsed;
    std::chrono::milliseconds timeout;
};

/// Coordinates graceful shutdown across multiple subsystems.
///
/// Subsystems register ShutdownCondition objects with the coordinator.
/// When shutdown is initiated, the coordinator:
/// 1. Notifies all conditions to prepare for shutdown
/// 2. Waits for all conditions to become ready (with individual timeouts)
/// 3. Returns the list of conditions that timed out (if any)
///
/// The coordinator has a hard cap on total shutdown time (10 seconds by default)
/// to prevent indefinite hangs.
class ShutdownCoordinator {
public:
    /// Default maximum total shutdown time.
    static constexpr auto DEFAULT_MAX_TOTAL_TIME = std::chrono::milliseconds(10000);

    /// Default polling interval when waiting for conditions.
    static constexpr auto DEFAULT_POLL_INTERVAL = std::chrono::milliseconds(50);

    ShutdownCoordinator() = default;
    ~ShutdownCoordinator() = default;

    // Non-copyable
    ShutdownCoordinator(const ShutdownCoordinator&) = delete;
    ShutdownCoordinator& operator=(const ShutdownCoordinator&) = delete;

    /// Register a shutdown condition.
    ///
    /// The coordinator does not take ownership of the condition.
    /// The condition must remain valid until unregistered or shutdown completes.
    void register_condition(ShutdownCondition* condition);

    /// Unregister a shutdown condition.
    ///
    /// Safe to call if condition is not registered.
    void unregister_condition(ShutdownCondition* condition);

    /// Initiate graceful shutdown and wait for all conditions.
    ///
    /// This function blocks until all conditions are satisfied or timed out.
    /// Call progress_callback periodically during waiting to report status.
    ///
    /// @param progress_callback Optional callback invoked periodically with current status.
    /// @return List of condition names that timed out (empty = fully graceful).
    std::vector<std::string> coordinate_shutdown(
        std::function<void(const std::vector<ConditionStatus>&)> progress_callback = nullptr);

    /// Get current status of all conditions.
    ///
    /// Can be called at any time for UI/debugging.
    /// If shutdown is not in progress, elapsed times will be 0.
    [[nodiscard]] std::vector<ConditionStatus> get_condition_status() const;

    /// Check if any conditions are registered.
    [[nodiscard]] bool has_conditions() const;

private:
    mutable std::mutex mutex_;
    std::vector<ShutdownCondition*> conditions_;
    std::chrono::steady_clock::time_point shutdown_start_;
    bool shutdown_in_progress_ = false;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SHUTDOWN_COORDINATOR_HPP
