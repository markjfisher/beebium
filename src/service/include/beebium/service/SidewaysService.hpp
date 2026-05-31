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
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
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
        : machine_(machine)
    {
        // Spin up the live header scanner. The tick is cheap when no
        // subscriber has asked for it; the thread runs for the service's
        // lifetime so subscribe/unsubscribe doesn't have to start/stop
        // threads. See docs/discussion/sideways-live-header-updates.md.
        if constexpr (HasSideways<Memory>) {
            scanner_thread_ = std::thread([this] { run_header_scanner(); });
        }
    }

    ~SidewaysServiceImpl() override {
        scanner_running_.store(false);
        if (scanner_thread_.joinable()) {
            scanner_thread_.join();
        }
    }

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
                    // Model B+ or B+ 128K BankedMemory<...> wiring: no
                    // per-socket runtime accessors. Two signals drive the
                    // reported type:
                    //
                    //   1. If the topology marks this socket as fixed RAM
                    //      (RAM-only, no ROM/empty), it's an integral
                    //      sideways RAM bank - report SLOT_TYPE_RAM. The
                    //      B+ 128K's SRAM W/X/Y/Z fit this.
                    //
                    //   2. Otherwise the slot is a ROM socket. A
                    //      recognised header means a ROM image is loaded
                    //      (SLOT_TYPE_ROM, populated); an unrecognised
                    //      peek means it's empty/open-bus.
                    const bool is_fixed_ram =
                        spec.supports_ram && !spec.supports_rom
                        && !spec.supports_empty;
                    if (is_fixed_ram) {
                        socket_status->set_type(
                            beebium::SIDEWAYS_SLOT_TYPE_RAM);
                        socket_status->set_populated(true);
                    } else {
                        socket_status->set_type(
                            parsed.recognised
                                ? beebium::SIDEWAYS_SLOT_TYPE_ROM
                                : beebium::SIDEWAYS_SLOT_TYPE_EMPTY);
                        socket_status->set_populated(parsed.recognised);
                    }
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
        if constexpr (!HasSideways<Memory>) {
            (void)request;
            (void)writer;
            return grpc::Status::OK;
        } else {
            auto sub = std::make_shared<HeaderSubscriber>();
            sub->writer = writer;
            sub->monitor_headers = request->monitor_header_changes();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                subscribers_.push_back(sub);
            }

            // Block here until the RPC is cancelled. The scanner thread
            // writes to `sub->writer` under `mutex_`; we drop the
            // subscriber from the list under the same lock once the
            // stream is closing so the scanner can never touch a stale
            // writer.
            while (!context->IsCancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                subscribers_.erase(
                    std::remove(subscribers_.begin(), subscribers_.end(), sub),
                    subscribers_.end());
            }
            return grpc::Status::OK;
        }
    }

private:
    // One open SubscribeEvents stream. Captured under mutex_ so the
    // scanner can write to it concurrently with the RPC handler holding
    // the stream open.
    struct HeaderSubscriber {
        grpc::ServerWriter<SidewaysEvent>* writer = nullptr;
        bool monitor_headers = false;
    };

    // Per-slot scanner state. last_hash short-circuits the parse when
    // bytes are unchanged; the last_emitted_* fields are the value last
    // delivered to subscribers so we only emit on a real difference.
    struct SlotState {
        bool hash_valid = false;
        size_t last_hash = 0;
        bool emitted_recognised = false;
        std::string emitted_title;
        std::string emitted_version;
        std::string emitted_copyright;
        bool emitted_contains_romfs = false;
        std::vector<std::string> emitted_kinds;
    };

    // 1 Hz scanner: while any subscriber has opted into
    // monitor_header_changes, walk RAM-typed slots, hash-gate, parse,
    // and emit SlotHeaderChangedEvent when the parsed RomHeader for any
    // slot diverges from what we last broadcast.
    //
    // Threading: scanner_running_ is an atomic flag the destructor
    // clears; the thread polls it every 100 ms so shutdown is prompt.
    // Subscriber list and per-slot state are mutated under mutex_.
    // ServerWriter::Write is called with mutex_ held - that serialises
    // writes to any single subscriber (gRPC requires that) and also
    // serialises against subscribe/unsubscribe.
    void run_header_scanner() {
        scanner_running_.store(true);
        using namespace std::chrono_literals;
        while (scanner_running_.load()) {
            // Sleep in 100 ms slices so destruction never has to wait a
            // full second for the thread to wake.
            for (int slice = 0; slice < 10 && scanner_running_.load(); ++slice) {
                std::this_thread::sleep_for(100ms);
            }
            if (!scanner_running_.load()) break;
            try {
                scan_tick();
            } catch (...) {
                // The scanner must never take the service down. Swallow
                // any anomaly and retry on the next tick.
            }
        }
    }

    void scan_tick() {
        if constexpr (HasSideways<Memory>) {
            std::lock_guard<std::mutex> lock(mutex_);

            // Refcount gate: any subscriber actually wants this work?
            bool any_watcher = false;
            for (const auto& sub : subscribers_) {
                if (sub->monitor_headers) {
                    any_watcher = true;
                    break;
                }
            }
            if (!any_watcher) {
                return;
            }

            if constexpr (!requires { Memory::slot_topology(motherboard_links_); }) {
                return;
            } else {
                auto topo = Memory::slot_topology(motherboard_links_);
                auto& sw = machine_.state().memory.sideways;

                for (const auto& spec : topo.sockets) {
                    if (spec.slots.empty()) continue;
                    const uint8_t probe_slot =
                        static_cast<uint8_t>(spec.slots[0]);

                    // Skip non-RAM slots. ROM contents don't change between
                    // ConfigureSlot calls, and machines without per-socket
                    // accessors (Model B+ BankedMemory) have no RAM at all.
                    bool is_ram = false;
                    if constexpr (HasAliasedSideways<Memory>) {
                        is_ram = sw.socket(
                            static_cast<uint8_t>(spec.socket_index)
                        ).type() == beebium::SlotType::Ram;
                    } else if constexpr (HasConfigurableSideways<Memory>) {
                        is_ram = sw.slot(
                            static_cast<uint8_t>(spec.socket_index)
                        ).type() == beebium::SlotType::Ram;
                    }
                    if (!is_ram) continue;

                    scan_slot(sw, probe_slot);
                }
            }
        }
    }

    // Sample one RAM slot. Skip when bytes are unchanged (cheap hash
    // gate). On change, parse the full bank and emit if the resulting
    // RomHeader differs from the last value we broadcast for this slot.
    template<typename Sideways>
    void scan_slot(Sideways& sw, uint8_t slot) {
        if (slot >= slot_states_.size()) return;

        std::vector<uint8_t> bytes(16384);
        for (uint16_t i = 0; i < 16384; ++i) {
            bytes[i] = sw.peek_bank(slot, i);
        }

        const size_t h = std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(bytes.data()),
                             bytes.size())
        );

        auto& state = slot_states_[slot];
        if (state.hash_valid && state.last_hash == h) {
            return;
        }
        state.last_hash = h;
        state.hash_valid = true;

        const auto parsed = beebium::parse_sideways_rom_header(bytes);
        std::vector<std::string> parsed_kinds;
        if (parsed.has_language_entry) parsed_kinds.emplace_back("language");
        if (parsed.has_service_entry) parsed_kinds.emplace_back("service");
        if (parsed.contains_romfs) parsed_kinds.emplace_back("romfs");

        const bool same =
            state.emitted_recognised == parsed.recognised
            && state.emitted_title == parsed.title
            && state.emitted_version == parsed.version
            && state.emitted_copyright == parsed.copyright
            && state.emitted_contains_romfs == parsed.contains_romfs
            && state.emitted_kinds == parsed_kinds;
        if (same) {
            return;
        }

        state.emitted_recognised = parsed.recognised;
        state.emitted_title = parsed.title;
        state.emitted_version = parsed.version;
        state.emitted_copyright = parsed.copyright;
        state.emitted_contains_romfs = parsed.contains_romfs;
        state.emitted_kinds = parsed_kinds;

        SidewaysEvent event;
        event.set_timestamp_cycles(machine_.cycle_count());
        auto* slot_changed = event.mutable_slot_header_changed();
        slot_changed->set_slot(slot);
        if (parsed.recognised) {
            auto* h_proto = slot_changed->mutable_rom_header();
            h_proto->set_recognised(true);
            h_proto->set_title(parsed.title);
            h_proto->set_version(parsed.version);
            h_proto->set_copyright(parsed.copyright);
            h_proto->set_contains_romfs(parsed.contains_romfs);
            for (const auto& kind : parsed_kinds) {
                h_proto->add_kinds(kind);
            }
        }
        // recognised=false case: leave rom_header unset so clients see a
        // cleared header (RamHeader.recognised reads as false / has_field
        // returns false).

        for (const auto& sub : subscribers_) {
            if (sub->monitor_headers && sub->writer) {
                sub->writer->Write(event);
            }
        }
    }

    MachineType& machine_;
    std::mutex mutex_;
    MotherboardLinks motherboard_links_{};

    // Header scanner state. subscribers_ and slot_states_ are guarded
    // by mutex_; scanner_running_ is an atomic flag for the thread.
    std::vector<std::shared_ptr<HeaderSubscriber>> subscribers_;
    std::array<SlotState, 16> slot_states_{};
    std::atomic<bool> scanner_running_{false};
    std::thread scanner_thread_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP
