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

#ifndef BEEBIUM_SERVICE_PARASITE_SERVER_HPP
#define BEEBIUM_SERVICE_PARASITE_SERVER_HPP

#include "beebium/service/DebuggerService.hpp"
#include "beebium/service/SystemService.hpp"
#include "beebium/service/ConnectionTracker.hpp"
#include <beebium/discovery/Advertiser.hpp>
#include <beebium/tube/ParasiteRunner.hpp>

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <memory>
#include <sstream>
#include <string>

namespace beebium::service {

// Lightweight gRPC server for the Tube parasite process.
//
// Hosts only DebuggerControl and SystemService -- the parasite has no
// video, audio, keyboard, disc, indicator, sideways, econet, or tube
// services. DeviceInspection is not registered since the parasite has
// no BBC Micro devices (VIAs, CRTC, etc.).
//
// This is the parasite's analogue of Server<MachineType> on the host.

class ParasiteServer {
public:
    ParasiteServer(ParasiteRunner& runner, const std::string& address = "127.0.0.1",
                   uint16_t port = 0)
        : runner_(runner), address_(address), port_(port) {}

    ~ParasiteServer() { stop(); }

    ParasiteServer(const ParasiteServer&) = delete;
    ParasiteServer& operator=(const ParasiteServer&) = delete;

    using ShutdownCallback = std::function<void()>;

    void start(Provenance provenance, MachineIdentity identity,
               bool enable_advertisement = false,
               ShutdownCallback shutdown_callback = nullptr) {
        if (running_) return;

        // Save identity info for mDNS
        std::string identity_name = identity.name;
        std::string identity_uuid = identity.uuid;
        std::string provenance_type = provenance.type;

        advertiser_ = discovery::create_advertiser();

        debugger_service_ = std::make_unique<DebuggerControlServiceImpl<ParasiteRunner>>(runner_);

        system_service_ = std::make_unique<SystemServiceImpl<ParasiteRunner>>(
            runner_, std::move(provenance), std::move(identity),
            &connection_tracker_, advertiser_.get(), 0,
            ShutdownPolicyConfig{}, nullptr, std::move(shutdown_callback));

        // Build server
        std::ostringstream addr_stream;
        addr_stream << address_ << ":" << port_;

        grpc::ServerBuilder builder;
        builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

        int selected_port = 0;
        builder.AddListeningPort(addr_stream.str(),
                                 grpc::InsecureServerCredentials(), &selected_port);
        builder.RegisterService(debugger_service_.get());
        builder.RegisterService(system_service_.get());

        grpc_server_ = builder.BuildAndStart();

        if (!grpc_server_ || selected_port <= 0) {
            if (grpc_server_) {
                grpc_server_->Shutdown();
                grpc_server_.reset();
            }
            throw std::runtime_error("Failed to start parasite gRPC server");
        }

        port_ = static_cast<uint16_t>(selected_port);
        system_service_->set_server_port(port_);

        if (enable_advertisement && advertiser_) {
            discovery::ServiceInfo info;
            info.instance_name = identity_name;
            info.port = port_;
            info.txt_records["uuid"] = identity_uuid;
            info.txt_records["role"] = "parasite";
            info.txt_records["processor"] = "65C02";
            info.txt_records["clock_mhz"] = "3";
            info.txt_records["provenance"] = provenance_type;
            advertiser_->start(info);
        }

        running_ = true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;

        if (advertiser_) advertiser_->stop();

        if (grpc_server_) {
            grpc_server_->Shutdown();
            grpc_server_.reset();
        }

        debugger_service_.reset();
        system_service_.reset();
    }

    bool is_running() const { return running_; }
    uint16_t port() const { return port_; }
    std::string address() const { return address_; }

    void notify_shutdown(uint32_t grace_ms = 5000) {
        if (system_service_) {
            system_service_->notify_shutdown(grace_ms);
        }
    }

private:
    ParasiteRunner& runner_;
    std::string address_;
    uint16_t port_;

    ConnectionTracker connection_tracker_;
    std::unique_ptr<DebuggerControlServiceImpl<ParasiteRunner>> debugger_service_;
    std::unique_ptr<SystemServiceImpl<ParasiteRunner>> system_service_;
    std::unique_ptr<grpc::Server> grpc_server_;
    std::unique_ptr<discovery::Advertiser> advertiser_;

    std::atomic<bool> running_{false};
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_PARASITE_SERVER_HPP
