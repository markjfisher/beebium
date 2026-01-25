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

#ifndef BEEBIUM_SERVICE_SHUTDOWN_CONDITION_HPP
#define BEEBIUM_SERVICE_SHUTDOWN_CONDITION_HPP

#include <chrono>
#include <string>

namespace beebium::service {

/// A condition that should be satisfied before shutdown proceeds.
///
/// Subsystems (disc controller, audio, etc.) implement this interface
/// and register with ShutdownCoordinator. When shutdown is initiated,
/// the coordinator notifies all conditions to prepare, then waits for
/// them to become ready (with individual timeouts).
///
/// This allows in-flight operations to complete cleanly - for example,
/// the disc controller can finish a write operation before shutdown.
class ShutdownCondition {
public:
    virtual ~ShutdownCondition() = default;

    /// Human-readable name for logging/debugging (e.g., "Disc controller").
    [[nodiscard]] virtual std::string name() const = 0;

    /// Called when shutdown is initiated. Subsystem should begin winding down.
    ///
    /// For example, disc controller stops accepting new commands but lets
    /// the current operation finish. Audio service stops accepting new
    /// samples and begins draining its buffer.
    virtual void prepare_for_shutdown() = 0;

    /// Returns true when this subsystem is ready for shutdown.
    ///
    /// For example, disc controller returns true when no I/O is in progress.
    /// Audio service returns true when buffer is empty or nearly empty.
    [[nodiscard]] virtual bool ready_for_shutdown() const = 0;

    /// Maximum time to wait for this condition (milliseconds).
    ///
    /// Coordinator will proceed after this timeout even if not ready.
    /// Default is 2 seconds.
    [[nodiscard]] virtual std::chrono::milliseconds timeout() const {
        return std::chrono::milliseconds(2000);
    }
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SHUTDOWN_CONDITION_HPP
