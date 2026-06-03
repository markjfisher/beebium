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

#include "RpcSerialExtension.hpp"
#include "RpcSerialService.hpp"

#include <beebium/devices/Mc6850.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialDevice.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

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
    ScriptableSerialEndpoint endpoint;
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
    CHECK(send_resp.queued() == 1);

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
