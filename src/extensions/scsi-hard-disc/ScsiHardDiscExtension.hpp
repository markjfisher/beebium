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

#ifndef BEEBIUM_SCSI_HARD_DISC_EXTENSION_HPP
#define BEEBIUM_SCSI_HARD_DISC_EXTENSION_HPP

#include <beebium/extension/PeripheralExtension.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace beebium {

class ScsiHardDisc;

// Peripheral extension that attaches a SCSI hard disc to an Acorn SCSI
// host adapter. Discovers the adapter via the "scsi" extension point
// and installs a ScsiHardDisc target in its target registry.
//
// The disc image filepath and SCSI target ID can be set before init()
// via set_image_filepath() and set_scsi_id(). If no image is set, the
// extension initialises without a disc (targets can be mounted later
// via gRPC).
class ScsiHardDiscExtension : public PeripheralExtension {
public:
    ScsiHardDiscExtension();
    ~ScsiHardDiscExtension() override;

    static std::unique_ptr<ScsiHardDiscExtension> create();

    // Configuration (call before init)
    void set_image_filepath(std::filesystem::path filepath) { image_filepath_ = std::move(filepath); }
    void set_scsi_id(uint8_t id) { scsi_id_ = id; }

    // PeripheralExtension
    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"scsi"};
        return deps;
    }

    std::span<const std::string_view> provides() const override { return {}; }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;

private:
    std::filesystem::path image_filepath_;
    uint8_t scsi_id_ = 0;
    std::unique_ptr<ScsiHardDisc> disc_;
    PeripheralExtension* adapter_ = nullptr;  // non-owning, from context
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_HARD_DISC_EXTENSION_HPP
