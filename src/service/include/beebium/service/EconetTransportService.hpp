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

#ifndef BEEBIUM_SERVICE_ECONET_TRANSPORT_SERVICE_HPP
#define BEEBIUM_SERVICE_ECONET_TRANSPORT_SERVICE_HPP

// Transport-agnostic discovery service. Reads from the
// EconetTransportRegistry that ServerMain populates at startup; lets
// clients ask "which Econet transport is currently active?" so they
// know whether to use AunService, PiconetService, etc. for
// transport-specific operations.

#include "beebium/extension/EconetTransportRegistry.hpp"

#include "econet_transport.grpc.pb.h"

#include <grpcpp/grpcpp.h>

namespace beebium::service {

class EconetTransportServiceImpl final
    : public ::beebium::EconetTransportService::Service {
public:
    explicit EconetTransportServiceImpl(EconetTransportRegistry& registry)
        : registry_(registry) {}

    grpc::Status ListTransports(grpc::ServerContext*,
                                const ::beebium::ListTransportsRequest*,
                                ::beebium::ListTransportsResponse* response) override {
        for (const auto& ext : registry_.extensions()) {
            // For BBC machines at most one transport exists, so any
            // entry that's in the registry is by definition active.
            fill_transport(*ext, /*active=*/true, response->add_transports());
        }
        return grpc::Status::OK;
    }

    grpc::Status GetActiveTransport(grpc::ServerContext*,
                                    const ::beebium::GetActiveTransportRequest*,
                                    ::beebium::GetActiveTransportResponse* response) override {
        if (registry_.empty()) {
            return grpc::Status::OK;  // unset 'active' field
        }
        // BBC variants: at most one transport. Multi-transport machines
        // (future Acorn Econet Bridge) should use ListTransports.
        auto& ext = *registry_.extensions().front();
        fill_transport(ext, /*active=*/true, response->mutable_active());
        return grpc::Status::OK;
    }

private:
    // Populate a proto EconetTransport from a transport extension. The
    // `id` and `has_ui` fields are what frontends use to drive the
    // transport's Extension UI panel via ExtensionUiService: the id is
    // the subscription key, has_ui says whether a panel exists at all.
    static void fill_transport(EconetTransportExtension& ext, bool active,
                               ::beebium::EconetTransport* out) {
        out->set_name(std::string(ext.name()));
        out->set_description(std::string(ext.description()));
        out->set_active(active);
        out->set_id(std::string(ext.id()));
        out->set_has_ui(ext.ui() != nullptr);
    }

    EconetTransportRegistry& registry_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_ECONET_TRANSPORT_SERVICE_HPP
