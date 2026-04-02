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

#include "beebium/extension/UserPort.hpp"
#include "beebium/Via6522.hpp"

#include <stdexcept>

namespace beebium {

// Bridge between ViaPeripheral (what the VIA calls) and UserPortDevice
// (what extensions implement). Forwards only Port B and CB1/CB2,
// leaving Port A and CA1/CA2 at their ViaPeripheral defaults.
class UserPort::Bridge : public ViaPeripheral {
public:
    explicit Bridge(UserPortDevice& device) : device_(device) {}

    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        return device_.update_port_b(output, ddr);
    }

    void update_control_lines(uint8_t& ca1, uint8_t& ca2,
                              uint8_t& cb1, uint8_t& cb2) override {
        // Port A control lines: leave untouched (no device on Port A)
        (void)ca1;
        (void)ca2;
        // Port B control lines: forward to the attached device
        device_.update_control_lines(cb1, cb2);
    }

    // update_port_a not overridden -- returns ViaPeripheral default (0xFF)

private:
    UserPortDevice& device_;
};

UserPort::UserPort(Via6522& user_via)
    : user_via_(user_via) {}

UserPort::~UserPort() = default;

void UserPort::attach(UserPortDevice& device) {
    if (device_) {
        throw std::runtime_error(
            "UserPort: a device is already attached (only one User Port device is supported)");
    }
    device_ = &device;
    bridge_ = std::make_unique<Bridge>(device);
    user_via_.set_peripheral(bridge_.get());
}

bool UserPort::is_occupied() const {
    return device_ != nullptr;
}

}  // namespace beebium
