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

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/EconetSocket.hpp"
#include "beebium/econet/TestBackend.hpp"

#include <memory>
#include <vector>

using namespace beebium;

namespace {

// Counting double: records every on_station_id_changed call.
class CountingBackend : public TestBackend {
public:
    void on_station_id_changed(uint8_t new_id) override {
        station_change_log_.push_back(new_id);
    }

    const std::vector<uint8_t>& station_change_log() const {
        return station_change_log_;
    }

private:
    std::vector<uint8_t> station_change_log_;
};

}  // namespace

TEST_CASE("NetworkBackend default on_station_id_changed is a no-op",
          "[econet][backend][station]") {
    // The base class provides a default no-op implementation. Subclasses that
    // do not care about station changes must compile and run without overriding.
    TestBackend backend;
    backend.on_station_id_changed(42);  // Must not crash, must not throw.
    SUCCEED("default on_station_id_changed is callable as a no-op");
}

TEST_CASE("EconetSocket::set_station_id propagates to backend via on_station_id_changed",
          "[econet][socket][station]") {
    EconetSocket socket;
    auto backend_owner = std::make_unique<CountingBackend>();
    auto* backend = backend_owner.get();

    socket.enable(0x10, std::move(backend_owner), true);

    // Baseline: enable() must not have called the hook (initial value is
    // delivered via the backend constructor, not via this hook).
    REQUIRE(backend->station_change_log().empty());

    socket.set_station_id(0x20);
    socket.set_station_id(0x21);
    socket.set_station_id(0x42);

    const auto& log = backend->station_change_log();
    REQUIRE(log.size() == 3);
    CHECK(log[0] == 0x20);
    CHECK(log[1] == 0x21);
    CHECK(log[2] == 0x42);
}

TEST_CASE("EconetSocket::enable does NOT call on_station_id_changed",
          "[econet][socket][station]") {
    // The initial station ID is communicated via the backend's constructor or
    // initial configuration, not via the on_station_id_changed hook. The hook
    // signals a *change* from a previously-known value.
    EconetSocket socket;
    auto backend_owner = std::make_unique<CountingBackend>();
    auto* backend = backend_owner.get();

    socket.enable(0xFE, std::move(backend_owner), true);

    CHECK(backend->station_change_log().empty());
}
