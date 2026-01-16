// Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP
#define BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP

#include "sideways.grpc.pb.h"
#include "beebium/devices/ConfigurableSlot.hpp"
#include "beebium/devices/AliasedBankedMemory.hpp"
#include "beebium/devices/ConfigurableBankedMemory.hpp"

#include <grpcpp/grpcpp.h>
#include <mutex>
#include <fstream>
#include <filesystem>

namespace beebium::service {

// Concept to detect if memory has aliased sideways (Model B)
template<typename T>
concept HasAliasedSideways = requires {
    { T::SidewaysType::has_aliasing } -> std::convertible_to<bool>;
    requires T::SidewaysType::has_aliasing == true;
};

// Concept to detect if memory has configurable sideways (ROM/RAM board)
template<typename T>
concept HasConfigurableSideways = requires {
    { T::SidewaysType::has_aliasing } -> std::convertible_to<bool>;
    requires T::SidewaysType::has_aliasing == false;
};

// Concept to detect if memory has any type of configurable sideways
// Requires the has_aliasing static member to distinguish from legacy BankedMemory
template<typename T>
concept HasSideways = requires(T t) {
    { t.sideways } -> std::convertible_to<typename T::SidewaysType&>;
    { T::SidewaysType::has_aliasing } -> std::convertible_to<bool>;
};

// Helper to convert SlotType to protobuf enum
inline beebium::SidewaysSlotType slot_type_to_proto(beebium::SlotType type) {
    switch (type) {
        case beebium::SlotType::Empty: return beebium::SIDEWAYS_SLOT_TYPE_EMPTY;
        case beebium::SlotType::Rom: return beebium::SIDEWAYS_SLOT_TYPE_ROM;
        case beebium::SlotType::Ram: return beebium::SIDEWAYS_SLOT_TYPE_RAM;
        default: return beebium::SIDEWAYS_SLOT_TYPE_EMPTY;
    }
}

// Helper to convert protobuf enum to SlotType
inline beebium::SlotType proto_to_slot_type(beebium::SidewaysSlotType proto_type) {
    switch (proto_type) {
        case beebium::SIDEWAYS_SLOT_TYPE_EMPTY: return beebium::SlotType::Empty;
        case beebium::SIDEWAYS_SLOT_TYPE_ROM: return beebium::SlotType::Rom;
        case beebium::SIDEWAYS_SLOT_TYPE_RAM: return beebium::SlotType::Ram;
        default: return beebium::SlotType::Empty;
    }
}

// gRPC service implementation for SidewaysService
template<typename MachineType>
class SidewaysServiceImpl final : public SidewaysService::Service {
public:
    explicit SidewaysServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~SidewaysServiceImpl() override = default;

    // Non-copyable
    SidewaysServiceImpl(const SidewaysServiceImpl&) = delete;
    SidewaysServiceImpl& operator=(const SidewaysServiceImpl&) = delete;

    grpc::Status GetSlotStatus(
        grpc::ServerContext* context,
        const GetSlotStatusRequest* request,
        GetSlotStatusResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasSideways<Memory>) {
            // Machine has no sideways memory
            response->set_has_aliasing(false);
            response->set_num_physical_slots(0);
            response->set_selected_bank(0);
            return grpc::Status::OK;
        } else {
            auto& sideways = machine_.state().memory.sideways;
            response->set_selected_bank(sideways.selected_bank());

            if constexpr (HasAliasedSideways<Memory>) {
                // Model B with 4 aliased sockets
                response->set_has_aliasing(true);
                response->set_num_physical_slots(4);

                for (uint8_t socket_idx = 0; socket_idx < 4; ++socket_idx) {
                    auto* socket_status = response->add_sockets();
                    socket_status->set_socket_index(socket_idx);

                    // Add aliased slots for this socket
                    auto aliased = AliasedBankedMemory::socket_aliased_slots(socket_idx);
                    for (uint8_t slot : aliased) {
                        socket_status->add_aliased_slots(slot);
                    }

                    // Get socket info
                    const auto& slot = sideways.socket(socket_idx);
                    socket_status->set_type(slot_type_to_proto(slot.type()));
                    socket_status->set_populated(slot.is_populated());
                    socket_status->set_image_name(std::string(slot.image_name()));
                    socket_status->set_socket_label(
                        std::string(AliasedBankedMemory::socket_names[socket_idx]));
                }
            } else if constexpr (HasConfigurableSideways<Memory>) {
                // ROM/RAM board with 16 independent slots
                response->set_has_aliasing(false);
                response->set_num_physical_slots(16);

                for (uint8_t slot_idx = 0; slot_idx < 16; ++slot_idx) {
                    auto* socket_status = response->add_sockets();
                    socket_status->set_socket_index(slot_idx);

                    // No aliasing - slot maps to itself
                    socket_status->add_aliased_slots(slot_idx);

                    // Get slot info
                    const auto& slot = sideways.slot(slot_idx);
                    socket_status->set_type(slot_type_to_proto(slot.type()));
                    socket_status->set_populated(slot.is_populated());
                    socket_status->set_image_name(std::string(slot.image_name()));
                    socket_status->set_socket_label("Slot " + std::to_string(slot_idx));
                }
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status ConfigureSlot(
        grpc::ServerContext* context,
        const ConfigureSlotRequest* request,
        ConfigureSlotResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasSideways<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no configurable sideways memory");
            return grpc::Status::OK;
        } else {
            uint32_t slot = request->slot();
            if (slot > 15) {
                response->set_success(false);
                response->set_error("Invalid slot number (must be 0-15)");
                return grpc::Status::OK;
            }

            auto& sideways = machine_.state().memory.sideways;
            beebium::SlotType new_type = proto_to_slot_type(request->type());

            // Determine physical socket for response
            uint32_t actual_socket = slot;
            if constexpr (HasAliasedSideways<Memory>) {
                actual_socket = AliasedBankedMemory::slot_to_socket(slot);
            }
            response->set_actual_socket(actual_socket);

            // Configure the slot type
            if constexpr (HasAliasedSideways<Memory>) {
                sideways.configure_socket(actual_socket, new_type);
            } else {
                sideways.configure_slot(slot, new_type);
            }

            // Load image data if provided
            if (request->has_url()) {
                // Load from file
                std::string url = request->url();
                std::string filepath;

                // Strip file:// prefix if present
                if (url.substr(0, 7) == "file://") {
                    filepath = url.substr(7);
                } else {
                    filepath = url;
                }

                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                if (!file) {
                    response->set_success(false);
                    response->set_error("Failed to open file: " + filepath);
                    return grpc::Status::OK;
                }

                auto size = file.tellg();
                if (size > 16384) {
                    response->set_success(false);
                    response->set_error("Image too large (max 16384 bytes)");
                    return grpc::Status::OK;
                }

                file.seekg(0, std::ios::beg);
                std::vector<uint8_t> data(static_cast<size_t>(size));
                file.read(reinterpret_cast<char*>(data.data()), size);

                // Load data into slot
                if constexpr (HasAliasedSideways<Memory>) {
                    sideways.load_rom_to_socket(actual_socket, data.data(), data.size());
                    sideways.set_socket_image_name(actual_socket,
                        std::filesystem::path(filepath).filename().string());
                } else {
                    sideways.load_rom(slot, data.data(), data.size());
                    sideways.set_slot_image_name(slot,
                        std::filesystem::path(filepath).filename().string());
                }

                response->set_image_name(
                    std::filesystem::path(filepath).filename().string());

            } else if (request->has_data()) {
                // Load from raw bytes
                const std::string& data = request->data();
                if (data.size() > 16384) {
                    response->set_success(false);
                    response->set_error("Image too large (max 16384 bytes)");
                    return grpc::Status::OK;
                }

                if constexpr (HasAliasedSideways<Memory>) {
                    sideways.load_rom_to_socket(actual_socket,
                        reinterpret_cast<const uint8_t*>(data.data()), data.size());
                } else {
                    sideways.load_rom(slot,
                        reinterpret_cast<const uint8_t*>(data.data()), data.size());
                }
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status ReadSlotData(
        grpc::ServerContext* context,
        const ReadSlotDataRequest* request,
        ReadSlotDataResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasSideways<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no sideways memory");
            return grpc::Status::OK;
        } else {
            uint32_t slot = request->slot();
            uint32_t offset = request->offset();
            uint32_t length = request->length();

            if (slot > 15) {
                response->set_success(false);
                response->set_error("Invalid slot number (must be 0-15)");
                return grpc::Status::OK;
            }

            if (offset > 16383) {
                response->set_success(false);
                response->set_error("Invalid offset (must be 0-16383)");
                return grpc::Status::OK;
            }

            if (length == 0) {
                length = 16384 - offset;  // Read to end of slot
            }

            if (offset + length > 16384) {
                length = 16384 - offset;  // Clamp to slot boundary
            }

            auto& sideways = machine_.state().memory.sideways;

            // Read data from slot
            std::string data;
            data.reserve(length);
            for (uint32_t i = 0; i < length; ++i) {
                data.push_back(static_cast<char>(
                    sideways.peek_bank(slot, static_cast<uint16_t>(offset + i))));
            }

            response->set_success(true);
            response->set_data(std::move(data));
            response->set_type(slot_type_to_proto(sideways.bank_type(slot)));

            return grpc::Status::OK;
        }
    }

    grpc::Status SubscribeEvents(
        grpc::ServerContext* context,
        const SubscribeEventsRequest* request,
        grpc::ServerWriter<SidewaysEvent>* writer) override
    {
        using Memory = typename MachineType::Memory;

        if constexpr (!HasSideways<Memory>) {
            // No sideways memory - just return immediately
            return grpc::Status::OK;
        } else {
            uint32_t min_interval_ms = request->min_interval_ms();
            if (min_interval_ms < 10) {
                min_interval_ms = 10;  // Minimum 10ms between updates
            }

            uint8_t last_bank = 255;  // Invalid initial value

            while (!context->IsCancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(min_interval_ms));

                std::lock_guard<std::mutex> lock(mutex_);

                auto& sideways = machine_.state().memory.sideways;
                uint8_t current_bank = sideways.selected_bank();

                if (current_bank != last_bank) {
                    SidewaysEvent event;
                    event.set_timestamp_cycles(machine_.cycle_count());

                    auto* bank_event = event.mutable_bank_selected();
                    bank_event->set_bank(current_bank);

                    if constexpr (HasAliasedSideways<Memory>) {
                        bank_event->set_socket(AliasedBankedMemory::slot_to_socket(current_bank));
                    } else {
                        bank_event->set_socket(current_bank);
                    }

                    writer->Write(event);
                    last_bank = current_bank;
                }
            }

            return grpc::Status::OK;
        }
    }

private:
    MachineType& machine_;
    std::mutex mutex_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP
