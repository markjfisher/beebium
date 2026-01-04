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

#ifndef BEEBIUM_SERVICE_INDICATOR_SERVICE_HPP
#define BEEBIUM_SERVICE_INDICATOR_SERVICE_HPP

#include "indicator.grpc.pb.h"
#include "beebium/indicators/Indicators.hpp"

#include <grpcpp/grpcpp.h>
#include <concepts>
#include <mutex>
#include <thread>

namespace beebium::service {

// Concept to detect if a hardware type has indicators
template<typename T>
concept HasIndicators = requires(T& hw) {
    { hw.indicators } -> std::same_as<Indicators&>;
};

// gRPC service implementation for IndicatorService
template<typename MachineType>
class IndicatorServiceImpl final : public IndicatorService::Service {
public:
    explicit IndicatorServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~IndicatorServiceImpl() override = default;

    // Non-copyable
    IndicatorServiceImpl(const IndicatorServiceImpl&) = delete;
    IndicatorServiceImpl& operator=(const IndicatorServiceImpl&) = delete;

    grpc::Status GetIndicators(
        grpc::ServerContext* context,
        const GetIndicatorsRequest* request,
        GetIndicatorsResponse* response) override
    {
        (void)context;

        if constexpr (!HasIndicators<typename MachineType::Memory>) {
            // No indicators - return empty response
            response->set_sequence(0);
            response->set_changed(false);
            return grpc::Status::OK;
        } else {
            auto& indicators = machine_.state().memory.indicators;
            uint64_t current_seq = indicators.sequence();

            // Check if client already has current data
            if (request->if_changed_since() > 0 &&
                request->if_changed_since() >= current_seq) {
                response->set_sequence(current_seq);
                response->set_changed(false);
                return grpc::Status::OK;
            }

            // Return all indicator values
            auto values = indicators.values();
            for (const auto& [name, value] : values) {
                (*response->mutable_values())[name] = value;
            }

            response->set_sequence(current_seq);
            response->set_changed(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status ListIndicators(
        grpc::ServerContext* context,
        const ListIndicatorsRequest* request,
        ListIndicatorsResponse* response) override
    {
        (void)context;
        (void)request;

        if constexpr (!HasIndicators<typename MachineType::Memory>) {
            // No indicators - return empty list
            return grpc::Status::OK;
        } else {
            auto& indicators = machine_.state().memory.indicators;

            for (const auto& name : indicators.names()) {
                auto* info = response->add_indicators();
                info->set_name(name);

                auto metadata = indicators.metadata(name);
                for (const auto& [key, value] : metadata) {
                    (*info->mutable_metadata())[key] = value;
                }
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status Subscribe(
        grpc::ServerContext* context,
        const SubscribeIndicatorsRequest* request,
        grpc::ServerWriter<IndicatorUpdate>* writer) override
    {
        if constexpr (!HasIndicators<typename MachineType::Memory>) {
            // No indicators - return immediately
            return grpc::Status::OK;
        } else {
            auto& indicators = machine_.state().memory.indicators;

            // Determine update interval (default 20ms = 50Hz)
            auto interval = std::chrono::milliseconds(
                request->min_interval_ms() > 0 ? request->min_interval_ms() : 20);

            // Build filter set if names specified
            std::unordered_set<std::string> filter_names;
            for (const auto& name : request->names()) {
                filter_names.insert(name);
            }
            bool filter_enabled = !filter_names.empty();

            // Track previous values for change detection
            std::unordered_map<std::string, uint8_t> prev_values;
            uint64_t prev_seq = 0;

            while (!context->IsCancelled()) {
                std::this_thread::sleep_for(interval);

                uint64_t curr_seq = indicators.sequence();
                if (curr_seq == prev_seq) {
                    continue;  // No changes
                }

                auto curr_values = indicators.values();
                IndicatorUpdate update;
                bool has_changes = false;

                for (const auto& [name, value] : curr_values) {
                    // Apply name filter if enabled
                    if (filter_enabled && filter_names.find(name) == filter_names.end()) {
                        continue;
                    }

                    // Check if value changed
                    auto it = prev_values.find(name);
                    if (it == prev_values.end() || it->second != value) {
                        (*update.mutable_values())[name] = value;
                        has_changes = true;
                    }
                }

                if (has_changes) {
                    update.set_sequence(curr_seq);
                    if (!writer->Write(update)) {
                        break;  // Client disconnected
                    }
                }

                prev_values = std::move(curr_values);
                prev_seq = curr_seq;
            }

            return grpc::Status::OK;
        }
    }

private:
    MachineType& machine_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_INDICATOR_SERVICE_HPP
