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

#ifndef BEEBIUM_USER_PORT_RTC_EXTENSION_HPP
#define BEEBIUM_USER_PORT_RTC_EXTENSION_HPP

#include "Saf3019p.hpp"
#include "UserPortRtcService.hpp"
#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/extension/UserPortDevice.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

class UserPortRtcExtension : public PeripheralExtension,
                              public UserPortDevice {
public:
    UserPortRtcExtension() = default;
    ~UserPortRtcExtension() override = default;

    // PeripheralExtension interface
    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;
    std::vector<grpc::Service*> grpc_services() override;

    // UserPortDevice interface
    uint8_t update_port_b(uint8_t output, uint8_t ddr) override;

    // Access for gRPC service
    Saf3019p& chip() { return chip_; }

private:
    Saf3019p chip_;
    bool ddr_reset_armed_ = false;
    std::unique_ptr<UserPortRtcServiceImpl> service_;
};

}  // namespace beebium

#endif  // BEEBIUM_USER_PORT_RTC_EXTENSION_HPP
