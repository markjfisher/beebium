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

            std::visit([&proto_event](auto&& ev) {
                using T = std::decay_t<decltype(ev)>;

                if constexpr (std::is_same_v<T, ScsiPhaseChangeEvent>) {
                    auto* e = proto_event.mutable_phase_change();
                    e->set_from_phase(ev.from_phase);
                    e->set_to_phase(ev.to_phase);
                } else if constexpr (std::is_same_v<T, ScsiSelectionEvent>) {
                    auto* e = proto_event.mutable_selection();
                    e->set_target_id(ev.target_id);
                    e->set_success(ev.success);
                } else if constexpr (std::is_same_v<T, ScsiCommandEvent>) {
                    auto* e = proto_event.mutable_command();
                    e->set_target_id(ev.target_id);
                    e->set_opcode(ev.opcode);
                    e->set_opcode_name(ev.opcode_name);
                    e->set_cdb(std::string(ev.cdb.begin(), ev.cdb.end()));
                    e->set_lba(ev.lba);
                    e->set_block_count(ev.block_count);
                } else if constexpr (std::is_same_v<T, ScsiDataTransferEvent>) {
                    auto* e = proto_event.mutable_data_transfer();
                    e->set_direction(ev.direction);
                    e->set_bytes_expected(ev.bytes_expected);
                    e->set_bytes_transferred(ev.bytes_transferred);
                    e->set_complete(ev.complete);
                } else if constexpr (std::is_same_v<T, ScsiStatusEvent>) {
                    auto* e = proto_event.mutable_status();
                    e->set_target_id(ev.target_id);
                    e->set_status_byte(ev.status_byte);
                    e->set_status_name(ev.status_name);
                    e->set_message_byte(ev.message_byte);
                } else if constexpr (std::is_same_v<T, ScsiRegisterAccessEvent>) {
                    auto* e = proto_event.mutable_register_access();
                    e->set_operation(ev.operation);
                    e->set_register_index(ev.register_index);
                    e->set_register_name(ev.register_name);
                    e->set_value(ev.value);
                    e->set_phase(ev.phase);
                }
            }, internal_event);

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
