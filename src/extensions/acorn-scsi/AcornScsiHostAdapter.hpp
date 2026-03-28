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

#ifndef BEEBIUM_ACORN_SCSI_HOST_ADAPTER_HPP
#define BEEBIUM_ACORN_SCSI_HOST_ADAPTER_HPP

#include "ScsiBus.hpp"
#include "ScsiTargetRegistry.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

class ScsiHostAdapterServiceImpl;

// Acorn SCSI Host Adapter extension for the 1 MHz bus.
//
// Emulates the register interface at 0xFC40-0xFC43 (FRED offsets 0x40-0x43).
// Delegates bus protocol to ScsiBus and target management to ScsiTargetRegistry.
// Provides a "scsi" extension point for future sub-bus targets.
//
// Can be linked as built-in (Master AIV) or loaded as a plugin (Model B).
class AcornScsiHostAdapter : public PeripheralExtension,
                              public OneMHzBusDevice {
public:
    static constexpr uint16_t kBaseOffset = 0x40;  // FRED offset for 0xFC40
    static constexpr uint16_t kEndOffset = 0x43;   // Through 0xFC43

    AcornScsiHostAdapter();
    ~AcornScsiHostAdapter() override;

    static std::unique_ptr<AcornScsiHostAdapter> create();

    // PeripheralExtension
    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }

    std::span<const std::string_view> provides() const override {
        static constexpr std::string_view ext_points[] = {"scsi"};
        return ext_points;
    }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;
    std::vector<grpc::Service*> grpc_services() override;

    // OneMHzBusDevice
    uint8_t read(uint16_t offset) override {
        return bus_.read_register(static_cast<uint8_t>(offset - kBaseOffset));
    }

    void write(uint16_t offset, uint8_t value) override {
        bus_.write_register(static_cast<uint8_t>(offset - kBaseOffset), value);
    }

    // Access to internals for testing and service
    ScsiBus& bus() { return bus_; }
    const ScsiBus& bus() const { return bus_; }
    ScsiTargetRegistry& target_registry() { return registry_; }
    const ScsiTargetRegistry& target_registry() const { return registry_; }

private:
    ScsiBus bus_;
    ScsiTargetRegistry registry_;
    std::unique_ptr<ScsiHostAdapterServiceImpl> service_;
};

}  // namespace beebium

#endif  // BEEBIUM_ACORN_SCSI_HOST_ADAPTER_HPP
