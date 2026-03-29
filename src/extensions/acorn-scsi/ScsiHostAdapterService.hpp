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
#include "ScsiBusEventBuffer.hpp"

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

    grpc::Status WatchBusEvents(
            grpc::ServerContext* context,
            const WatchScsiBusEventsRequest* request,
            grpc::ServerWriter<ScsiBusEvent>* writer) override {
        // Create an event buffer and attach it to the bus.
        // The buffer lives for the duration of this RPC call.
        ScsiBusEventBuffer buffer;
        adapter_.bus().set_event_buffer(&buffer);
        adapter_.bus().set_event_register_access(request->include_register_access());

        // Drain events from the buffer and stream them to the client
        // until the client disconnects or the server shuts down.
        beebium::ScsiInternalEvent internal_event;
        while (buffer.pop(internal_event)) {
            if (context->IsCancelled()) break;

            ::beebium::ScsiBusEvent proto_event;

            switch (internal_event.type) {
                case beebium::ScsiInternalEvent::Type::PhaseChange: {
                    auto* e = proto_event.mutable_phase_change();
                    e->set_from_phase(internal_event.from_phase);
                    e->set_to_phase(internal_event.to_phase);
                    break;
                }
                case beebium::ScsiInternalEvent::Type::Selection: {
                    auto* e = proto_event.mutable_selection();
                    e->set_target_id(internal_event.target_id);
                    e->set_success(internal_event.selection_success);
                    break;
                }
                case beebium::ScsiInternalEvent::Type::Command: {
                    auto* e = proto_event.mutable_command();
                    e->set_target_id(internal_event.target_id);
                    e->set_opcode(internal_event.opcode);
                    e->set_opcode_name(internal_event.opcode_name);
                    e->set_cdb(std::string(internal_event.cdb.begin(),
                                           internal_event.cdb.end()));
                    e->set_lba(internal_event.lba);
                    e->set_block_count(internal_event.block_count);
                    break;
                }
                case beebium::ScsiInternalEvent::Type::DataTransfer: {
                    auto* e = proto_event.mutable_data_transfer();
                    e->set_direction(internal_event.direction);
                    e->set_bytes_expected(internal_event.bytes_expected);
                    e->set_bytes_transferred(internal_event.bytes_transferred);
                    e->set_complete(internal_event.transfer_complete);
                    break;
                }
                case beebium::ScsiInternalEvent::Type::Status: {
                    auto* e = proto_event.mutable_status();
                    e->set_target_id(internal_event.target_id);
                    e->set_status_byte(internal_event.status_byte);
                    e->set_status_name(internal_event.status_name);
                    e->set_message_byte(internal_event.message_byte);
                    break;
                }
                case beebium::ScsiInternalEvent::Type::RegisterAccess: {
                    auto* e = proto_event.mutable_register_access();
                    e->set_operation(internal_event.operation);
                    e->set_register_index(internal_event.register_index);
                    e->set_register_name(internal_event.register_name);
                    e->set_value(internal_event.value);
                    e->set_phase(internal_event.phase);
                    break;
                }
            }

            if (!writer->Write(proto_event)) break;
        }

        // Detach the buffer from the bus before it goes out of scope
        adapter_.bus().set_event_buffer(nullptr);
        adapter_.bus().set_event_register_access(false);
        return grpc::Status::OK;
    }

private:
    AcornScsiHostAdapter& adapter_;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_HOST_ADAPTER_SERVICE_HPP
