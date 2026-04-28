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

#include "AcornScsiHostAdapter.hpp"
#include "ScsiHostAdapterService.hpp"

#include <beebium/indicators/IndicatorFilter.hpp>
#include <beebium/indicators/Indicators.hpp>

#include <stdexcept>
#include <utility>

namespace beebium {

AcornScsiHostAdapter::AcornScsiHostAdapter() {
    indicator_ids_.fill(kNoIndicator);
}
AcornScsiHostAdapter::~AcornScsiHostAdapter() = default;

std::unique_ptr<AcornScsiHostAdapter> AcornScsiHostAdapter::create() {
    auto adapter = std::unique_ptr<AcornScsiHostAdapter>(new AcornScsiHostAdapter());
    adapter->set_manifest(ExtensionManifest{
        "acorn-scsi",
        "Acorn SCSI Host Adapter for 1 MHz bus (0xFC40-0xFC43)",
        "acorn-scsi",
        {},   // cli_name (uses name)
        {},   // manifest_dirpath (built-in, no manifest file)
        {},   // parameters
    });
    return adapter;
}

void AcornScsiHostAdapter::init(ExtensionContext& ctx) {
    ctx.get<OneMHzBusPort>().claim_addresses(kBaseOffset, kEndOffset, *this);
    registry_.wire_to_bus(bus_);
    service_ = std::make_unique<ScsiHostAdapterServiceImpl>(*this);

    if (ctx.has_indicators()) {
        indicators_ = &ctx.indicators();
        // Single bus-level activity callback dispatches to the per-LUN
        // indicator (if one has been registered for that LUN). Activity for
        // unregistered LUNs is silently dropped.
        bus_.set_activity_callback(
            [this](uint8_t lun, bool active) {
                if (lun >= indicator_ids_.size()) return;
                uint16_t id = indicator_ids_[lun];
                if (id == kNoIndicator) return;
                indicators_->set(id, active ? uint8_t{255} : uint8_t{0});
            });
    }
}

void AcornScsiHostAdapter::register_target_indicator(
        uint8_t lun,
        std::string name,
        std::unordered_map<std::string, std::string> metadata) {
    if (!indicators_) {
        throw std::runtime_error(
            "AcornScsiHostAdapter::register_target_indicator: "
            "no Indicators registry (init() must run first, and the machine "
            "must provide one)");
    }
    if (lun >= indicator_ids_.size()) {
        throw std::out_of_range(
            "AcornScsiHostAdapter::register_target_indicator: LUN out of range");
    }
    uint16_t id = indicators_->register_indicator(
        std::move(name),
        std::make_unique<RetriggerableMonostableFilter>(kActivityPulseWidth),
        std::move(metadata));
    indicator_ids_[lun] = id;
}

void AcornScsiHostAdapter::shutdown() {
    service_.reset();
}

std::vector<grpc::Service*> AcornScsiHostAdapter::grpc_services() {
    if (service_) {
        return {service_.get()};
    }
    return {};
}

}  // namespace beebium
