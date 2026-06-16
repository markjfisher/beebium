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

#ifndef BEEBIUM_ACORN_RTC_EXTENSION_HPP
#define BEEBIUM_ACORN_RTC_EXTENSION_HPP

#include "AcornRtcUi.hpp"
#include "Saf3019p.hpp"
#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/extension/UserPortDevice.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

class ExtensionRpcDispatcher;
class AcornRtcDispatcher;  // defined in AcornRtcDispatcher.hpp

class AcornRtcExtension : public PeripheralExtension,
                              public UserPortDevice {
public:
    // Both defined in the .cpp where AcornRtcDispatcher is complete (the
    // unique_ptr<AcornRtcDispatcher> member needs it for construction cleanup
    // and destruction).
    AcornRtcExtension();
    ~AcornRtcExtension() override;

    // PeripheralExtension interface
    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

    // ExtensionUi panel for the Peripherals sidebar. Returns a stable
    // pointer (owned by this extension) showing the current chip
    // time and active register layout.
    ExtensionUi* ui() override { return &ui_; }

    // UserPortDevice interface
    uint8_t update_port_b(uint8_t output, uint8_t ddr) override;

    // Access for the dispatcher and UI.
    Saf3019p& chip() { return chip_; }

private:
    Saf3019p chip_;
    bool ddr_reset_armed_ = false;
    std::unique_ptr<AcornRtcDispatcher> dispatcher_;
    AcornRtcUi ui_{*this};
};

}  // namespace beebium

#endif  // BEEBIUM_ACORN_RTC_EXTENSION_HPP
