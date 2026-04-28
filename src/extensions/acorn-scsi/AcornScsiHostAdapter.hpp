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
#include "ScsiConstants.hpp"
#include "ScsiTargetRegistry.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/indicators/IndicatorFilter.hpp>
#include <beebium/indicators/Indicators.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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

    // Register a per-LUN activity indicator. Called by SCSI target extensions
    // (e.g. scsi-hard-disc) during their own init() after the adapter has been
    // initialised. The adapter owns the filter policy (RetriggerableMonostable
    // with a fixed pulse width) so that all SCSI activity LEDs share the same
    // visual semantics. The caller supplies the indicator name and metadata
    // (label, color, shape) which determine its presentation.
    //
    // Subsequent SCSI activity on this LUN will drive the indicator value
    // through the adapter's bus activity callback.
    //
    // Throws if init() has not yet been called (no Indicators registry
    // available) or if the LUN is out of range.
    //
    // Defined inline because each plugin dylib that depends on the SCSI
    // extension point (e.g. scsi-hard-disc) compiles its own caller to this
    // method without linking the acorn-scsi library at static-link time.
    void register_target_indicator(
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

    // OneMHzBusDevice
    uint8_t read(uint16_t offset) override {
        return bus_.read_register(static_cast<uint8_t>(offset - kBaseOffset));
    }

    void write(uint16_t offset, uint8_t value) override {
        bus_.write_register(static_cast<uint8_t>(offset - kBaseOffset), value);
    }

    bool irq_pending() const override {
        return bus_.irq_pending();
    }

    // Access to internals for testing and service
    ScsiBus& bus() { return bus_; }
    const ScsiBus& bus() const { return bus_; }
    ScsiTargetRegistry& target_registry() { return registry_; }
    const ScsiTargetRegistry& target_registry() const { return registry_; }

private:
    // Pulse width for the SCSI activity LED. A SCSI transaction is typically
    // dispatched in a fraction of a millisecond; without this stretch the LED
    // would flash imperceptibly. 80 ms is fast enough to flicker visibly under
    // sustained access and slow enough that single transactions register.
    static constexpr std::chrono::milliseconds kActivityPulseWidth{80};

    // 0xFFFF means "no indicator registered for this LUN".
    static constexpr uint16_t kNoIndicator = 0xFFFF;

    ScsiBus bus_;
    ScsiTargetRegistry registry_;
    std::unique_ptr<ScsiHostAdapterServiceImpl> service_;

    // Captured from ExtensionContext during init(). Non-owning; lifetime is
    // managed by the machine.
    Indicators* indicators_ = nullptr;

    // Per-LUN indicator IDs. kNoIndicator means no indicator has been
    // registered for that LUN; activity events for it are silently dropped.
    std::array<uint16_t, scsi::MAX_TARGETS> indicator_ids_{};
};

}  // namespace beebium

#endif  // BEEBIUM_ACORN_SCSI_HOST_ADAPTER_HPP
