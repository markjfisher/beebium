// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#ifndef BEEBIUM_SERVICE_SERIAL_SERVICE_HPP
#define BEEBIUM_SERVICE_SERIAL_SERVICE_HPP

#include "serial.grpc.pb.h"
#include "beebium/serial/SerialConcepts.hpp"
#include "beebium/serial/SerialSocket.hpp"

#include <grpcpp/grpcpp.h>

namespace beebium::service {

using beebium::HasSerialSocket;

// gRPC service reporting the BBC's on-board serial hardware status (MC6850 ACIA
// + Serial ULA). What is attached to the far end of the serial port is owned by
// a serial PeripheralExtension (host-serial, rpc-serial, loopback-serial); this
// service only observes the chip registers and never attaches a device.
//
// GetSerialStatus reads the ACIA/ULA registers without pausing the emulation
// thread: it is a benign racy snapshot of a few scalar fields, sufficient for a
// status display.
template<typename MachineType>
class SerialServiceImpl final : public SerialService::Service {
public:
    explicit SerialServiceImpl(MachineType& machine) : machine_(machine) {}

    SerialServiceImpl(const SerialServiceImpl&) = delete;
    SerialServiceImpl& operator=(const SerialServiceImpl&) = delete;

    grpc::Status GetSerialStatus(
        grpc::ServerContext* /*context*/,
        const GetSerialStatusRequest* /*request*/,
        SerialStatus* response) override
    {
        using Memory = typename MachineType::Memory;
        if constexpr (!HasSerialSocket<Memory>) {
            response->set_has_serial_socket(false);
            return grpc::Status::OK;
        } else {
            response->set_has_serial_socket(true);
            auto& serial = machine_.state().memory.serial_socket;
            const auto& acia = serial.acia();
            const auto& ula = serial.ula();

            response->set_acia_control(acia.control());
            response->set_acia_status(acia.status());
            response->set_tdre(acia.tdre());
            response->set_rdrf(acia.rdrf());
            response->set_not_dcd(acia.not_dcd());
            response->set_not_cts(acia.not_cts());
            response->set_irq_pending(acia.irq_pending());

            response->set_ula_control(ula.control());
            response->set_tx_baud(ula.tx_baud());
            response->set_rx_baud(ula.rx_baud());
            response->set_rs423_selected(ula.rs423_selected());
            response->set_motor_on(ula.motor_on());
            response->set_tx_bit_period(ula.tx_bit_period());
            response->set_rx_bit_period(ula.rx_bit_period());
            return grpc::Status::OK;
        }
    }

private:
    MachineType& machine_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_SERIAL_SERVICE_HPP
