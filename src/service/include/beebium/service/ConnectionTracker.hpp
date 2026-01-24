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

#ifndef BEEBIUM_SERVICE_CONNECTION_TRACKER_HPP
#define BEEBIUM_SERVICE_CONNECTION_TRACKER_HPP

#include <atomic>

namespace beebium::service {

/// Tracks the number of active client connections (WatchServerStatus streams).
/// Thread-safe via std::atomic operations.
class ConnectionTracker {
public:
    ConnectionTracker() = default;
    ~ConnectionTracker() = default;

    // Non-copyable
    ConnectionTracker(const ConnectionTracker&) = delete;
    ConnectionTracker& operator=(const ConnectionTracker&) = delete;

    /// Called when a client starts a WatchServerStatus stream.
    void on_client_connected();

    /// Called when a client's WatchServerStatus stream ends.
    /// Safe to call even if count is already 0.
    void on_client_disconnected();

    /// Get the current number of active clients.
    [[nodiscard]] int client_count() const noexcept;

private:
    std::atomic<int> client_count_{0};
};

/// RAII guard for automatic connection tracking in streaming RPCs.
/// Increments count on construction, decrements on destruction.
class ConnectionGuard {
public:
    /// Create a guard that tracks a connection.
    explicit ConnectionGuard(ConnectionTracker& tracker);

    /// Decrement the connection count.
    ~ConnectionGuard();

    // Non-copyable, non-movable
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
    ConnectionGuard(ConnectionGuard&&) = delete;
    ConnectionGuard& operator=(ConnectionGuard&&) = delete;

private:
    ConnectionTracker& tracker_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_CONNECTION_TRACKER_HPP
