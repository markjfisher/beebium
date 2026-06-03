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

#include "RpcSerialEndpoint.hpp"
#include "RpcSerialExtension.hpp"
#include "RpcSerialService.hpp"

#include <beebium/devices/Mc6850.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

using namespace beebium;

namespace {
constexpr uint8_t CONTROL_8N1 =
    Mc6850::COUNTER_DIVIDE_16 | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);
}  // namespace

TEST_CASE("RpcSerialExtension attaches and exposes a service",
          "[serial][rpc-serial]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    RpcSerialExtension ext;
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");

    ext.init(ctx);
    CHECK(port.is_occupied());
    CHECK(ext.grpc_services().size() == 1);

    ext.shutdown();
}

TEST_CASE("RpcSerial service round-trips bytes through the BBC",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    SerialSocket socket;
    SerialPort port(socket);
    port.attach(endpoint);

    RpcSerialServiceImpl service(endpoint);

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);

    // device -> BBC: Send injects a byte the BBC then receives.
    RpcSerialSendRequest send_req;
    send_req.set_data("Z");
    RpcSerialSendResponse send_resp;
    service.Send(nullptr, &send_req, &send_resp);
    CHECK(send_resp.accepted() == 1);

    for (int i = 0; i < 6000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == static_cast<uint8_t>('Z'));

    // BBC -> device: transmit a byte, then Receive collects it.
    socket.write_acia(1, 0x5A);
    for (int i = 0; i < 6000; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    RpcSerialReceiveRequest recv_req;
    recv_req.set_max_bytes(0);
    RpcSerialReceiveResponse recv_resp;
    service.Receive(nullptr, &recv_req, &recv_resp);
    REQUIRE(recv_resp.data().size() == 1);
    CHECK(static_cast<uint8_t>(recv_resp.data()[0]) == 0x5A);
}

TEST_CASE("RpcSerial Send caps the receive queue and reports accepted",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    RpcSerialServiceImpl service(endpoint);

    // Flood beyond the receive capacity with nothing draining it: Send accepts
    // only what fits and reports it -- it never blocks or grows unbounded.
    const std::size_t flood = RpcSerialEndpoint::kRxCapacity + 1000;
    RpcSerialSendRequest req;
    req.set_data(std::string(flood, 'A'));
    RpcSerialSendResponse resp;
    service.Send(nullptr, &req, &resp);
    CHECK(resp.accepted() == RpcSerialEndpoint::kRxCapacity);

    // The queue is now full; a further send accepts nothing.
    RpcSerialSendResponse resp2;
    service.Send(nullptr, &req, &resp2);
    CHECK(resp2.accepted() == 0);
}

TEST_CASE("RpcSerialEndpoint bounds tx and signals back-pressure",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    CHECK(endpoint.accepts_more());

    for (std::size_t i = 0; i < RpcSerialEndpoint::kTxBackPressure; ++i) {
        endpoint.add_byte(0x41);
    }
    CHECK_FALSE(endpoint.accepts_more());  // at the back-pressure mark -> /CTS

    // Beyond the hard cap, bytes are dropped (a real ACIA loses data too) -- the
    // queue never grows without bound.
    for (std::size_t i = 0; i < 1000; ++i) endpoint.add_byte(0x42);
    CHECK(endpoint.tx_pending() <= RpcSerialEndpoint::kTxHardCap);
    CHECK(endpoint.tx_dropped() > 0);

    endpoint.drain();  // client collects everything
    CHECK(endpoint.accepts_more());  // back-pressure released
}
