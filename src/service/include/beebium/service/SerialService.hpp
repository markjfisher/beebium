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
#include "beebium/serial/SerialDevice.hpp"
#include "beebium/serial/SerialSocket.hpp"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <string>

namespace beebium::service {

using beebium::HasSerialSocket;

// gRPC service for the BBC's on-board serial port (MC6850 ACIA + Serial ULA).
// It serves status snapshots and the in-process test endpoints: loopback, and a
// scriptable endpoint (clients inject bytes for the Beeb via SendToDevice and
// collect transmitted bytes via ReceiveFromDevice).
//
// Real host transports (pty / serial device) are NOT here: they are provided by
// the host-serial PeripheralExtension, which attaches via the SerialPort handle.
// When such an extension owns the port, this service only reports status.
//
// Thread-safety: SendToDevice/ReceiveFromDevice touch the scriptable endpoint's
// own mutex-protected queues; SetEndpointMode swaps the attached device with the
// emulation loop paused.
template<typename MachineType>
class SerialServiceImpl final : public SerialService::Service {
public:
    explicit SerialServiceImpl(MachineType& machine)
        : machine_(machine)
    {
        using Memory = typename MachineType::Memory;
        if constexpr (HasSerialSocket<Memory>) {
            auto& port = machine_.state().memory.serial_port();
            if (!port.is_occupied()) {
                // No transport/device extension claimed the serial port, so
                // attach a scriptable endpoint by default: SendToDevice/
                // ReceiveFromDevice work out of the box. Extensions init before
                // the server constructs its services, so is_occupied() already
                // reflects any extension that took the port -- if one did, we
                // leave it alone and only report status (mode NONE).
                scriptable_ = std::make_shared<ScriptableSerialEndpoint>();
                port.attach(*scriptable_);
                service_owns_port_ = true;
                mode_ = SERIAL_ENDPOINT_SCRIPTABLE;
            }
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
            // endpoint_path/endpoint_open describe a host transport, which now
            // lives in the host-serial extension, not this service.
            return grpc::Status::OK;
        }
    }

    grpc::Status SetEndpointMode(
        grpc::ServerContext* /*context*/,
        const SetEndpointModeRequest* request,
        SetEndpointModeResponse* response) override
    {
        std::string error;
        std::string advertised;
        bool ok = apply_endpoint_mode(request->mode(), request->path(),
                                      static_cast<int>(request->baud()),
                                      error, advertised);
        response->set_success(ok);
        response->set_error(error);
        response->set_advertised_path(advertised);
        return grpc::Status::OK;
    }

    // Apply an endpoint mode programmatically (shared by the RPC above and by
    // the server's --serial CLI wiring). Returns false and fills `error` on
    // failure; on success `advertised` carries the PTY/DEVICE path (if any).
    bool apply_endpoint_mode(SerialEndpointMode mode, const std::string& path,
                             int baud, std::string& error, std::string& advertised)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        using Memory = typename MachineType::Memory;

        if constexpr (!HasSerialSocket<Memory>) {
            error = "Machine has no serial socket";
            return false;
        } else {
            auto& port_handle = machine_.state().memory.serial_port();
            // If a transport/device extension owns the serial port, the service
            // must not yank it out from under the extension.
            if (port_handle.is_occupied() && !service_owns_port_) {
                error = "serial port is owned by an extension; "
                        "endpoint mode cannot be changed";
                return false;
            }
            // pty/device transports are provided by the host-serial extension,
            // not this service.
            if (mode == SERIAL_ENDPOINT_PTY || mode == SERIAL_ENDPOINT_DEVICE) {
                error = "pty/device serial transports are provided by the "
                        "host-serial extension; use --host-serial mode=pty|device";
                return false;
            }
            (void)path;
            (void)baud;
            (void)advertised;  // only pty/device had an advertised path

            // Swap the attached device with the emulation loop paused: the ULA
            // reads the device pointer on every tick. Detach first so the port
            // is free (attach() throws if occupied) and is_occupied() stays
            // accurate; the service owns the endpoint objects it attaches.
            machine_.with_emulation_paused([&] {
                port_handle.detach();
                switch (mode) {
                    case SERIAL_ENDPOINT_NONE:
                        break;  // leave the port detached
                    case SERIAL_ENDPOINT_LOOPBACK:
                        loopback_ = std::make_shared<LoopbackSerialEndpoint>();
                        port_handle.attach(*loopback_);
                        break;
                    case SERIAL_ENDPOINT_SCRIPTABLE:
                    default:
                        if (!scriptable_) {
                            scriptable_ = std::make_shared<ScriptableSerialEndpoint>();
                        }
                        port_handle.attach(*scriptable_);
                        mode = SERIAL_ENDPOINT_SCRIPTABLE;
                        break;
                }
            });
            // NONE leaves the port detached (the service owns nothing); every
            // other mode attaches a service-owned endpoint.
            service_owns_port_ = (mode != SERIAL_ENDPOINT_NONE);
            mode_ = mode;
            return true;
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
    // True when the device currently attached to the serial port is one the
    // service created (scriptable/loopback/host). False when the port is free
    // or owned by a transport/device extension -- in which case the service
    // only reports status and refuses to change the endpoint mode.
    bool service_owns_port_ = false;
    std::shared_ptr<ScriptableSerialEndpoint> scriptable_;
    std::shared_ptr<LoopbackSerialEndpoint> loopback_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_SERIAL_SERVICE_HPP
