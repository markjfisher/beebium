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

#ifndef BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP
#define BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP

#include "sideways.grpc.pb.h"
#include "beebium/SlotTopology.hpp"
#include "beebium/SidewaysRomHeader.hpp"
#include "beebium/devices/ConfigurableSlot.hpp"
#include "beebium/devices/AliasedBankedMemory.hpp"
#include "beebium/devices/ConfigurableBankedMemory.hpp"

#include <grpcpp/grpcpp.h>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <vector>

namespace beebium::service {

// Concept to detect if memory has aliased sideways with per-socket runtime
// accessors (Model B's AliasedBankedMemory). Requires both the aliasing
// trait and a socket() method - so a B+ BankedMemory<...> with aliasing
// but no socket() accessor does NOT match.
template<typename T>
concept HasAliasedSideways = requires(T t) {
    { T::SidewaysType::has_aliasing } -> std::convertible_to<bool>;
    requires T::SidewaysType::has_aliasing == true;
    { t.sideways.socket(uint8_t{0}) };
};

// Concept to detect if memory has configurable sideways with per-slot
// runtime accessors (ROM/RAM board's ConfigurableBankedMemory).
template<typename T>
concept HasConfigurableSideways = requires(T t) {
    { T::SidewaysType::has_aliasing } -> std::convertible_to<bool>;
    requires T::SidewaysType::has_aliasing == false;
    { t.sideways.slot(uint8_t{0}) };
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

// Populate proto SocketCapabilities from a SocketSpec (topology source of truth).
inline void fill_capabilities(beebium::SocketCapabilities* caps,
                              const beebium::SocketSpec& spec) {
    caps->set_supports_rom(spec.supports_rom);
    caps->set_supports_ram(spec.supports_ram);
    caps->set_supports_empty(spec.supports_empty);
    caps->set_runtime_configurable(spec.runtime_configurable);
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
    using Memory = typename MachineType::Memory;
    using MotherboardLinks = typename Memory::MotherboardLinks;

    explicit SidewaysServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~SidewaysServiceImpl() override = default;

    // Non-copyable
    SidewaysServiceImpl(const SidewaysServiceImpl&) = delete;
    SidewaysServiceImpl& operator=(const SidewaysServiceImpl&) = delete;

    // Capture the motherboard link state the server was configured with at
    // start-up so GetSlotStatus reports the correct topology and link list
    // to clients. Motherboard links are not runtime-mutable; this is a
    // one-shot setter called by the server bootstrap code.
    void set_motherboard_links(const MotherboardLinks& links) {
        std::lock_guard<std::mutex> lock(mutex_);
        motherboard_links_ = links;
    }

    grpc::Status GetSlotStatus(
        grpc::ServerContext* context,
        const GetSlotStatusRequest* request,
        GetSlotStatusResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        // Topology comes from the machine variant when available, and is
        // the source of truth for socket labels, alias sets, and capability
        // flags. The link-aware overload is used so machines like the
        // Model B+ (where S13 changes which slots IC71 owns) report what
        // the user actually asked for at startup. The runtime per-slot
        // type/image_name fields are read from the live device when
        // accessible.
        if constexpr (requires { Memory::slot_topology(motherboard_links_); }) {
            auto topo = Memory::slot_topology(motherboard_links_);

            // Report the link state itself so clients can display it.
            for (const auto& info : motherboard_links_.describe()) {
                auto* link = response->add_motherboard_links();
                link->set_name(info.name);
                link->set_value(info.value);
                link->set_description(info.description);
            }

            response->set_has_aliasing(topo.has_aliasing);
            response->set_num_physical_slots(
                static_cast<uint32_t>(topo.sockets.size()));

            for (const auto& spec : topo.sockets) {
                auto* socket_status = response->add_sockets();
                socket_status->set_socket_index(spec.socket_index);
                socket_status->set_socket_label(spec.label);
                for (int slot : spec.slots) {
                    socket_status->add_aliased_slots(static_cast<uint32_t>(slot));
                }
                fill_capabilities(
                    socket_status->mutable_capabilities(), spec);

                // Peek the slot's 16 KiB and run the standard
                // sideways-ROM-header parser. An empty socket or a
                // non-standard image produces recognised=false. The parse
                // result drives both rom_header and (in the fallback
                // branch below) type/populated for machines that don't
                // expose per-socket runtime accessors.
                beebium::SidewaysRomHeader parsed;
                if constexpr (HasSideways<Memory>) {
                    if (!spec.slots.empty()) {
                        auto& sw = machine_.state().memory.sideways;
                        const auto probe = static_cast<uint8_t>(spec.slots[0]);
                        std::vector<uint8_t> bytes(16384);
                        for (uint16_t i = 0; i < 16384; ++i) {
                            bytes[i] = sw.peek_bank(probe, i);
                        }
                        parsed = beebium::parse_sideways_rom_header(bytes);
                    }
                }

                // Runtime fields: read from the live device when the device
                // exposes per-socket / per-slot accessors. Otherwise derive
                // type/populated from the parsed header - if the parser
                // recognised a ROM signature there are bytes there, if not
                // the slot reads as open bus (empty).
                if constexpr (HasAliasedSideways<Memory>) {
                    auto& sideways = machine_.state().memory.sideways;
                    const auto& slot = sideways.socket(
                        static_cast<uint8_t>(spec.socket_index));
                    socket_status->set_type(slot_type_to_proto(slot.type()));
                    socket_status->set_populated(slot.is_populated());
                    socket_status->set_image_name(std::string(slot.image_name()));
                } else if constexpr (HasConfigurableSideways<Memory>) {
                    auto& sideways = machine_.state().memory.sideways;
                    const auto& slot = sideways.slot(
                        static_cast<uint8_t>(spec.socket_index));
                    socket_status->set_type(slot_type_to_proto(slot.type()));
                    socket_status->set_populated(slot.is_populated());
                    socket_status->set_image_name(std::string(slot.image_name()));
                } else {
                    // Model B+ or similar BankedMemory<...> wiring: no
                    // per-socket runtime accessors. A recognised header
                    // is the cleanest signal that a ROM image was loaded;
                    // an unrecognised peek means the socket is empty or
                    // holds a non-standard image (rare on the B+, which
                    // ships with recognised Acorn ROMs).
                    socket_status->set_type(
                        parsed.recognised
                            ? beebium::SIDEWAYS_SLOT_TYPE_ROM
                            : beebium::SIDEWAYS_SLOT_TYPE_EMPTY);
                    socket_status->set_populated(parsed.recognised);
                    socket_status->set_image_name("");
                }

                if (parsed.recognised) {
                    auto* h = socket_status->mutable_rom_header();
                    h->set_recognised(true);
                    h->set_title(parsed.title);
                    h->set_version(parsed.version);
                    h->set_copyright(parsed.copyright);
                    h->set_contains_romfs(parsed.contains_romfs);
                    if (parsed.has_language_entry) h->add_kinds("language");
                    if (parsed.has_service_entry) h->add_kinds("service");
                    if (parsed.contains_romfs) h->add_kinds("romfs");
                }
            }
            return grpc::Status::OK;
        } else if constexpr (!HasSideways<Memory>) {
            // Machine has no sideways memory and no topology.
            response->set_has_aliasing(false);
            response->set_num_physical_slots(0);
            return grpc::Status::OK;
        } else {
            // Should be unreachable: any machine with sideways now also
            // provides slot_topology().
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
            uint32_t slot_num = request->slot();
            if (slot_num > 15) {
                response->set_success(false);
                response->set_error("Invalid slot number (must be 0-15)");
                return grpc::Status::OK;
            }
            uint8_t slot = static_cast<uint8_t>(slot_num);

            // Reject runtime configuration of sockets the topology marks as
            // not runtime_configurable. On real hardware (Model B, Model B+,
            // Master 128 internal sockets) this is a power-off chip-swap
            // operation; only the fantasy ROM/RAM expansion board and
            // future cartridge slots support hot reconfiguration.
            if constexpr (requires { Memory::slot_topology(motherboard_links_); }) {
                auto topo = Memory::slot_topology(motherboard_links_);
                const auto* spec = topo.find_socket_for_slot(
                    static_cast<int>(slot));
                if (spec == nullptr) {
                    response->set_success(false);
                    response->set_error(
                        "Slot " + std::to_string(slot)
                        + " does not exist on this machine variant");
                    return grpc::Status::OK;
                }
                if (!spec->runtime_configurable) {
                    response->set_success(false);
                    response->set_error(
                        "Socket " + spec->label
                        + " is not runtime-reconfigurable on this machine "
                          "variant; configure at startup with --sideways");
                    return grpc::Status::OK;
                }
            }

            auto& sideways = machine_.state().memory.sideways;
            beebium::SlotType new_type = proto_to_slot_type(request->type());

            // Determine physical socket for response
            uint8_t actual_socket = slot;
            if constexpr (HasAliasedSideways<Memory>) {
                actual_socket = AliasedBankedMemory::slot_to_socket(slot);
            }
            response->set_actual_socket(actual_socket);

            // Configure the slot type. The runtime_configurable check above
            // returns an error for machines whose sockets don't support
            // runtime reconfiguration (Model B, Model B+), so only the two
            // configurable-memory branches need to compile here.
            if constexpr (HasAliasedSideways<Memory>) {
                sideways.configure_socket(actual_socket, new_type);
            } else if constexpr (HasConfigurableSideways<Memory>) {
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

                // Load data into slot. Store the full filepath as image_name
                // so the Memory sidebar's Copy Path / Reveal in Finder actions
                // have something useful; clients can take the basename for
                // display when they want a name.
                if constexpr (HasAliasedSideways<Memory>) {
                    sideways.load_rom_to_socket(actual_socket, data.data(), data.size());
                    sideways.set_socket_image_name(actual_socket, filepath);
                } else if constexpr (HasConfigurableSideways<Memory>) {
                    sideways.load_rom(slot, data.data(), data.size());
                    sideways.set_slot_image_name(slot, filepath);
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
                } else if constexpr (HasConfigurableSideways<Memory>) {
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
            uint32_t slot_num = request->slot();
            uint32_t offset = request->offset();
            uint32_t length = request->length();

            if (slot_num > 15) {
                response->set_success(false);
                response->set_error("Invalid slot number (must be 0-15)");
                return grpc::Status::OK;
            }
            uint8_t slot = static_cast<uint8_t>(slot_num);

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
            if constexpr (HasAliasedSideways<Memory>
                          || HasConfigurableSideways<Memory>) {
                response->set_type(slot_type_to_proto(sideways.bank_type(slot)));
            } else {
                // BankedMemory<...> (Model B+): every wired slot holds a ROM,
                // no runtime configurability.
                response->set_type(beebium::SIDEWAYS_SLOT_TYPE_ROM);
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status SubscribeEvents(
        grpc::ServerContext* context,
        const SubscribeEventsRequest* request,
        grpc::ServerWriter<SidewaysEvent>* writer) override
    {
        using Memory = typename MachineType::Memory;

        (void)request;
        (void)writer;
        if constexpr (!HasSideways<Memory>) {
            // No sideways memory - just return immediately.
            return grpc::Status::OK;
        } else {
            // The only events this stream is ever going to carry are
            // SlotConfiguredEvent (after ConfigureSlot landed) and the
            // forthcoming SlotHeaderChangedEvent (docs/discussion/
            // sideways-live-header-updates.md). Neither emits yet, so for
            // now this is a block-until-cancelled rendezvous - the stream
            // exists so clients can hold it open and start receiving
            // events as soon as the emitters are wired up.
            while (!context->IsCancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return grpc::Status::OK;
        }
    }

private:
    MachineType& machine_;
    std::mutex mutex_;
    MotherboardLinks motherboard_links_{};
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP
