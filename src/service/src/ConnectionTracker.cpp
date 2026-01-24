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

#include "beebium/service/ConnectionTracker.hpp"

namespace beebium::service {

void ConnectionTracker::on_client_connected() {
    ++client_count_;
}

void ConnectionTracker::on_client_disconnected() {
    // Decrement only if positive to prevent underflow
    int current = client_count_.load();
    while (current > 0) {
        if (client_count_.compare_exchange_weak(current, current - 1)) {
            break;
        }
        // current is updated by compare_exchange_weak if it failed
    }
}

int ConnectionTracker::client_count() const noexcept {
    return client_count_.load();
}

// ConnectionGuard implementation

ConnectionGuard::ConnectionGuard(ConnectionTracker& tracker)
    : tracker_(tracker) {
    tracker_.on_client_connected();
}

ConnectionGuard::~ConnectionGuard() {
    tracker_.on_client_disconnected();
}

} // namespace beebium::service
