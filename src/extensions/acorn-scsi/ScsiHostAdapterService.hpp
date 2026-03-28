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

#ifndef BEEBIUM_SCSI_HOST_ADAPTER_SERVICE_HPP
#define BEEBIUM_SCSI_HOST_ADAPTER_SERVICE_HPP

#include "scsi_host_adapter.grpc.pb.h"
#include "AcornScsiHostAdapter.hpp"

#include <grpcpp/grpcpp.h>

namespace beebium {

class ScsiHostAdapterServiceImpl final : public ScsiHostAdapterService::Service {
public:
    explicit ScsiHostAdapterServiceImpl(AcornScsiHostAdapter& adapter)
        : adapter_(adapter) {}

    grpc::Status ListTargets(
            grpc::ServerContext*,
            const ListScsiTargetsRequest*,
            ListScsiTargetsResponse* response) override {
        auto infos = adapter_.target_registry().enumerate();
        for (const auto& info : infos) {
            auto* target = response->add_targets();
            target->set_id(info.id);
            target->set_present(info.present);
            target->set_device_type(std::string(info.device_type));
            target->set_description(std::string(info.description));
        }
        return grpc::Status::OK;
    }

    grpc::Status GetBusStatus(
            grpc::ServerContext*,
            const GetScsiBusStatusRequest*,
            GetScsiBusStatusResponse* response) override {
        response->set_phase(std::string(scsi_phase_name(adapter_.bus().phase())));
        response->set_selected_target(adapter_.bus().selected_target_id());
        response->set_status_register(adapter_.bus().status_register());
        response->set_irq_pending(adapter_.bus().irq_pending());
        return grpc::Status::OK;
    }

private:
    AcornScsiHostAdapter& adapter_;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_HOST_ADAPTER_SERVICE_HPP
