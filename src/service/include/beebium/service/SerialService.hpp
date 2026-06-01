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

#ifndef BEEBIUM_SERVICE_SERIAL_SERVICE_HPP
#define BEEBIUM_SERVICE_SERIAL_SERVICE_HPP

#include "serial.grpc.pb.h"
#include "beebium/serial/SerialConcepts.hpp"
#include "beebium/serial/SerialDevice.hpp"
#include "beebium/serial/SerialSocket.hpp"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>

namespace beebium::service {

using beebium::HasSerialSocket;

// gRPC service exposing the BBC's on-board serial port (MC6850 ACIA + Serial
// ULA). It serves status snapshots and provides a scriptable in-process
// transport: clients inject bytes for the Beeb to receive (SendToDevice) and
// collect bytes the Beeb has transmitted (ReceiveFromDevice).
//
// Thread-safety: SendToDevice/ReceiveFromDevice only touch the scriptable
// endpoint's own mutex-protected queues, which the emulation thread accesses
// through the same locks, so they need no machine pause. SetEndpointMode swaps
// the source/sink pointers inside the Serial ULA, which the emulation thread
// reads every tick, so it is performed with the emulation loop paused.
template<typename MachineType>
class SerialServiceImpl final : public SerialService::Service {
public:
    explicit SerialServiceImpl(MachineType& machine)
        : machine_(machine)
    {
        using Memory = typename MachineType::Memory;
        if constexpr (HasSerialSocket<Memory>) {
            // Attach a scriptable endpoint by default so SendToDevice/
            // ReceiveFromDevice work out of the box (mirrors the demo flow).
            scriptable_ = std::make_shared<ScriptableSerialEndpoint>();
            auto& serial = machine_.state().memory.serial_socket;
            serial.set_source(scriptable_);
            serial.set_sink(scriptable_);
            mode_ = SERIAL_ENDPOINT_SCRIPTABLE;
        }
    }

    ~SerialServiceImpl() override = default;

    SerialServiceImpl(const SerialServiceImpl&) = delete;
    SerialServiceImpl& operator=(const SerialServiceImpl&) = delete;

    grpc::Status GetSerialStatus(
        grpc::ServerContext* /*context*/,
        const GetSerialStatusRequest* /*request*/,
        SerialStatus* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
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

            response->set_endpoint_mode(mode_);
            if (scriptable_) {
                response->set_tx_pending(static_cast<uint32_t>(scriptable_->tx_pending()));
                response->set_rx_pending(static_cast<uint32_t>(scriptable_->rx_pending()));
            }
            return grpc::Status::OK;
        }
    }

    grpc::Status SetEndpointMode(
        grpc::ServerContext* /*context*/,
        const SetEndpointModeRequest* request,
        SetEndpointModeResponse* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        using Memory = typename MachineType::Memory;

        if constexpr (!HasSerialSocket<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no serial socket");
            return grpc::Status::OK;
        } else {
            SerialEndpointMode mode = request->mode();
            auto& serial = machine_.state().memory.serial_socket;

            // Swap source/sink with the emulation loop paused: the ULA reads
            // these pointers on every tick.
            machine_.with_emulation_paused([&] {
                switch (mode) {
                    case SERIAL_ENDPOINT_NONE:
                        serial.set_source(nullptr);
                        serial.set_sink(nullptr);
                        break;
                    case SERIAL_ENDPOINT_LOOPBACK: {
                        loopback_ = std::make_shared<LoopbackSerialEndpoint>();
                        serial.set_source(loopback_);
                        serial.set_sink(loopback_);
                        break;
                    }
                    case SERIAL_ENDPOINT_SCRIPTABLE:
                    default:
                        if (!scriptable_) {
                            scriptable_ = std::make_shared<ScriptableSerialEndpoint>();
                        }
                        serial.set_source(scriptable_);
                        serial.set_sink(scriptable_);
                        mode = SERIAL_ENDPOINT_SCRIPTABLE;
                        break;
                }
            });
            mode_ = mode;
            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status SendToDevice(
        grpc::ServerContext* /*context*/,
        const SendToDeviceRequest* request,
        SendToDeviceResponse* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mode_ != SERIAL_ENDPOINT_SCRIPTABLE || !scriptable_) {
            response->set_success(false);
            response->set_error("Serial endpoint is not in scriptable mode");
            return grpc::Status::OK;
        }
        const std::string& data = request->data();
        scriptable_->inject(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        response->set_success(true);
        response->set_queued(static_cast<uint32_t>(data.size()));
        return grpc::Status::OK;
    }

    grpc::Status ReceiveFromDevice(
        grpc::ServerContext* /*context*/,
        const ReceiveFromDeviceRequest* request,
        ReceiveFromDeviceResponse* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mode_ != SERIAL_ENDPOINT_SCRIPTABLE || !scriptable_) {
            response->set_success(false);
            response->set_error("Serial endpoint is not in scriptable mode");
            return grpc::Status::OK;
        }
        std::vector<uint8_t> bytes = scriptable_->drain(request->max_bytes());
        response->set_success(true);
        response->set_data(std::string(bytes.begin(), bytes.end()));
        return grpc::Status::OK;
    }

private:
    MachineType& machine_;
    std::mutex mutex_;

    SerialEndpointMode mode_ = SERIAL_ENDPOINT_NONE;
    std::shared_ptr<ScriptableSerialEndpoint> scriptable_;
    std::shared_ptr<LoopbackSerialEndpoint> loopback_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_SERIAL_SERVICE_HPP
