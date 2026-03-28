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

#include "ScsiHardDiscExtension.hpp"

#include <AcornScsiHostAdapter.hpp>
#include <HardDiskImage.hpp>
#include <ScsiHardDisc.hpp>

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionManifest.hpp>

#include <stdexcept>

namespace beebium {

ScsiHardDiscExtension::ScsiHardDiscExtension() = default;
ScsiHardDiscExtension::~ScsiHardDiscExtension() = default;

std::unique_ptr<ScsiHardDiscExtension> ScsiHardDiscExtension::create() {
    auto ext = std::unique_ptr<ScsiHardDiscExtension>(new ScsiHardDiscExtension());
    ext->set_manifest(ExtensionManifest{
        "scsi-hard-disc",
        "SCSI hard disc target (DAT+DSC image)",
        "scsi-hard-disc",
        {}
    });
    return ext;
}

void ScsiHardDiscExtension::init(ExtensionContext& ctx) {
    // Discover the SCSI adapter via the "scsi" extension point
    adapter_ = ctx.provider("scsi");
    if (!adapter_) {
        throw std::runtime_error(
            "ScsiHardDiscExtension: no SCSI adapter found (missing 'scsi' provider)");
    }

    auto* scsi_adapter = dynamic_cast<AcornScsiHostAdapter*>(adapter_);
    if (!scsi_adapter) {
        throw std::runtime_error(
            "ScsiHardDiscExtension: 'scsi' provider is not an AcornScsiHostAdapter");
    }

    // If an image filepath was configured, load it and install as a target
    if (!image_filepath_.empty()) {
        auto image = HardDiskImage::open(image_filepath_);
        disc_ = std::make_unique<ScsiHardDisc>(std::move(image));
        scsi_adapter->target_registry().install(scsi_id_, std::move(disc_));
        scsi_adapter->target_registry().wire_to_bus(scsi_adapter->bus());
    }
}

void ScsiHardDiscExtension::shutdown() {
    // Target is owned by the registry, not by us, so nothing to clean up
    // (the registry's unique_ptr was moved in init)
    disc_ = nullptr;
    adapter_ = nullptr;
}

}  // namespace beebium
