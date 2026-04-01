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

namespace beebium {

AcornScsiHostAdapter::AcornScsiHostAdapter() = default;
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
