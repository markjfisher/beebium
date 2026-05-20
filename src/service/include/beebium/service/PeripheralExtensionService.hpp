// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_PERIPHERAL_EXTENSION_SERVICE_HPP
#define BEEBIUM_SERVICE_PERIPHERAL_EXTENSION_SERVICE_HPP

#include "peripheral_extension.grpc.pb.h"
#include "beebium/extension/ExtensionRegistry.hpp"

#include <grpcpp/grpcpp.h>

namespace beebium::service {

// gRPC service for discovering loaded peripheral extensions.
// This is a core service (always present), not templated on MachineType.
// It queries the ExtensionRegistry to enumerate loaded extensions.
class PeripheralExtensionServiceImpl final : public PeripheralExtensionService::Service {
public:
    explicit PeripheralExtensionServiceImpl(const ExtensionRegistry& registry)
        : registry_(registry) {}

    grpc::Status ListExtensions(
            grpc::ServerContext*,
            const ListExtensionsRequest*,
            ListExtensionsResponse* response) override {
        for (auto* ext : registry_.extensions()) {
            auto* info = response->add_extensions();
            info->set_name(std::string(ext->name()));
            info->set_id(std::string(ext->id()));
            info->set_label(std::string(ext->label()));
            info->set_description(std::string(ext->description()));
            info->set_has_ui(ext->ui() != nullptr);

            for (auto dep : ext->attaches_to()) {
                info->add_attaches_to(std::string(dep));
            }
            for (auto provided : ext->provides()) {
                info->add_provides(std::string(provided));
            }

            // Current instance config
            for (const auto& [key, value] : ext->config()) {
                (*info->mutable_config())[key] = value;
            }

            // Parameter schema from manifest
            for (const auto& param : ext->manifest().parameters) {
                auto* p = info->add_parameters();
                p->set_key(param.key);
                p->set_type(param.type);
                p->set_description(param.description);
                p->set_position(param.position);
                p->set_required(param.required);
                p->set_default_value(param.default_value);
            }
        }
        return grpc::Status::OK;
    }

private:
    const ExtensionRegistry& registry_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_PERIPHERAL_EXTENSION_SERVICE_HPP
