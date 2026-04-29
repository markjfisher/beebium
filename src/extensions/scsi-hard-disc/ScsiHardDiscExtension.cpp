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
        .name = "scsi-hard-disc",
        .description = "SCSI hard disc target (DAT+DSC image)",
        .library_stem = "scsi-hard-disc",
    });
    return ext;
}

void ScsiHardDiscExtension::init(ExtensionContext& ctx) {
    // Discover the SCSI adapter via the "scsi" extension point.
    // If adapter-id is specified, look up a specific adapter; otherwise use the default.
    auto adapter_id = config_value("adapter-id");
    if (adapter_id) {
        adapter_ = ctx.provider("scsi", std::string(*adapter_id));
    } else {
        adapter_ = ctx.provider("scsi");
    }
    if (!adapter_) {
        throw std::runtime_error(
            "ScsiHardDiscExtension: no SCSI adapter found (missing 'scsi' provider)");
    }

    // The "scsi" extension point contract guarantees the provider is an
    // AcornScsiHostAdapter. We use static_cast rather than dynamic_cast to
    // avoid requiring the adapter's RTTI (typeinfo) in the plugin binary,
    // which would force linking the adapter's static library and cause
    // duplicate protobuf descriptor registration at dlopen time.
    auto* scsi_adapter = static_cast<AcornScsiHostAdapter*>(adapter_);

    // Read SCSI target ID from config (default 0)
    uint8_t target_id = 0;
    auto scsi_id_str = config_value("scsi-id");
    if (scsi_id_str) {
        target_id = static_cast<uint8_t>(std::stoi(std::string(*scsi_id_str)));
    } else if (scsi_id_ != 0) {
        // Legacy: use programmatic setter value
        target_id = scsi_id_;
    }

    // Read image filepath from config
    std::filesystem::path image_path;
    auto image_str = config_value("image");
    if (image_str) {
        image_path = std::string(*image_str);
    } else if (!image_filepath_.empty()) {
        // Legacy: use programmatic setter value
        image_path = image_filepath_;
    }

    // If an image filepath was configured, load it and install as a target
    if (!image_path.empty()) {
        auto image = HardDiskImage::open(image_path);
        disc_ = std::make_unique<ScsiHardDisc>(std::move(image));
        scsi_adapter->target_registry().install(target_id, std::move(disc_));
        scsi_adapter->target_registry().wire_to_bus(scsi_adapter->bus());

        // Register a per-LUN activity indicator with the adapter, which owns
        // the filter policy. The indicator is named for the SCSI ID, matching
        // the floppy convention (floppy-N-activity-led -> hdd-N-activity-led).
        // Skipped silently when the machine has no Indicators registry, so
        // tests that don't care about LEDs work unchanged.
        if (ctx.has_indicators()) {
            std::string indicator_name = "hdd-" + std::to_string(target_id) + "-activity-led";
            std::string label = "HDD " + std::to_string(target_id);
            scsi_adapter->register_target_indicator(
                target_id,
                std::move(indicator_name),
                {{"label", std::move(label)},
                 {"color", "600nm"},        // orange HDD activity LED
                 {"shape", "rectangular"}});
        }
    }
}

void ScsiHardDiscExtension::shutdown() {
    // Target is owned by the registry, not by us, so nothing to clean up
    // (the registry's unique_ptr was moved in init)
    disc_ = nullptr;
    adapter_ = nullptr;
}

}  // namespace beebium
