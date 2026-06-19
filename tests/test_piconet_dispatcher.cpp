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

// Drives the piconet PiconetDispatcher directly (no gRPC), exactly as the
// core's ExtensionRpc service feeds it serialized bytes at runtime. With no
// backend brought up (no real USB device), GetStatus reports the
// configured-but-unavailable state.

#include "PiconetDispatcher.hpp"
#include "PiconetEconetTransportExtension.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "piconet_service.pb.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

using namespace beebium;

namespace {
class TestRpcContext final : public RpcContext {
public:
    bool is_cancelled() const override { return false; }
    const std::map<std::string, std::string>& metadata() const override {
        return metadata_;
    }
private:
    std::map<std::string, std::string> metadata_;
};
}  // namespace

TEST_CASE("PiconetEconetTransportExtension exposes a PiconetService dispatcher",
          "[piconet][extension-rpc]") {
    PiconetEconetTransportExtension ext;
    auto dispatchers = ext.rpc_dispatchers();
    REQUIRE(dispatchers.size() == 1);
    CHECK(dispatchers[0]->service_name() == "PiconetService");
}

TEST_CASE("Piconet dispatcher GetStatus reports closed without a backend",
          "[piconet][extension-rpc]") {
    PiconetEconetTransportExtension ext;
    auto* dispatcher = ext.rpc_dispatchers()[0];
    TestRpcContext ctx;

    PiconetGetStatusRequest req;
    std::string out;
    RpcStatus status = dispatcher->invoke("GetStatus", req.SerializeAsString(), out, ctx);
    REQUIRE(status.is_ok());

    PiconetGetStatusResponse resp;
    REQUIRE(resp.ParseFromString(out));
    CHECK(resp.device_path().empty());
    CHECK_FALSE(resp.serial_open());
}

TEST_CASE("Piconet dispatcher rejects an unknown method",
          "[piconet][extension-rpc]") {
    PiconetEconetTransportExtension ext;
    auto* dispatcher = ext.rpc_dispatchers()[0];
    TestRpcContext ctx;

    std::string out;
    RpcStatus status = dispatcher->invoke("Nope", "", out, ctx);
    CHECK_FALSE(status.is_ok());
    CHECK(status.code == kRpcUnimplemented);
}

TEST_CASE("PiconetEconetTransportExtension requires real-time pacing",
          "[piconet][transport]") {
    // Piconet bridges to a real Econet line, so it only works at 1x and the
    // server gates it off at any other speed.
    PiconetEconetTransportExtension ext;
    REQUIRE(ext.requires_real_time_pacing());
}
